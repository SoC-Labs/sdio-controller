//-----------------------------------------------------------------------------
// pynq_sdio_top.v — PYNQ-Z2 standalone SDIO controller top-level
//
// Zynq PS GP0 (AXI-Lite master) → sdio AXI-Lite slave (control)
// sdio AXI4 DMA master → Zynq PS HP0 (DDR access)
// Physical SDIO signals on PMOD JA (IOBUFs handled inside sdfrontend)
//
// AXI-Lite registers (5-bit internal address, see sdio.v for full map):
//   0x00 = control/status
//   0x04 = argument
//   0x08 = command
//
// Tie-offs for unused features:
//   i_card_detect = 1  (card always present — PMOD has no CD pin)
//   i_ds          = 0  (no HS400 data strobe)
//   S_AC_VALID    = 0  (no async CMD path)
//   S_AD_VALID    = 0  (no async DAT path)
//   i_1p8v        = 0  (3.3V PMOD only)
//-----------------------------------------------------------------------------
`default_nettype none

module pynq_sdio_top #(
    parameter ADDRESS_WIDTH  = 32,
    parameter DMA_DW         = 32,
    parameter AXI_IW         = 1,
    parameter NUMIO          = 4
) (
    // -----------------------------------------------------------------------
    // Clocks / resets
    // -----------------------------------------------------------------------
    input  wire         i_clk,        // 100 MHz from PS FCLK0
    input  wire         i_reset_n,    // active-low from proc_sys_reset

    // -----------------------------------------------------------------------
    // AXI-Lite slave — from Zynq PS GP0 via AXI Interconnect (control)
    // Address is 32-bit to match AXI Interconnect master port width;
    // only the lower 5 bits are used internally by sdio.v.
    // -----------------------------------------------------------------------
    // Write address channel
    input  wire         S_AXIL_AWVALID,
    output wire         S_AXIL_AWREADY,
    input  wire [31:0]  S_AXIL_AWADDR,
    input  wire [2:0]   S_AXIL_AWPROT,
    // Write data channel
    input  wire         S_AXIL_WVALID,
    output wire         S_AXIL_WREADY,
    input  wire [31:0]  S_AXIL_WDATA,
    input  wire [3:0]   S_AXIL_WSTRB,
    // Write response channel
    output wire         S_AXIL_BVALID,
    input  wire         S_AXIL_BREADY,
    output wire [1:0]   S_AXIL_BRESP,
    // Read address channel
    input  wire         S_AXIL_ARVALID,
    output wire         S_AXIL_ARREADY,
    input  wire [31:0]  S_AXIL_ARADDR,
    input  wire [2:0]   S_AXIL_ARPROT,
    // Read data channel
    output wire         S_AXIL_RVALID,
    input  wire         S_AXIL_RREADY,
    output wire [31:0]  S_AXIL_RDATA,
    output wire [1:0]   S_AXIL_RRESP,

    // -----------------------------------------------------------------------
    // AXI4 master — to Zynq PS HP0 (DMA)
    // -----------------------------------------------------------------------
    // Write address channel
    output wire [AXI_IW-1:0]         M_AXI_AWID,
    output wire [ADDRESS_WIDTH-1:0]   M_AXI_AWADDR,
    output wire [7:0]                 M_AXI_AWLEN,
    output wire [2:0]                 M_AXI_AWSIZE,
    output wire [1:0]                 M_AXI_AWBURST,
    output wire                       M_AXI_AWLOCK,
    output wire [3:0]                 M_AXI_AWCACHE,
    output wire [2:0]                 M_AXI_AWPROT,
    output wire [3:0]                 M_AXI_AWQOS,
    output wire                       M_AXI_AWVALID,
    input  wire                       M_AXI_AWREADY,
    // Write data channel
    output wire [DMA_DW-1:0]          M_AXI_WDATA,
    output wire [DMA_DW/8-1:0]        M_AXI_WSTRB,
    output wire                       M_AXI_WLAST,
    output wire                       M_AXI_WVALID,
    input  wire                       M_AXI_WREADY,
    // Write response channel
    input  wire [AXI_IW-1:0]         M_AXI_BID,
    input  wire [1:0]                 M_AXI_BRESP,
    input  wire                       M_AXI_BVALID,
    output wire                       M_AXI_BREADY,
    // Read address channel
    output wire [AXI_IW-1:0]         M_AXI_ARID,
    output wire [ADDRESS_WIDTH-1:0]   M_AXI_ARADDR,
    output wire [7:0]                 M_AXI_ARLEN,
    output wire [2:0]                 M_AXI_ARSIZE,
    output wire [1:0]                 M_AXI_ARBURST,
    output wire                       M_AXI_ARLOCK,
    output wire [3:0]                 M_AXI_ARCACHE,
    output wire [2:0]                 M_AXI_ARPROT,
    output wire [3:0]                 M_AXI_ARQOS,
    output wire                       M_AXI_ARVALID,
    input  wire                       M_AXI_ARREADY,
    // Read data channel
    input  wire [AXI_IW-1:0]         M_AXI_RID,
    input  wire [DMA_DW-1:0]          M_AXI_RDATA,
    input  wire [1:0]                 M_AXI_RRESP,
    input  wire                       M_AXI_RLAST,
    input  wire                       M_AXI_RVALID,
    output wire                       M_AXI_RREADY,

    // -----------------------------------------------------------------------
    // Interrupt to PS
    // -----------------------------------------------------------------------
    output wire         o_int,

    // -----------------------------------------------------------------------
    // Physical SDIO pins — PMOD JA
    // CLK  → Y18 (output only)
    // CMD  → Y19 (bidirectional)
    // DAT0 → U18, DAT1 → U19, DAT2 → W18, DAT3 → W19 (bidirectional)
    // IOBUFs are instantiated INSIDE sdfrontend.v
    // -----------------------------------------------------------------------
    output wire         sdio_clk,
    inout  wire         sdio_cmd,
    inout  wire [3:0]   sdio_dat
);

    // -----------------------------------------------------------------------
    // Internal wires — sdio ↔ sdfrontend
    // -----------------------------------------------------------------------
    wire        w_cfg_ddr;
    wire        w_cfg_ds;
    wire        w_cfg_dscmd;
    wire [4:0]  w_cfg_sample_shift;

    wire [7:0]  w_sdclk;
    wire        w_cmd_en;
    wire        w_cmd_tristate;
    wire [1:0]  w_cmd_data;

    wire        w_data_en;
    wire        w_data_tristate;
    wire        w_rx_en;
    wire [31:0] w_tx_data;

    wire [1:0]  w_cmd_strb;
    wire [1:0]  w_rx_cmd_data;
    wire        w_cmd_collision;
    wire        w_card_busy;
    wire [1:0]  w_rx_strb;
    wire [15:0] w_rx_data;
    wire        w_crcack;
    wire        w_crcnak;

    // OPT_DS async paths — connect frontend outputs back to sdio inputs
    wire        w_mac_valid;
    wire [1:0]  w_mac_data;
    wire        w_mad_valid;
    wire [31:0] w_mad_data;
    wire [31:0] w_frontend_debug;
    wire [31:0] w_phy_debug;

    // -----------------------------------------------------------------------
    // SDIO controller
    // sdio.v expects a 5-bit AXI-Lite address; slice lower bits from the
    // 32-bit port that comes from the AXI Interconnect.
    // -----------------------------------------------------------------------
    sdio #(
        .OPT_DMA            (1),
        .ADDRESS_WIDTH      (ADDRESS_WIDTH),
        .DMA_DW             (DMA_DW),
        .AXI_IW             (AXI_IW),
        .OPT_LITTLE_ENDIAN  (1),
        .OPT_SERDES         (0),
        .OPT_DDR            (0),
        .OPT_DS             (0),
        .OPT_EMMC           (0),
        .OPT_HWRESET        (0),    // explicit: Verilog-2001 won't inherit OPT_EMMC override
        .OPT_CARD_DETECT    (0),    // no CD pin on PMOD — card always present
        .OPT_1P8V           (0),
        .OPT_CRCTOKEN       (1)
    ) u_sdio (
        .i_clk              (i_clk),
        .i_reset            (~i_reset_n),

        // AXI-Lite slave (only lower 5 bits of address used)
        .S_AXIL_AWVALID     (S_AXIL_AWVALID),
        .S_AXIL_AWREADY     (S_AXIL_AWREADY),
        .S_AXIL_AWADDR      (S_AXIL_AWADDR[4:0]),
        .S_AXIL_AWPROT      (S_AXIL_AWPROT),
        .S_AXIL_WVALID      (S_AXIL_WVALID),
        .S_AXIL_WREADY      (S_AXIL_WREADY),
        .S_AXIL_WDATA       (S_AXIL_WDATA),
        .S_AXIL_WSTRB       (S_AXIL_WSTRB),
        .S_AXIL_BVALID      (S_AXIL_BVALID),
        .S_AXIL_BREADY      (S_AXIL_BREADY),
        .S_AXIL_BRESP       (S_AXIL_BRESP),
        .S_AXIL_ARVALID     (S_AXIL_ARVALID),
        .S_AXIL_ARREADY     (S_AXIL_ARREADY),
        .S_AXIL_ARADDR      (S_AXIL_ARADDR[4:0]),
        .S_AXIL_ARPROT      (S_AXIL_ARPROT),
        .S_AXIL_RVALID      (S_AXIL_RVALID),
        .S_AXIL_RREADY      (S_AXIL_RREADY),
        .S_AXIL_RDATA       (S_AXIL_RDATA),
        .S_AXIL_RRESP       (S_AXIL_RRESP),

        // AXI4 DMA master
        .M_AXI_AWID         (M_AXI_AWID),
        .M_AXI_AWADDR       (M_AXI_AWADDR),
        .M_AXI_AWLEN        (M_AXI_AWLEN),
        .M_AXI_AWSIZE       (M_AXI_AWSIZE),
        .M_AXI_AWBURST      (M_AXI_AWBURST),
        .M_AXI_AWLOCK       (M_AXI_AWLOCK),
        .M_AXI_AWCACHE      (M_AXI_AWCACHE),
        .M_AXI_AWPROT       (M_AXI_AWPROT),
        .M_AXI_AWQOS        (M_AXI_AWQOS),
        .M_AXI_AWVALID      (M_AXI_AWVALID),
        .M_AXI_AWREADY      (M_AXI_AWREADY),
        .M_AXI_WDATA        (M_AXI_WDATA),
        .M_AXI_WSTRB        (M_AXI_WSTRB),
        .M_AXI_WLAST        (M_AXI_WLAST),
        .M_AXI_WVALID       (M_AXI_WVALID),
        .M_AXI_WREADY       (M_AXI_WREADY),
        .M_AXI_BID          (M_AXI_BID),
        .M_AXI_BRESP        (M_AXI_BRESP),
        .M_AXI_BVALID       (M_AXI_BVALID),
        .M_AXI_BREADY       (M_AXI_BREADY),
        .M_AXI_ARID         (M_AXI_ARID),
        .M_AXI_ARADDR       (M_AXI_ARADDR),
        .M_AXI_ARLEN        (M_AXI_ARLEN),
        .M_AXI_ARSIZE       (M_AXI_ARSIZE),
        .M_AXI_ARBURST      (M_AXI_ARBURST),
        .M_AXI_ARLOCK       (M_AXI_ARLOCK),
        .M_AXI_ARCACHE      (M_AXI_ARCACHE),
        .M_AXI_ARPROT       (M_AXI_ARPROT),
        .M_AXI_ARQOS        (M_AXI_ARQOS),
        .M_AXI_ARVALID      (M_AXI_ARVALID),
        .M_AXI_ARREADY      (M_AXI_ARREADY),
        .M_AXI_RID          (M_AXI_RID),
        .M_AXI_RDATA        (M_AXI_RDATA),
        .M_AXI_RRESP        (M_AXI_RRESP),
        .M_AXI_RLAST        (M_AXI_RLAST),
        .M_AXI_RVALID       (M_AXI_RVALID),
        .M_AXI_RREADY       (M_AXI_RREADY),

        // Misc
        .i_card_detect      (1'b1),
        .o_hwreset_n        (),
        .o_1p8v             (),
        .i_1p8v             (1'b0),
        .o_int              (o_int),

        // PHY interface → sdfrontend
        .o_cfg_ddr          (w_cfg_ddr),
        .o_cfg_ds           (w_cfg_ds),
        .o_cfg_dscmd        (w_cfg_dscmd),
        .o_cfg_sample_shift (w_cfg_sample_shift),
        .o_sdclk            (w_sdclk),
        .o_cmd_en           (w_cmd_en),
        .o_cmd_tristate     (w_cmd_tristate),
        .o_cmd_data         (w_cmd_data),
        .o_data_en          (w_data_en),
        .o_data_tristate    (w_data_tristate),
        .o_rx_en            (w_rx_en),
        .o_tx_data          (w_tx_data),
        .i_cmd_strb         (w_cmd_strb),
        .i_cmd_data         (w_rx_cmd_data),
        .i_cmd_collision    (w_cmd_collision),
        .i_card_busy        (w_card_busy),
        .i_rx_strb          (w_rx_strb),
        .i_rx_data          (w_rx_data),
        .i_crcack           (w_crcack),
        .i_crcnak           (w_crcnak),

        // Async DS paths — OPT_DS=0, loop frontend outputs back
        .S_AC_VALID         (w_mac_valid),
        .S_AC_DATA          (w_mac_data),
        .S_AD_VALID         (w_mad_valid),
        .S_AD_DATA          (w_mad_data),
        .i_phy_debug        (w_phy_debug)
    );

    // -----------------------------------------------------------------------
    // SDIO PHY (handles Xilinx IOBUFs internally)
    // -----------------------------------------------------------------------
    sdfrontend #(
        .OPT_SERDES     (0),
        .OPT_DDR        (0),
        .OPT_DS         (0),
        .OPT_COLLISION  (0),
        .OPT_CRCTOKEN   (1),
        .BUSY_CLOCKS    (16),
        .NUMIO          (4)
    ) u_sdfrontend (
        .i_clk              (i_clk),
        .i_hsclk            (1'b0),   // unused — OPT_SERDES=0 needs no high-speed clock
        .i_reset            (~i_reset_n),

        // Configuration
        .i_cfg_ddr          (w_cfg_ddr),
        .i_cfg_ds           (w_cfg_ds),
        .i_cfg_dscmd        (w_cfg_dscmd),
        .i_sample_shift     (w_cfg_sample_shift),

        // Clock & data from sdio
        .i_sdclk            (w_sdclk),
        .i_cmd_en           (w_cmd_en),
        .i_cmd_tristate     (w_cmd_tristate),
        .i_cmd_data         (w_cmd_data),
        .i_data_en          (w_data_en),
        .i_data_tristate    (w_data_tristate),
        .i_rx_en            (w_rx_en),
        .i_tx_data          (w_tx_data),

        // Feedback to sdio
        .o_cmd_strb         (w_cmd_strb),
        .o_cmd_data         (w_rx_cmd_data),
        .o_cmd_collision    (w_cmd_collision),
        .o_data_busy        (w_card_busy),  // port is o_data_busy, not o_card_busy
        .o_rx_strb          (w_rx_strb),
        .o_rx_data          (w_rx_data),
        .o_crcack           (w_crcack),
        .o_crcnak           (w_crcnak),

        // Async CMD/DAT (OPT_DS path — loop back to sdio S_AC/S_AD)
        .MAC_VALID          (w_mac_valid),
        .MAC_DATA           (w_mac_data),
        .MAD_VALID          (w_mad_valid),
        .MAD_DATA           (w_mad_data),

        // Physical pins — PMOD JA
        // io_cmd / io_dat are inout ports with IOBUFs inside sdfrontend
        // (active under `ifndef VERILATOR, which applies during Vivado synthesis)
        .o_ck               (sdio_clk),
        .i_ds               (1'b0),
        .io_cmd             (sdio_cmd),
        .io_dat             (sdio_dat),

        .o_debug            (w_frontend_debug)
    );

    // Linux-visible board debug at AXI offset 0x18.  Keep this at the wrapper
    // boundary so it proves both sides of the command PHY connection.  The
    // sdaxil register latches an OR of these bits, so the two *_low flags are
    // sticky evidence for whether CMD was ever pulled low.
    assign w_phy_debug = {
        4'h6,
        w_cmd_en,
        w_cmd_tristate,
        w_cmd_data,
        w_cmd_strb,
        w_rx_cmd_data,
        w_cmd_collision,
        w_card_busy,
        w_cmd_en && !w_cmd_tristate && (w_cmd_data != 2'b11),
        !w_cmd_en && w_cmd_tristate && !w_frontend_debug[21],
        w_frontend_debug[21:20],
        w_frontend_debug[13:0]
    };

endmodule
`default_nettype wire
