/*
 * PYNQ-Z2 SDIO FatFs integration test.
 *
 * This program runs FatFs directly on top of sdiodrv.c from Linux userspace.
 * It provides the FatFs disk_* hooks here because the repository diskio.c is
 * intended for bare-metal board symbols, while PYNQ maps the SDIO registers at
 * runtime through /dev/mem.
 *
 * Default mode is intentionally non-formatting: it mounts the existing FAT
 * filesystem and creates files under 0:/SDIOTEST.  Use --mkfs only on a card
 * that may be erased, and compile ffconf.h with FF_USE_MKFS set to 1.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"
#include "sdiodrv.h"
#include "pynq_bsp.h"

#define TEST_DIR            "0:/SDIOTEST"
#define SMALL_FILE          TEST_DIR "/SML.TXT"
#define LARGE_FILE          TEST_DIR "/LARGE.BIN"
#define DEFAULT_LARGE_MB    64u
#define DEFAULT_SMALL_FILES 128u
#define IO_CHUNK_BYTES      (32u * 1024u)

static SDIODRV *g_sdio_drv;
static SDIO *g_sdio_regs;
static BYTE g_io_buf[IO_CHUNK_BYTES];
static BYTE g_cmp_buf[IO_CHUNK_BYTES];

static uint32_t fnv1a_update(uint32_t hash, const BYTE *buf, UINT len)
{
    for (UINT i = 0; i < len; i++) {
        hash ^= buf[i];
        hash *= 16777619u;
    }
    return hash;
}

static void fill_pattern(BYTE *buf, UINT len, uint32_t seed)
{
    uint32_t x = seed ^ 0x9e3779b9u;

    for (UINT i = 0; i < len; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        buf[i] = (BYTE)(x + i + (seed >> 8));
    }
}

static int check_fr(const char *what, FRESULT fr)
{
    if (fr == FR_OK)
        return 0;

    printf("ERROR: %s failed, FR=%d\n", what, (int)fr);
	return 1;
}

static void dump_sector0_hint(void)
{
	BYTE sec[512];
	DRESULT dr;

	printf("\nSector-0 diagnostic after mount failure\n");
	dr = disk_read(0, sec, 0, 1);
	if (dr != RES_OK) {
		printf("  ERROR: sector 0 read failed, DRESULT=%d\n", (int)dr);
		return;
	}

	printf("  MBR signature[0x1fe:0x1ff] = %02x %02x\n",
		sec[0x1fe], sec[0x1ff]);
	printf("  FAT label guess  [0x36..0x3d] = '%c%c%c%c%c%c%c%c'\n",
		sec[0x36], sec[0x37], sec[0x38], sec[0x39],
		sec[0x3a], sec[0x3b], sec[0x3c], sec[0x3d]);
	printf("  FAT32 label guess[0x52..0x59] = '%c%c%c%c%c%c%c%c'\n",
		sec[0x52], sec[0x53], sec[0x54], sec[0x55],
		sec[0x56], sec[0x57], sec[0x58], sec[0x59]);
	printf("  exFAT label guess[0x03..0x0a] = '%c%c%c%c%c%c%c%c'\n",
		sec[0x03], sec[0x04], sec[0x05], sec[0x06],
		sec[0x07], sec[0x08], sec[0x09], sec[0x0a]);

	printf("  First 64 bytes:");
	for (unsigned i = 0; i < 64; i++) {
		if ((i & 15u) == 0)
			printf("\n    %02x:", i);
		printf(" %02x", sec[i]);
	}
	printf("\n");

	printf("  Partition entries:\n");
	for (unsigned p = 0; p < 4; p++) {
		unsigned off = 0x1beu + p * 16u;
		uint32_t lba = (uint32_t)sec[off + 8]
			| ((uint32_t)sec[off + 9] << 8)
			| ((uint32_t)sec[off + 10] << 16)
			| ((uint32_t)sec[off + 11] << 24);
		uint32_t cnt = (uint32_t)sec[off + 12]
			| ((uint32_t)sec[off + 13] << 8)
			| ((uint32_t)sec[off + 14] << 16)
			| ((uint32_t)sec[off + 15] << 24);
		printf("    %u: boot=0x%02x type=0x%02x lba=%u count=%u\n",
			p, sec[off], sec[off + 4], lba, cnt);
	}
	printf("\n");
}

static int ensure_test_dir(void)
{
    FRESULT fr = f_mkdir(TEST_DIR);

    if (fr == FR_OK || fr == FR_EXIST)
        return 0;

    printf("ERROR: mkdir %s failed, FR=%d\n", TEST_DIR, (int)fr);
    return 1;
}

static int remove_if_exists(const char *path)
{
    FRESULT fr = f_unlink(path);

    if (fr == FR_OK || fr == FR_NO_FILE || fr == FR_NO_PATH)
        return 0;

    printf("ERROR: unlink %s failed, FR=%d\n", path, (int)fr);
    return 1;
}

static void small_file_name(char *path, unsigned idx, char prefix)
{
    snprintf(path, 32, TEST_DIR "/%c%03u.TXT", prefix, idx % 1000u);
}

static int test_small_file(void)
{
    static const char payload[] =
        "PYNQ SDIO FatFs small file test\n"
        "clock=50MHz phase=24 width=4bit\n";
    FIL fp;
    UINT done;
    char readback[sizeof(payload)];
    FRESULT fr;

    printf("Test FS1: small file write/read\n");
    if (remove_if_exists(SMALL_FILE))
        return 1;

    fr = f_open(&fp, SMALL_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    if (check_fr("open small write", fr))
        return 1;
    fr = f_write(&fp, payload, (UINT)sizeof(payload), &done);
    if (check_fr("write small", fr) || done != sizeof(payload)) {
        f_close(&fp);
        return 1;
    }
    fr = f_close(&fp);
    if (check_fr("close small write", fr))
        return 1;

    memset(readback, 0, sizeof(readback));
    fr = f_open(&fp, SMALL_FILE, FA_READ);
    if (check_fr("open small read", fr))
        return 1;
    fr = f_read(&fp, readback, (UINT)sizeof(readback), &done);
    if (check_fr("read small", fr) || done != sizeof(readback)) {
        f_close(&fp);
        return 1;
    }
    f_close(&fp);

    if (memcmp(payload, readback, sizeof(payload)) != 0) {
        printf("ERROR: small file data mismatch\n");
        return 1;
    }

    printf("PASS FS1\n\n");
    return 0;
}

static int write_large_file(unsigned large_mb, uint32_t *out_hash)
{
    FIL fp;
    FRESULT fr;
    UINT done;
    uint64_t total = (uint64_t)large_mb * 1024u * 1024u;
    uint64_t offset = 0;
    uint32_t hash = 2166136261u;

    printf("Test FS2: large file write %u MB\n", large_mb);
    if (remove_if_exists(LARGE_FILE))
        return 1;

    fr = f_open(&fp, LARGE_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    if (check_fr("open large write", fr))
        return 1;

    while (offset < total) {
        UINT chunk = (UINT)((total - offset) > IO_CHUNK_BYTES
            ? IO_CHUNK_BYTES : (total - offset));

        fill_pattern(g_io_buf, chunk, (uint32_t)(offset >> 9));
        hash = fnv1a_update(hash, g_io_buf, chunk);

        fr = f_write(&fp, g_io_buf, chunk, &done);
        if (check_fr("write large", fr) || done != chunk) {
            printf("ERROR: large write short count at offset %llu\n",
                (unsigned long long)offset);
            f_close(&fp);
            return 1;
        }

        offset += chunk;
        if ((offset % (8u * 1024u * 1024u)) == 0)
            printf("  wrote %llu MB\n", (unsigned long long)(offset >> 20));
    }

    fr = f_sync(&fp);
    if (check_fr("sync large", fr)) {
        f_close(&fp);
        return 1;
    }

    fr = f_close(&fp);
    if (check_fr("close large write", fr))
        return 1;

    *out_hash = hash;
    printf("PASS FS2 write hash=0x%08x\n\n", hash);
    return 0;
}

static int verify_large_file(unsigned large_mb, uint32_t expected_hash)
{
    FIL fp;
    FRESULT fr;
    UINT done;
    uint64_t total = (uint64_t)large_mb * 1024u * 1024u;
    uint64_t offset = 0;
    uint32_t hash = 2166136261u;

    printf("Test FS3: large file read/verify %u MB\n", large_mb);
    fr = f_open(&fp, LARGE_FILE, FA_READ);
    if (check_fr("open large read", fr))
        return 1;

    if (f_size(&fp) != total) {
        printf("ERROR: large file size mismatch: got %llu expected %llu\n",
            (unsigned long long)f_size(&fp), (unsigned long long)total);
        f_close(&fp);
        return 1;
    }

    while (offset < total) {
        UINT chunk = (UINT)((total - offset) > IO_CHUNK_BYTES
            ? IO_CHUNK_BYTES : (total - offset));

        fr = f_read(&fp, g_io_buf, chunk, &done);
        if (check_fr("read large", fr) || done != chunk) {
            printf("ERROR: large read short count at offset %llu\n",
                (unsigned long long)offset);
            f_close(&fp);
            return 1;
        }

        fill_pattern(g_cmp_buf, chunk, (uint32_t)(offset >> 9));
        if (memcmp(g_io_buf, g_cmp_buf, chunk) != 0) {
            printf("ERROR: large file data mismatch at offset %llu\n",
                (unsigned long long)offset);
            f_close(&fp);
            return 1;
        }

        hash = fnv1a_update(hash, g_io_buf, chunk);
        offset += chunk;
        if ((offset % (8u * 1024u * 1024u)) == 0)
            printf("  verified %llu MB\n", (unsigned long long)(offset >> 20));
    }

    f_close(&fp);
    if (hash != expected_hash) {
        printf("ERROR: large file hash mismatch got=0x%08x expected=0x%08x\n",
            hash, expected_hash);
        return 1;
    }

    printf("PASS FS3 read hash=0x%08x\n\n", hash);
    return 0;
}

static int test_many_small_files(unsigned count)
{
    FIL fp;
    FRESULT fr;
    UINT done;
    char path[32], renamed[32];

    printf("Test FS4: many small files count=%u\n", count);

    for (unsigned i = 0; i < count; i++) {
        small_file_name(path, i, 'F');
        remove_if_exists(path);

        fr = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
        if (check_fr("open many write", fr))
            return 1;
        fill_pattern(g_io_buf, 512, 0x50000000u + i);
        fr = f_write(&fp, g_io_buf, 512, &done);
        if (check_fr("write many", fr) || done != 512) {
            f_close(&fp);
            return 1;
        }
        fr = f_close(&fp);
        if (check_fr("close many write", fr))
            return 1;
    }

    for (unsigned i = 0; i < count; i++) {
        small_file_name(path, i, 'F');
        fr = f_open(&fp, path, FA_READ);
        if (check_fr("open many read", fr))
            return 1;
        fr = f_read(&fp, g_io_buf, 512, &done);
        if (check_fr("read many", fr) || done != 512) {
            f_close(&fp);
            return 1;
        }
        fill_pattern(g_cmp_buf, 512, 0x50000000u + i);
        if (memcmp(g_io_buf, g_cmp_buf, 512) != 0) {
            printf("ERROR: many-file mismatch at index %u\n", i);
            f_close(&fp);
            return 1;
        }
        f_close(&fp);
    }

    for (unsigned i = 0; i < count && i < 16u; i++) {
        small_file_name(path, i, 'F');
        small_file_name(renamed, i, 'R');
        remove_if_exists(renamed);
        fr = f_rename(path, renamed);
        if (check_fr("rename many", fr))
            return 1;
    }

    for (unsigned i = 16u; i < count; i += 2u) {
        small_file_name(path, i, 'F');
        if (remove_if_exists(path))
            return 1;
    }

    printf("PASS FS4\n\n");
    return 0;
}

static int remount_and_verify(FATFS *fs, unsigned large_mb, uint32_t large_hash)
{
    FRESULT fr;

    printf("Test FS5: unmount/remount and verify\n");
    fr = f_mount(NULL, "0:", 0);
    if (check_fr("unmount", fr))
        return 1;

    fr = f_mount(fs, "0:", 1);
    if (check_fr("remount", fr))
        return 1;

    if (verify_large_file(large_mb, large_hash))
        return 1;

    printf("PASS FS5\n\n");
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0)
        return STA_NODISK;

    if (g_sdio_drv != NULL)
        return 0;

    g_sdio_regs = (SDIO *)pynq_map_sdio();
    g_sdio_drv = sdio_init(g_sdio_regs);
    return (g_sdio_drv == NULL) ? STA_NOINIT : 0;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0)
        return STA_NODISK;
    return (g_sdio_drv == NULL) ? STA_NOINIT : 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || g_sdio_drv == NULL || count == 0)
        return RES_PARERR;
    return (sdio_read(g_sdio_drv, (unsigned)sector, count, (char *)buff) == RES_OK)
        ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || g_sdio_drv == NULL || count == 0)
        return RES_PARERR;
    return (sdio_write(g_sdio_drv, (unsigned)sector, count, (const char *)buff) == RES_OK)
        ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0 || g_sdio_drv == NULL)
        return RES_PARERR;
    return (DRESULT)sdio_ioctl(g_sdio_drv, (char)cmd, (char *)buff);
}

DWORD get_fattime(void)
{
    return ((DWORD)(2026u - 1980u) << 25)
        | ((DWORD)5u << 21)
        | ((DWORD)22u << 16)
        | ((DWORD)12u << 11);
}

static void usage(const char *prog)
{
    printf("Usage: %s [--large-mb N] [--small-files N] [--mkfs]\n", prog);
    printf("Default: mount existing FS, write/read %u MB, create %u small files.\n",
        DEFAULT_LARGE_MB, DEFAULT_SMALL_FILES);
    printf("WARNING: --mkfs erases/reformats the SD card and requires FF_USE_MKFS=1.\n");
}

int main(int argc, char **argv)
{
    FATFS fs;
    FRESULT fr;
    unsigned large_mb = DEFAULT_LARGE_MB;
    unsigned small_files = DEFAULT_SMALL_FILES;
    int request_mkfs = 0;
    uint32_t large_hash = 0;
    int errors = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--large-mb") == 0 && i + 1 < argc) {
            large_mb = (unsigned)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--small-files") == 0 && i + 1 < argc) {
            small_files = (unsigned)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--mkfs") == 0) {
            request_mkfs = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    printf("PYNQ-Z2 SDIO FatFs filesystem test\n");
    printf("large_mb=%u small_files=%u mkfs=%s\n",
        large_mb, small_files, request_mkfs ? "yes" : "no");

    fr = f_mount(&fs, "0:", 1);
	if (fr != FR_OK) {
		printf("Initial mount failed, FR=%d\n", (int)fr);
		dump_sector0_hint();
		if (!request_mkfs) {
			printf("Use a FAT-formatted card, or rerun with --mkfs after enabling FF_USE_MKFS=1.\n");
			return 1;
        }
    }

    if (request_mkfs) {
#if FF_USE_MKFS
        BYTE work[FF_MAX_SS];
        MKFS_PARM opt = { FM_FAT32, 0, 0, 0, 0 };

        printf("Formatting card with FatFs f_mkfs(). Existing data will be erased.\n");
        f_mount(NULL, "0:", 0);
        fr = f_mkfs("0:", &opt, work, sizeof(work));
        if (check_fr("mkfs", fr))
            return 1;
        fr = f_mount(&fs, "0:", 1);
        if (check_fr("mount after mkfs", fr))
            return 1;
#else
        printf("ERROR: --mkfs requested, but ffconf.h has FF_USE_MKFS=0.\n");
        printf("Set FF_USE_MKFS to 1 for a formatting build.\n");
        return 1;
#endif
    }

    errors += ensure_test_dir();
    if (!errors)
        errors += test_small_file();
    if (!errors)
        errors += write_large_file(large_mb, &large_hash);
    if (!errors)
        errors += verify_large_file(large_mb, large_hash);
    if (!errors)
        errors += test_many_small_files(small_files);
    if (!errors)
        errors += remount_and_verify(&fs, large_mb, large_hash);

    f_mount(NULL, "0:", 0);

    printf("Total FatFs errors = %d\n", errors);
    if (errors == 0) {
        printf("========================================\n");
        printf("        FATFS TESTS PASSED\n");
        return 0;
    }

    printf("========================================\n");
    printf("        FATFS TESTS FAILED\n");
    return 1;
}
