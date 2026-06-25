#------------------------------------------------------------------------------
# pynq_sdio_bd.tcl — Vivado block design for PYNQ-Z2 SDIO standalone project
#
# Topology:
#   PS7 GP0 (AXI3) → axi_interconnect_0 → sdio_top_0/S_AXIL  (control)
#   sdio_top_0/M_AXI (AXI4) → axi_smartconnect_dma → PS7 HP0 (AXI3)  (DMA)
#
# SmartConnect handles AXI4→AXI3 protocol conversion on the DMA path.
# AXI Interconnect handles AXI3→AXI4-Lite on the control path.
# pynq_sdio_top is a MODULE REFERENCE; Vivado infers M_AXI and S_AXIL
# interfaces automatically from the port naming.
#------------------------------------------------------------------------------

proc create_pynq_sdio_bd {} {

    create_bd_design "pynq_sdio_bd"
    update_compile_order -fileset sources_1

    # ------------------------------------------------------------------
    # Zynq PS7
    # ------------------------------------------------------------------
    set ps7 [create_bd_cell -type ip \
        -vlnv xilinx.com:ip:processing_system7:5.5 processing_system7_0]
    set_property -dict [list \
        CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {100}  \
        CONFIG.PCW_USE_M_AXI_GP0            {1}    \
        CONFIG.PCW_USE_S_AXI_HP0            {1}    \
        CONFIG.PCW_S_AXI_HP0_DATA_WIDTH     {32}   \
        CONFIG.PCW_EN_CLK0_PORT             {1}    \
        CONFIG.PCW_EN_RST0_PORT             {1}    \
        CONFIG.PCW_USE_FABRIC_INTERRUPT     {1}    \
        CONFIG.PCW_IRQ_F2P_INTR             {1}    \
    ] $ps7
    make_bd_intf_pins_external [get_bd_intf_pins processing_system7_0/DDR]
    make_bd_intf_pins_external [get_bd_intf_pins processing_system7_0/FIXED_IO]

    # ------------------------------------------------------------------
    # Processor System Reset
    # ------------------------------------------------------------------
    create_bd_cell -type ip \
        -vlnv xilinx.com:ip:proc_sys_reset:5.0 proc_sys_reset_0

    # ------------------------------------------------------------------
    # AXI Interconnect — control path: GP0 (AXI3) → S_AXIL (AXI4-Lite)
    # ------------------------------------------------------------------
    set axi_ic [create_bd_cell -type ip \
        -vlnv xilinx.com:ip:axi_interconnect:2.1 axi_interconnect_0]
    set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {1}] $axi_ic

    # ------------------------------------------------------------------
    # AXI SmartConnect — DMA path: M_AXI (AXI4) → HP0 (AXI3)
    # SmartConnect automatically converts AXI4 to AXI3.
    # ------------------------------------------------------------------
    set axi_sc [create_bd_cell -type ip \
        -vlnv xilinx.com:ip:smartconnect:1.0 axi_smartconnect_dma]
    set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {1}] $axi_sc

    # ------------------------------------------------------------------
    # pynq_sdio_top — RTL module reference
    # Vivado infers M_AXI (AXI4 master) and S_AXIL (AXI4-Lite slave)
    # from the standard port naming convention.
    # ------------------------------------------------------------------
    create_bd_cell -type module -reference pynq_sdio_top sdio_top_0

    # ------------------------------------------------------------------
    # Clocks — all driven from PS FCLK0 (100 MHz)
    # ------------------------------------------------------------------
    set fclk [get_bd_pins processing_system7_0/FCLK_CLK0]

    connect_bd_net $fclk \
        [get_bd_pins processing_system7_0/M_AXI_GP0_ACLK]   \
        [get_bd_pins processing_system7_0/S_AXI_HP0_ACLK]   \
        [get_bd_pins proc_sys_reset_0/slowest_sync_clk]      \
        [get_bd_pins axi_interconnect_0/ACLK]                \
        [get_bd_pins axi_interconnect_0/S00_ACLK]            \
        [get_bd_pins axi_interconnect_0/M00_ACLK]            \
        [get_bd_pins axi_smartconnect_dma/aclk]              \
        [get_bd_pins sdio_top_0/i_clk]

    # Associate both AXI interfaces with i_clk so Vivado knows the clock domain.
    # This suppresses the BD 41-967 critical warnings.
    set_property CONFIG.ASSOCIATED_BUSIF {M_AXI:S_AXIL} \
        [get_bd_pins sdio_top_0/i_clk]

    # ------------------------------------------------------------------
    # Resets
    # ------------------------------------------------------------------
    connect_bd_net [get_bd_pins processing_system7_0/FCLK_RESET0_N] \
        [get_bd_pins proc_sys_reset_0/ext_reset_in]

    # interconnect_aresetn → interconnect and SmartConnect
    connect_bd_net [get_bd_pins proc_sys_reset_0/interconnect_aresetn] \
        [get_bd_pins axi_interconnect_0/ARESETN]             \
        [get_bd_pins axi_interconnect_0/S00_ARESETN]         \
        [get_bd_pins axi_interconnect_0/M00_ARESETN]         \
        [get_bd_pins axi_smartconnect_dma/aresetn]

    # peripheral_aresetn → RTL module
    connect_bd_net [get_bd_pins proc_sys_reset_0/peripheral_aresetn] \
        [get_bd_pins sdio_top_0/i_reset_n]

    # ------------------------------------------------------------------
    # AXI control path: PS GP0 → AXI Interconnect → SDIO AXI-Lite
    # ------------------------------------------------------------------
    connect_bd_intf_net \
        [get_bd_intf_pins processing_system7_0/M_AXI_GP0] \
        [get_bd_intf_pins axi_interconnect_0/S00_AXI]

    connect_bd_intf_net \
        [get_bd_intf_pins axi_interconnect_0/M00_AXI] \
        [get_bd_intf_pins sdio_top_0/S_AXIL]

    # ------------------------------------------------------------------
    # AXI DMA path: SDIO M_AXI → SmartConnect → PS HP0
    # SmartConnect bridges AXI4 (from sdio_top_0) to AXI3 (PS HP0).
    # ------------------------------------------------------------------
    connect_bd_intf_net \
        [get_bd_intf_pins sdio_top_0/M_AXI] \
        [get_bd_intf_pins axi_smartconnect_dma/S00_AXI]

    connect_bd_intf_net \
        [get_bd_intf_pins axi_smartconnect_dma/M00_AXI] \
        [get_bd_intf_pins processing_system7_0/S_AXI_HP0]

    # The PS7 HP port exposes an optional FIFO status/control sideband
    # interface.  Vivado 2024.1 may promote it to top-level pins if it is left
    # floating, which then blocks bitstream generation with UCIO/NSTD DRCs.
    # Keep it internal and enable full read/write issue capability.
    if { [llength [get_bd_intf_ports -quiet S_AXI_HP0_FIFO_CTRL_0]] != 0 } {
        delete_bd_objs [get_bd_intf_nets -quiet S_AXI_HP0_FIFO_CTRL_0_1]
        delete_bd_objs [get_bd_intf_ports -quiet S_AXI_HP0_FIFO_CTRL_0]
    }
    set hp0_issue_en [create_bd_cell -type ip \
        -vlnv xilinx.com:ip:xlconstant:1.1 hp0_issue_en]
    set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL {1}] \
        $hp0_issue_en
    foreach p {
        processing_system7_0/S_AXI_HP0_RDISSUECAP1_EN
        processing_system7_0/S_AXI_HP0_WRISSUECAP1_EN
    } {
        if { [llength [get_bd_pins -quiet $p]] == 1 } {
            connect_bd_net [get_bd_pins hp0_issue_en/dout] [get_bd_pins $p]
        }
    }

    # ------------------------------------------------------------------
    # Interrupt: SDIO → PS IRQ_F2P[0]
    # ------------------------------------------------------------------
    connect_bd_net \
        [get_bd_pins sdio_top_0/o_int] \
        [get_bd_pins processing_system7_0/IRQ_F2P]

    # ------------------------------------------------------------------
    # SDIO physical pins → external BD ports
    # These become top-level ports on the generated wrapper, constrained
    # to PMOD JA pins in pynq_sdio_pinmap.xdc.
    # ------------------------------------------------------------------
    make_bd_pins_external [get_bd_pins sdio_top_0/sdio_clk]
    make_bd_pins_external [get_bd_pins sdio_top_0/sdio_cmd]
    make_bd_pins_external [get_bd_pins sdio_top_0/sdio_dat]

    # ------------------------------------------------------------------
    # Address assignment
    # auto-assign first, then refine offsets.  Do not hide failures here:
    # the software maps /dev/mem at 0x4000_0000, so a stale or differently
    # named address segment must stop the build instead of producing a
    # bitstream that silently maps SDIO elsewhere.
    # ------------------------------------------------------------------
    assign_bd_address

    set sdio_segs [get_bd_addr_segs -quiet {processing_system7_0/Data/SEG_sdio_top_0_reg0}]
    if { [llength $sdio_segs] != 1 } {
        set sdio_segs [get_bd_addr_segs -quiet -regexp {processing_system7_0/Data/SEG_sdio_top_0.*reg0}]
    }
    if { [llength $sdio_segs] != 1 } {
        puts "ERROR: Expected exactly one SDIO S_AXIL address segment, found [llength $sdio_segs]: $sdio_segs"
        puts "ERROR: Available processing_system7_0/Data segments:"
        foreach seg [get_bd_addr_segs -quiet {processing_system7_0/Data/*}] {
            puts "  $seg"
        }
        error "Cannot assign SDIO S_AXIL address segment"
    }

    set sdio_seg [lindex $sdio_segs 0]
    set_property offset 0x40000000 $sdio_seg
    set_property range 4K $sdio_seg
    puts "INFO: SDIO S_AXIL segment: $sdio_seg OFFSET=[get_property OFFSET $sdio_seg] RANGE=[get_property RANGE $sdio_seg]"

    set dma_segs [get_bd_addr_segs -quiet {sdio_top_0/M_AXI/SEG_processing_system7_0_HP0_DDR_LOWOCM}]
    if { [llength $dma_segs] == 1 } {
        set dma_seg [lindex $dma_segs 0]
        set_property offset 0x00000000 $dma_seg
        set_property range 512M $dma_seg
        puts "INFO: SDIO DMA segment: $dma_seg OFFSET=[get_property OFFSET $dma_seg] RANGE=[get_property RANGE $dma_seg]"
    } else {
        puts "WARNING: Expected one SDIO DMA address segment, found [llength $dma_segs]: $dma_segs"
    }

    # ------------------------------------------------------------------
    # Validate and save
    # ------------------------------------------------------------------
    validate_bd_design
    save_bd_design
    puts "INFO: Block design pynq_sdio_bd created and validated successfully."
}

create_pynq_sdio_bd
