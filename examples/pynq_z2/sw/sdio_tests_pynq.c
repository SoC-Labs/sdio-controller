/*
 * sdio_tests_pynq.c — SDIO integration tests adapted for PYNQ-Z2 Linux.
 *
 * Build:
 *   arm-linux-gnueabihf-gcc -O2 -Wall \
 *       -I /home/ssrs1g24/pynq_sdio_project/sw \
 *       -o sdio_test \
 *       sdio_tests_pynq.c pynq_bsp.c \
 *       /home/ssrs1g24/megasoc_project/megasoc_tech/software/lib/sw_lib/devices/src/sdiodrv.c
 *
 * Run on PYNQ-Z2 (requires sudo for /dev/mem access):
 *   sudo ./sdio_test
 */
#include "pynq_bsp.h"
#include "sdiodrv.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define SDIO_BLOCK_SIZE_FALLBACK   512u
#define SDIO_TEST_SECTOR_BASE      0x1000u
#define SDIO_MULTI_BLOCK_COUNT     2u
#define SDIO_STRESS_ITERS          8u
#define SDIO_SECTOR_ZERO        0x0000u
#define SDIO_NEIGHBOR_BASE      0x1000u
#define SDIO_BOUNDARY_BASE      0x1010u
#define SDIO_PATTERN_BASE       0x1020u
#define SDIO_RECOVERY_SECTOR    0x1030u

static SDIO    *SDIO_S   = NULL;
static SDIODRV *SDIO_DRV = NULL;

static uint32_t get_block_size_bytes(void) {
    if ((SDIO_DRV != NULL) && (SDIO_DRV->d_block_size != 0))
        return SDIO_DRV->d_block_size;
    return SDIO_BLOCK_SIZE_FALLBACK;
}

static int buffer_value(const uint8_t *buf, uint32_t len, uint8_t value) {
    for (uint32_t i = 0; i < len; i++)
        if (buf[i] != value) return 0;
    return 1;
}

static void fill_pattern(uint8_t *buf, uint32_t len, uint32_t seed) {
    for (uint32_t i = 0; i < len; i++)
        buf[i] = (uint8_t)((seed + i) & 0xFFu);
}

static int compare_buffers(const uint8_t *a, const uint8_t *b, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        if (a[i] != b[i]) return (int)(i + 1);
    return 0;
}

static void print_buffer_prefix(const uint8_t *buf, uint32_t len) {
    uint32_t shown = (len < 16u) ? len : 16u;
    printf(" Data[0:%u] =", (unsigned)(shown - 1u));
    for (uint32_t i = 0; i < shown; i++)
        printf(" %02x", (unsigned)buf[i]);
    printf("\n");
}

static void fill_const(uint8_t *buf, uint32_t len, uint8_t value) {
    memset(buf, value, len);
}

static void fill_alternating(uint8_t *buf, uint32_t len, uint8_t a, uint8_t b) {
    for (uint32_t i = 0; i < len; i++)
        buf[i] = (i & 1u) ? b : a;
}

static void fill_walking_ones(uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(1u << (i & 7u));
}

static void fill_walking_zeroes(uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        buf[i] = (uint8_t)~(1u << (i & 7u));
}

static int test_sdio_init_success(void) {
    int errors = 0;
    printf("Test1: SDIO initialization\n");
    SDIO_DRV = sdio_init(SDIO_S);
    if (SDIO_DRV == NULL) {
        printf("ERROR: sdio_init returned NULL\n");
        return 1;
    }
    printf("PASS: sdio_init returned a valid driver handle\n\n");
    return errors;
}

static int test_sdio_card_info_valid(void) {
    int errors = 0;
    printf("Test2: SDIO card information validity\n");
    if (SDIO_DRV == NULL) { printf("Error: driver handle is NULL\n"); return 1; }
    printf(" OCR = 0x%08x\n", (unsigned)SDIO_DRV->d_OCR);
    printf(" RCA = 0x%04x\n", (unsigned)SDIO_DRV->d_RCA);
    printf(" Block size = %u\n", (unsigned)SDIO_DRV->d_block_size);
    printf(" Sector count = %u\n", (unsigned)SDIO_DRV->d_sector_count);
    if (SDIO_DRV->d_block_size == 0) { printf("Warning: Block size is 0\n"); errors++; }
    if (SDIO_DRV->d_sector_count == 0) { printf("Warning: sector count is 0\n"); errors++; }
    if (SDIO_DRV->d_OCR & 0x40000000u)
        printf(" Card type = SDHC/SDXC (block addressing)\n");
    else
        printf(" Card type = SDSC/legacy (byte addressing)\n");
    printf("Test2 executed successfully\n\n");
    return errors;
}

static int test_sdio_single_block_read(void) {
    int errors = 0;
    uint8_t buf[SDIO_BLOCK_SIZE_FALLBACK];
    uint32_t block_size = get_block_size_bytes();
    printf("Test3: SDIO single block read\n");
    if (block_size > sizeof(buf)) {
        printf(" ERROR: block size %u exceeds buffer %u\n",
               (unsigned)block_size, (unsigned)sizeof(buf));
        return 1;
    }
    memset(buf, 0xA5, sizeof(buf));
    if (sdio_read(SDIO_DRV, SDIO_TEST_SECTOR_BASE, 1, (char *)buf) != 0) {
        printf("ERROR: SDIO read failure\n"); return 1;
    }
    if (buffer_value(buf, sizeof(buf), 0xA5)) {
        printf("ERROR: read buffer unchanged after sdio_read\n"); errors++;
    }
    print_buffer_prefix(buf, block_size);
    printf("PASS: Single block read complete\n\n");
    return errors;
}

static int test_sdio_read_repeatability(void) {
    int errors = 0;
    uint8_t buf1[SDIO_BLOCK_SIZE_FALLBACK];
    uint8_t buf2[SDIO_BLOCK_SIZE_FALLBACK];
    uint32_t block_size = get_block_size_bytes();
    printf("Test4: SDIO repeated single block read\n");
    memset(buf1, 0x00, sizeof(buf1));
    memset(buf2, 0x00, sizeof(buf2));
    if (sdio_read(SDIO_DRV, SDIO_TEST_SECTOR_BASE, 1, (char *)buf1) != 0) {
        printf("ERROR: first sdio_read failed\n"); return 1;
    }
    if (sdio_read(SDIO_DRV, SDIO_TEST_SECTOR_BASE, 1, (char *)buf2) != 0) {
        printf("ERROR: second sdio_read failed\n"); return 1;
    }
    int cmp = compare_buffers(buf1, buf2, block_size);
    if (cmp != 0) { printf("ERROR: repeated reads mismatch at index %d\n", cmp - 1); errors++; }
    printf("PASS Test4: Read repeatability\n\n");
    return errors;
}

static int test_sdio_single_block_write_readback(void) {
    int errors = 0;
    uint8_t tx[SDIO_BLOCK_SIZE_FALLBACK];
    uint8_t rx[SDIO_BLOCK_SIZE_FALLBACK];
    uint32_t block_size = get_block_size_bytes();
    uint32_t test_sector = SDIO_TEST_SECTOR_BASE + 1u;
    printf("Test5: SDIO single block write/readback\n");
    fill_pattern(tx, block_size, 0x10u);
    memset(rx, 0, sizeof(rx));
    if (sdio_write(SDIO_DRV, test_sector, 1, (char *)tx) != 0) {
        printf("ERROR: sdio_write failed\n"); return 1;
    }
    if (sdio_read(SDIO_DRV, test_sector, 1, (char *)rx) != 0) {
        printf("ERROR: sdio_read failed during readback\n"); return 1;
    }
    int cmp = compare_buffers(tx, rx, block_size);
    if (cmp != 0) {
        printf("ERROR: write/readback mismatch at index %d\n", cmp - 1);
        printf("TX"); print_buffer_prefix(tx, block_size);
        printf("RX"); print_buffer_prefix(rx, block_size);
        errors++;
    }
    printf("Test5 executed successfully\n\n");
    return errors;
}

static int test_sdio_multi_block_write_readback(void) {
    int errors = 0;
    uint8_t tx[SDIO_MULTI_BLOCK_COUNT * SDIO_BLOCK_SIZE_FALLBACK];
    uint8_t rx[SDIO_MULTI_BLOCK_COUNT * SDIO_BLOCK_SIZE_FALLBACK];
    uint32_t block_size = get_block_size_bytes();
    uint32_t total_len = block_size * SDIO_MULTI_BLOCK_COUNT;
    uint32_t test_sector = SDIO_TEST_SECTOR_BASE + 4u;
    printf("Test6: SDIO multi-block write/readback\n");
    fill_pattern(tx, total_len, 0x40u);
    memset(rx, 0, sizeof(rx));
    if (sdio_write(SDIO_DRV, test_sector, SDIO_MULTI_BLOCK_COUNT, (char *)tx) != 0) {
        printf("ERROR: multi-block sdio_write failed\n"); return 1;
    }
    if (sdio_read(SDIO_DRV, test_sector, SDIO_MULTI_BLOCK_COUNT, (char *)rx) != 0) {
        printf("ERROR: multi-block sdio_read failed\n"); return 1;
    }
    int cmp = compare_buffers(tx, rx, total_len);
    if (cmp != 0) { printf("ERROR: multi-block mismatch at index %d\n", cmp - 1); errors++; }
    printf("TC6 executed successfully\n\n");
    return errors;
}

static int test_sdio_invalid_args(void) {
    int errors = 0;
    uint8_t buf[SDIO_BLOCK_SIZE_FALLBACK];
    printf("Test7: SDIO invalid argument handling\n");
    if (sdio_read(NULL, SDIO_TEST_SECTOR_BASE, 1, (char *)buf) == 0) {
        printf("ERROR: sdio_read accepted NULL driver\n"); errors++;
    }
    if (sdio_read(SDIO_DRV, SDIO_TEST_SECTOR_BASE, 0, (char *)buf) == 0) {
        printf("ERROR: sdio_read accepted zero count\n"); errors++;
    }
    if (sdio_write(SDIO_DRV, SDIO_TEST_SECTOR_BASE, 1, NULL) == 0) {
        printf("ERROR: sdio_write accepted NULL buffer\n"); errors++;
    }
    printf("TC7 executed successfully\n\n");
    return errors;
}

static int test_sdio_stress_basic(void) {
    int errors = 0;
    uint8_t tx[SDIO_BLOCK_SIZE_FALLBACK];
    uint8_t rx[SDIO_BLOCK_SIZE_FALLBACK];
    uint32_t block_size = get_block_size_bytes();
    printf("Test8: SDIO repeated transfer stress\n");
    for (uint32_t iter = 0; iter < SDIO_STRESS_ITERS; iter++) {
        uint32_t sector = SDIO_TEST_SECTOR_BASE + 16u + iter;
        fill_pattern(tx, block_size, 0x80u + iter);
        memset(rx, 0, sizeof(rx));
        if (sdio_write(SDIO_DRV, sector, 1, (char *)tx) != 0) {
            printf("ERROR: write failed at iteration %u\n", (unsigned)iter); errors++; continue;
        }
        if (sdio_read(SDIO_DRV, sector, 1, (char *)rx) != 0) {
            printf("ERROR: read failed at iteration %u\n", (unsigned)iter); errors++; continue;
        }
        int cmp = compare_buffers(tx, rx, block_size);
        if (cmp != 0) {
            printf("ERROR: mismatch at iteration %u index %d\n", (unsigned)iter, cmp - 1);
            errors++;
        }
    }
    printf("TC8 executed successfully\n\n");
    return errors;
}

static int test_sdio_multisector_boundary_transition(void) {
    int errors = 0;
    uint8_t tx[1024];
    uint8_t rx[1024];
    printf("Test9: SDIO multi-sector boundary transition\n");
    fill_const(&tx[0],   512, 0xA5u);
    fill_const(&tx[512], 512, 0x5Au);
    memset(rx, 0, sizeof(rx));
    tx[511] = 0x11u;
    tx[512] = 0x22u;
    if (sdio_write(SDIO_DRV, SDIO_BOUNDARY_BASE, 2, (char *)tx) != 0) {
        printf("ERROR: two-sector write failed\n"); return 1;
    }
    if (sdio_read(SDIO_DRV, SDIO_BOUNDARY_BASE, 2, (char *)rx) != 0) {
        printf("ERROR: two-sector read failed\n"); return 1;
    }
    if (rx[511] != 0x11u) { printf("ERROR: last byte of sector N incorrect\n"); errors++; }
    if (rx[512] != 0x22u) { printf("ERROR: first byte of sector N+1 incorrect\n"); errors++; }
    if (compare_buffers(tx, rx, sizeof(tx)) != 0) {
        printf("ERROR: full boundary compare failed\n"); errors++;
    }
    printf("Test9 executed successfully\n\n");
    return errors;
}

static int test_sdio_adversarial_patterns(void) {
    int errors = 0;
    uint8_t tx[512];
    uint8_t rx[512];
    printf("Test10: SDIO adversarial data patterns\n");
    for (uint32_t p = 0; p < 6u; p++) {
        uint32_t sector = SDIO_PATTERN_BASE + p;
        switch (p) {
            case 0: fill_const(tx, sizeof(tx), 0x00u); break;
            case 1: fill_const(tx, sizeof(tx), 0xFFu); break;
            case 2: fill_const(tx, sizeof(tx), 0xAAu); break;
            case 3: fill_const(tx, sizeof(tx), 0x55u); break;
            case 4: fill_walking_ones(tx, sizeof(tx)); break;
            case 5: fill_walking_zeroes(tx, sizeof(tx)); break;
            default: fill_pattern(tx, sizeof(tx), 0x80u); break;
        }
        memset(rx, 0, sizeof(rx));
        if (sdio_write(SDIO_DRV, sector, 1, (char *)tx) != 0) {
            printf("ERROR: pattern %u write failed\n", (unsigned)p); errors++; continue;
        }
        if (sdio_read(SDIO_DRV, sector, 1, (char *)rx) != 0) {
            printf("ERROR: pattern %u read failed\n", (unsigned)p); errors++; continue;
        }
        if (compare_buffers(tx, rx, sizeof(tx)) != 0) {
            printf("ERROR: pattern %u mismatch\n", (unsigned)p); errors++;
        }
    }
    printf("Test10: Executed Successfully\n\n");
    return errors;
}

static int test_sdio_neighbor_sector_preservation(void) {
    int errors = 0;
    uint8_t sec0_before[512], sec1_before[512], sec2_before[512];
    uint8_t sec0_after[512],  sec1_after[512],  sec2_after[512];
    uint8_t pattern[512];
    uint32_t s0 = SDIO_NEIGHBOR_BASE;
    uint32_t s1 = SDIO_NEIGHBOR_BASE + 1u;
    uint32_t s2 = SDIO_NEIGHBOR_BASE + 2u;
    printf("Test11: SDIO neighbour sector preservation\n");
    if (sdio_read(SDIO_DRV, s0, 1, (char *)sec0_before) != 0) return 1;
    if (sdio_read(SDIO_DRV, s1, 1, (char *)sec1_before) != 0) return 1;
    if (sdio_read(SDIO_DRV, s2, 1, (char *)sec2_before) != 0) return 1;
    fill_pattern(pattern, sizeof(pattern), 0x55u);
    if (sdio_write(SDIO_DRV, s1, 1, (char *)pattern) != 0) {
        printf("ERROR: write to target neighbour sector failed\n"); return 1;
    }
    if (sdio_read(SDIO_DRV, s0, 1, (char *)sec0_after) != 0) return 1;
    if (sdio_read(SDIO_DRV, s1, 1, (char *)sec1_after) != 0) return 1;
    if (sdio_read(SDIO_DRV, s2, 1, (char *)sec2_after) != 0) return 1;
    if (compare_buffers(sec0_before, sec0_after, 512) != 0) {
        printf("ERROR: previous sector corrupted\n"); errors++;
    }
    if (compare_buffers(pattern, sec1_after, 512) != 0) {
        printf("ERROR: target sector readback mismatch\n"); errors++;
    }
    if (compare_buffers(sec2_before, sec2_after, 512) != 0) {
        printf("ERROR: next sector corrupted\n"); errors++;
    }
    if (sdio_write(SDIO_DRV, s1, 1, (char *)sec1_before) != 0)
        printf("WARNING: failed to restore original target sector\n");
    printf("TC11 executed successfully\n\n");
    return errors;
}

static int test_sdio_sector_zero_preservation(void) {
    int errors = 0;
    uint8_t before[512], after[512], pattern[512];
    printf("Test12: SDIO sector 0 preservation\n");
    if (sdio_read(SDIO_DRV, SDIO_SECTOR_ZERO, 1, (char *)before) != 0) {
        printf("ERROR: failed to read sector 0 before test\n"); return 1;
    }
    fill_pattern(pattern, sizeof(pattern), 0x33u);
    for (uint32_t s = 0x1001u; s <= 0x1017u; s++) {
        if (sdio_write(SDIO_DRV, s, 1, (char *)pattern) != 0) {
            printf("ERROR: write failed at sector 0x%08x\n", (unsigned)s); errors++;
        }
    }
    if (sdio_read(SDIO_DRV, SDIO_SECTOR_ZERO, 1, (char *)after) != 0) {
        printf("ERROR: failed to read sector 0 after test\n"); return 1;
    }
    if (compare_buffers(before, after, sizeof(before)) != 0) {
        printf("ERROR: sector 0 corrupted by scratch writes\n"); errors++;
    }
    printf("Test12 executed successfully\n\n");
    return errors;
}

static int test_sdio_recovery_after_invalid_request(void) {
    int errors = 0;
    uint8_t buf[512], tx[512], rx[512];
    printf("Test13: SDIO recovery after invalid request\n");
    if (sdio_read(SDIO_DRV, SDIO_RECOVERY_SECTOR, 0, (char *)buf) != 0)
        printf("WARNING: zero-count read returned non-zero\n");
    fill_pattern(tx, sizeof(tx), 0xC0u);
    memset(rx, 0, sizeof(rx));
    if (sdio_write(SDIO_DRV, SDIO_RECOVERY_SECTOR, 1, (char *)tx) != 0) {
        printf("ERROR: valid write failed after invalid request\n"); return 1;
    }
    if (sdio_read(SDIO_DRV, SDIO_RECOVERY_SECTOR, 1, (char *)rx) != 0) {
        printf("ERROR: valid read failed after invalid request\n"); return 1;
    }
    if (compare_buffers(tx, rx, sizeof(tx)) != 0) {
        printf("ERROR: recovery readback mismatch\n"); errors++;
    }
    printf("TC13 executed successfully\n\n");
    return errors;
}

static int test_sdio_scratch_range_integrity(void) {
    int errors = 0;
    uint8_t tx[512], rx[512];
    printf("Test14: SDIO scratch range integrity sweep\n");
    for (uint32_t i = 0; i < 16u; i++) {
        uint32_t sector = 0x1040u + i;
        fill_pattern(tx, sizeof(tx), 0x20u + i);
        memset(rx, 0, sizeof(rx));
        if (sdio_write(SDIO_DRV, sector, 1, (char *)tx) != 0) {
            printf("ERROR: write failed at sector 0x%08x\n", (unsigned)sector); errors++; continue;
        }
        if (sdio_read(SDIO_DRV, sector, 1, (char *)rx) != 0) {
            printf("ERROR: read failed at sector 0x%08x\n", (unsigned)sector); errors++; continue;
        }
        if (compare_buffers(tx, rx, sizeof(tx)) != 0) {
            printf("ERROR: mismatch at sector 0x%08x\n", (unsigned)sector); errors++;
        }
    }
    printf("TC14 executed successfully\n\n");
    return errors;
}

int main(void)
{
    int errors = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("Step1: mapping SDIO registers at 0x%081x...\n", (unsigned long)SYS_SDIO_BASE);
    SDIO_S = (SDIO *)pynq_map_sdio();
    printf("Step2: mmap ok, calling sdio_init\n");
    UartStdOutInit();
    printf("PYNQ-Z2 SDIO Integration Tests (SDIO base = 0x%08lx)\n",
           (unsigned long)SYS_SDIO_BASE);

    errors += test_sdio_init_success();
    if (errors != 0) {
        printf("Aborting after SDIO init failure\n");
        TEST_FAIL();
        return 1;
    }

    errors += test_sdio_card_info_valid();
    if (errors != 0) {
        printf("Aborting after SDIO card-info validation failure\n");
        TEST_FAIL();
        return 1;
    }

    errors += test_sdio_single_block_read();
    errors += test_sdio_read_repeatability();
    errors += test_sdio_single_block_write_readback();
    errors += test_sdio_multi_block_write_readback();
    errors += test_sdio_invalid_args();
    errors += test_sdio_stress_basic();
    errors += test_sdio_multisector_boundary_transition();
    errors += test_sdio_adversarial_patterns();
    errors += test_sdio_neighbor_sector_preservation();
    errors += test_sdio_sector_zero_preservation();
    errors += test_sdio_recovery_after_invalid_request();
    errors += test_sdio_scratch_range_integrity();

    printf("Total SDIO errors = %d\n", errors);

    if (errors == 0)
        TEST_PASS();
    else
        TEST_FAIL();

    return errors;
}
