/*
 * sdiodrv.h — local copy for PYNQ-Z2 Linux build.
 *
 * OPT_SDIODMA is intentionally NOT defined here.  The hardware DMA path in
 * sdiodrv.c writes the caller's buffer pointer directly to sd_dma_addr; on
 * Linux the buffer address is virtual, but the FPGA DMA engine needs a
 * physical bus address.  Omitting OPT_SDIODMA makes the driver use the PIO
 * FIFO path (sd_fifa/sd_fifb) instead, which is safe in userspace.
 */
#ifndef SDIODRV_H
#define SDIODRV_H
#include <stdint.h>

typedef struct SDIO_S {
    volatile uint32_t sd_cmd, sd_data, sd_fifa, sd_fifb, sd_phy;
    volatile uint32_t sd_unused;
    volatile void    *sd_dma_addr;
    volatile uint32_t sd_dma_length;
} SDIO;

typedef struct SDIODRV_S {
    SDIO    *d_dev;
    uint32_t d_CID[4], d_OCR;
    char     d_SCR[8], d_CSD[16];
    uint16_t d_RCA;
    uint32_t d_sector_count, d_block_size;
} SDIODRV;

extern struct SDIODRV_S *sdio_init(SDIO *dev);
extern int sdio_write(struct SDIODRV_S *dev, const unsigned sector,
                      const unsigned count, const char *buf);
extern int sdio_read(struct SDIODRV_S *dev, const unsigned sector,
                     const unsigned count, char *buf);
extern int sdio_ioctl(struct SDIODRV_S *dev, char cmd, char *buf);
#endif
