#------------------------------------------------------------------------------
# pynq_sdio_timing.xdc — Timing constraints for PYNQ-Z2 SDIO project
#
# The design clock comes from PS FCLK0 (100 MHz internal) inside the BD.
# BD wrapper port names: sdio_clk_0, sdio_cmd_0, sdio_dat_0[*]
#------------------------------------------------------------------------------

#------------------------------------------------------------------------------
# SDIO board-interface reference clock.
#
# The current PYNQ verification bitstream runs the SD card bus at 5 MHz.  Use a
# matching virtual clock here so Vivado checks the implemented board interface
# against the mode we are actually validating on the PMOD wiring.
#
# Do not set this back to 20 ns just to represent the SD spec's 50 MHz high
# speed mode: the present non-SERDES fabric/PMOD output path does not meet that
# timing.  A true 25/50 MHz target should be handled as a separate hardware
# closure task, normally with IOB/ODDR/OSERDES-style launch/capture changes.
#
# The actual sdio_clk_0 port is generated inside the controller and is
# rate-programmable, so do not clock the output port directly or Vivado will
# create false internal sdio_ck timing paths through the fabric.
#------------------------------------------------------------------------------
create_clock -name sdio_ck -period 200.000

#------------------------------------------------------------------------------
# SDIO I/O timing — conservative values for PMOD cable
#------------------------------------------------------------------------------

# Output delays (CMD + DAT)
set_output_delay -clock sdio_ck -max  5.0 [get_ports { sdio_cmd_0 }]
set_output_delay -clock sdio_ck -min -1.0 [get_ports { sdio_cmd_0 }]
set_output_delay -clock sdio_ck -max  5.0 [get_ports { sdio_dat_0[*] }]
set_output_delay -clock sdio_ck -min -1.0 [get_ports { sdio_dat_0[*] }]

# Input delays
set_input_delay  -clock sdio_ck -max 14.0 [get_ports { sdio_cmd_0 }]
set_input_delay  -clock sdio_ck -min  0.0 [get_ports { sdio_cmd_0 }]
set_input_delay  -clock sdio_ck -max 14.0 [get_ports { sdio_dat_0[*] }]
set_input_delay  -clock sdio_ck -min  0.0 [get_ports { sdio_dat_0[*] }]

# SDIO frontend multicycle exceptions are applied by
# vivado/apply_sdio_io_multicycle.tcl as an impl_1 init_design post-hook.
# In this BD project, project XDCs are evaluated before link_design, so the
# PS7-generated clk_fpga_0 clock is not available here yet.
