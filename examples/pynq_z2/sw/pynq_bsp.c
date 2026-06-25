/*
 * pynq_bsp.c — /dev/mem mmap for PYNQ-Z2 SDIO register access
 */
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include "pynq_bsp.h"

#define MAP_SIZE 4096UL

void *pynq_map_sdio(void)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        exit(1);
    }

    void *base = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, SYS_SDIO_BASE);
    if (base == MAP_FAILED) {
        perror("mmap SDIO");
        exit(1);
    }

    return base;
}
