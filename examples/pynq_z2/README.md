# PYNQ-Z2 standalone example

A minimal Vivado project and Linux userspace test harness that exercises
`rtl/sdspi/` directly over AXI-Lite (control) + AXI4 DMA (data) on real
PYNQ-Z2 hardware, independent of the MegaSoC chip integration. This is the
setup used to produce the validated configuration in the top-level README
(4-bit, 50 MHz, phase 24, SDMULTI=1 -- see `docs/sdio_verification_report.md`).

Built with Vivado 2024.1, target part `xc7z020clg400-1` (Zynq-7020, as used
on PYNQ-Z2).

## Contents

```
bd/pynq_sdio_top.tcl   Block design build script (PS7 <-> sdio_top)
constrs/*.xdc          Pin map (PMOD JA) and timing constraints
rtl/top/pynq_sdio_top.v  Board-level top wrapper around rtl/sdspi/sdio.v
sw/                    Linux userspace test harness for PYNQ-Z2
```

## Block design topology

```
PS7 GP0 (AXI3) -> axi_interconnect_0 -> sdio_top_0/S_AXIL   (control, AXI-Lite)
sdio_top_0/M_AXI (AXI4) -> axi_smartconnect_dma -> PS7 HP0 (AXI3)  (DMA)
```

AXI SmartConnect performs the AXI4->AXI3 protocol conversion on the DMA
path; the AXI Interconnect performs AXI3->AXI4-Lite on the control path.
`pynq_sdio_top` is added to the block design as a Verilog module reference;
Vivado infers the `M_AXI` and `S_AXIL` bus interfaces from the port naming
convention in `pynq_sdio_top.v`.

SDIO AXI-Lite control register base address: `0x40000000` (PS GP0 space, 4KB
range -- set via `assign_bd_address` in the block design script).

## How to regenerate the FPGA project

1. Create a new Vivado 2024.1 RTL project targeting part `xc7z020clg400-1`
   (or board part `tul.com.tw:pynq-z2:part0:1.0` if installed).
2. Add all of `rtl/sdspi/*.v` (from the repo root) plus `rtl/top/pynq_sdio_top.v`
   (from this directory) as design sources.
3. Add `constrs/pynq_sdio_pinmap.xdc` and `constrs/pynq_sdio_timing.xdc` as
   constraint sources.
4. Open the Tcl console in Vivado and source the block design script:
   ```tcl
   source bd/pynq_sdio_bd.tcl
   create_pynq_sdio_bd
   ```
   This recreates the PS7 + `sdio_top` + AXI interconnect/smartconnect block
   design described above and generates the wrapper.
5. Generate the bitstream as normal (`generate_bitstream` / Vivado GUI
   "Generate Bitstream"). Everything under `.cache/`, `.gen/`, `.runs/`,
   `.sim/`, and the resulting `.bit`/`.bin` are build outputs -- they are not
   checked in here and do not need to be; they regenerate from the steps
   above.

## How to run the test harness on the board

The `sw/` files replace MegaSoC's bare-metal headers so the driver and test
applications compile and run as a normal Linux userspace process on the
PYNQ-Z2's Cortex-A9 (PYNQ ships a Linux image on the ARM core):

- `pynq_bsp.c` / `pynq_bsp.h` -- maps the SDIO register block via
  `mmap(/dev/mem)` at `SYS_SDIO_BASE = 0x40000000` and stubs out the
  megasoc-specific bare-metal calls (`UartStdOutInit`, `TEST_PASS`/`TEST_FAIL`,
  GIC/CMSDK headers) that `sdio_tests.c`/`sdiodrv.c` pull in transitively.
- `diskio.h` / `sdiodrv.h` -- glue headers for the FatFs/driver build.
- `sdio_tests_pynq.c` -- block-level test suite (the 14 TCs referenced in
  `docs/sdio_verification_report.md`).
- `sdio_fatsfs_pynq.c` -- FatFs filesystem-level test suite.
- `sdiodrv_board_fix.patch` -- two PYNQ-board-specific robustness fixes for
  `sw/sdiodrv.c` (CMD8/IF_COND response checking, and an ACMD41 retry bound
  instead of an unbounded busy-loop). Apply this patch to the top-level
  `sw/sdiodrv.c` before building for this board if you hit CMD8/ACMD41
  bring-up issues on your own card:
  ```bash
  cd ../../sw    # repo root sw/ directory
  patch -p0 < ../examples/pynq_z2/sw/sdiodrv_board_fix.patch
  ```

To build, on the PYNQ-Z2 itself (it has a working Linux userspace GCC):
```bash
cd examples/pynq_z2/sw
gcc -O2 -I. -I../../sw -o sdio_tests_pynq sdio_tests_pynq.c ../../sw/sdiodrv.c pynq_bsp.c
sudo ./sdio_tests_pynq    # /dev/mem access requires root
```
Substitute `sdio_fatsfs_pynq.c` (plus a FatFs source tree) for the
filesystem-level suite.

## Note on the bitstream

A prebuilt `.bit`/`.bin` is not included in this repo to avoid checking in
large, non-diffable binary build output. If you want to share a known-good
bitstream for this validated configuration, attach it to a GitLab Release
on this project rather than committing it to git history.
