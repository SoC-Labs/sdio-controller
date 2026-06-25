# SDIO Controller — SoC Labs

ZipCPU-derived AXI-Lite SD card controller, validated by SoC Labs at the
University of Southampton. Structured as a standalone repository so it can
be imported into MegaSoC or any other SoC Labs project.

## Validated configuration

| Parameter       | Value           |
|-----------------|-----------------|
| Bus width       | 4-bit           |
| Clock           | 50 MHz          |
| Sample phase    | 24              |
| SDMULTI (DMA)   | 1               |
| PHY register    | 0xf9183402      |
| Platform        | PYNQ-Z2 PMOD JA |
| Test result     | 14/14 TCs pass  |
| Soak runs       | 120 (24 configs x 5 runs, 100% STABLE) |
| FatFs result    | 256 MB validated, hash=0xdc6235a5 |

Also confirmed functionally correct (zero UVM/SDIO errors, TC1-TC14 pass)
against the Synopsys VC-VIP-SOC eMMC/SD card UVM agent.

## Repository structure

```
rtl/sdspi/    18 synthesisable RTL files (ZipCPU SDIO core, AXI-Lite + DMA path)
sw/           sdiodrv.c and sdiodrv.h (AXI-Lite software driver)
docs/         Verification reports (FPGA bring-up, Synopsys VIP)
```

The `rtl/sdspi/` file list is exactly the set referenced by the SDIO IP
filelist used in the MegaSoC integration (`megasoc_tech/flist/IP/SDIO.flist`).
It does not include the SPI-mode driver files (`sdspi.v`, `spicmd.v`,
`spirxdata.v`, `spitxdata.v`, `llsdspi.v`, `exportspi.v`) or the
SERDES/DDR/wrapper files (`xsdddr.v`, `xsdserdes8x.v`, `sdio_top.v`), none of
which are used by this AXI-Lite, non-SERDES, non-DDR integration. The
behavioural test models (`mdl_sdio.v`, `mdl_sdtx.v`, under
`bench/verilog/`) are deliberately excluded — they are simulation-only test
doubles, not part of the reusable IP.

## sdfrontend parameters (as instantiated for this integration)

| Parameter      | Value  |
|----------------|--------|
| OPT_SERDES     | 1'b0   | Raw IOBUFs, no SERDES |
| OPT_DDR        | 1'b0   | Single data rate |
| OPT_COLLISION  | 1'b0   |  |
| OPT_CRCTOKEN   | 1'b1   |  |
| BUSY_CLOCKS    | 16     |  |
| NUMIO          | 4      |  |

## How to integrate into another SoC Labs project

1. Clone or add as a submodule:
   ```bash
   git submodule add https://git.soton.ac.uk/soclabs/sdio-controller.git
   ```
2. Add `rtl/sdspi/*.v` to your project filelist.
3. Instantiate `sdio`/`sdfrontend` from `rtl/sdspi/` in your top-level RTL.
4. Add `sw/sdiodrv.c` and `sw/sdiodrv.h` to your software build.
5. AXI-Lite base address and DMA configuration depend on your SoC interconnect.

## Licence

RTL files are derived from the ZipCPU SDIO project by Dan Gisselquist and
are licensed under LGPL-3. SoC Labs additions (driver, docs) are provided
under the same licence. See individual file headers.
