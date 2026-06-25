/*
 * pynq_bsp.h — Linux BSP shim for PYNQ-Z2 SDIO tests
 *
 * Replaces megasoc bare-metal headers so sdio_tests.c compiles and runs
 * as a Linux userspace process on the PYNQ-Z2 Cortex-A9.
 *
 * SDIO AXI-Lite base: 0x40000000 (PS GP0 space, assigned in block design)
 * Access method: mmap /dev/mem (requires sudo on PYNQ)
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * SDIO hardware base address in the PYNQ-Z2 FPGA address map.
 * This must match the offset assigned in the Vivado block design
 * (assign_bd_address set it to 0x40000000 with 4 KB range).
 * ----------------------------------------------------------------------- */
#define SYS_SDIO_BASE   0x40000000UL

/* -----------------------------------------------------------------------
 * Megasoc UART stdout — not needed on Linux (printf goes to stdout).
 * ----------------------------------------------------------------------- */
static inline void UartStdOutInit(void) {}

/* -----------------------------------------------------------------------
 * TEST_PASS / TEST_FAIL — print result and exit.
 * In the megasoc flow these write to a simulation end-of-test register.
 * On Linux we just print and return; main() handles the exit code.
 * ----------------------------------------------------------------------- */
static inline void TEST_PASS(void) {
    printf("\n========================================\n");
    printf("           ALL TESTS PASSED\n");
    printf("========================================\n");
}

static inline void TEST_FAIL(void) {
    printf("\n========================================\n");
    printf("           TESTS FAILED\n");
    printf("========================================\n");
}

/* -----------------------------------------------------------------------
 * Stubs for megasoc headers pulled in transitively.
 * gic400.h / CMSDK.h / sys_intr_map.h are not needed for SDIO polling.
 * system_level_functions.h has nothing used by sdio_tests.c directly.
 * ----------------------------------------------------------------------- */

/* Map the SDIO register block from physical memory.
 * Returns a pointer suitable for casting to (SDIO *).
 * Call once at the top of main() before sdio_init().               */
void *pynq_map_sdio(void);
