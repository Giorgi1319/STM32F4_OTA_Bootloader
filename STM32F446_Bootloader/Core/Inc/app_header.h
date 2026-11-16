#ifndef INC_APP_HEADER_H_
#define INC_APP_HEADER_H_

#include <stdint.h>
#include "flash_layout.h"

#define APP_MAGIC   0xDEADBEEFu

/*
 * Stored at APP_HEADER_ADDR. Written by the bootloader after a verified OTA,
 * or by hand with the file from tools/ota_upload.py --header-out.
 */
typedef struct {
    uint32_t reserved;   /* was ota_flag; OTA requests now go through backup SRAM (boot_shared.h) */
    uint32_t magic;      /* APP_MAGIC, written last */
    uint32_t size;       /* application image size in bytes */
    uint32_t crc;        /* CRC-32 of the application image */
    uint32_t version;
} app_header_t;

#endif /* INC_APP_HEADER_H_ */
