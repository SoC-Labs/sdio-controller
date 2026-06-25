#------------------------------------------------------------------------------
# pynq_sdio_pinmap.xdc — Pin & I/O standard constraints for PYNQ-Z2
#
# PMOD SD adapter (SPI/SDIO combo) signal mapping on PMOD JA:
#   JA Pin 1 (~CS / DAT3) → Y18 → sdio_dat_0[3]
#   JA Pin 2 (MOSI / CMD)  → Y19 → sdio_cmd_0
#   JA Pin 3 (MISO / DAT0) → Y16 → sdio_dat_0[0]
#   JA Pin 4 (SCK  / CLK)  → Y17 → sdio_clk_0
#   JA Pin 7 (DAT1)         → U18 → sdio_dat_0[1]
#   JA Pin 8 (DAT2)         → U19 → sdio_dat_0[2]
#
# The BD wrapper also renames the PS DDR/FIXED_IO — those are constrained
# by the processing_system7 IP internally and need no entries here.
#------------------------------------------------------------------------------

#------------------------------------------------------------------------------
# SDIO — PMOD JA (corrected to match PMOD SD adapter pinout)
# Note: BD wrapper appends _0 to all pins made external with make_bd_pins_external
#------------------------------------------------------------------------------
set_property -dict { PACKAGE_PIN Y18  IOSTANDARD LVCMOS33  SLEW FAST  DRIVE 8 } [get_ports { sdio_dat_0[3] }]
set_property -dict { PACKAGE_PIN Y19  IOSTANDARD LVCMOS33  SLEW FAST  DRIVE 8 } [get_ports { sdio_cmd_0 }]
set_property -dict { PACKAGE_PIN Y16  IOSTANDARD LVCMOS33  SLEW FAST  DRIVE 8 } [get_ports { sdio_dat_0[0] }]
set_property -dict { PACKAGE_PIN Y17  IOSTANDARD LVCMOS33  SLEW FAST  DRIVE 8 } [get_ports { sdio_clk_0 }]
set_property -dict { PACKAGE_PIN U18  IOSTANDARD LVCMOS33  SLEW FAST  DRIVE 8 } [get_ports { sdio_dat_0[1] }]
set_property -dict { PACKAGE_PIN U19  IOSTANDARD LVCMOS33  SLEW FAST  DRIVE 8 } [get_ports { sdio_dat_0[2] }]

# SD CMD/DAT are bidirectional and idle high.  Enable weak FPGA-side pullups so
# command turnarounds and DAT3/card-select idle are well defined on PMOD wiring.
set_property PULLUP true [get_ports { sdio_cmd_0 }]
set_property PULLUP true [get_ports { sdio_dat_0[*] }]
