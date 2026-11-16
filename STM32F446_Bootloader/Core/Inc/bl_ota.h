#ifndef INC_BL_OTA_H_
#define INC_BL_OTA_H_

#include <stdint.h>

/* Payload bytes per ACK. tools/ota_upload.py uses the same value. */
#define BL_OTA_CHUNK_SIZE      256u

/* bl_ota_run() results, printed by main.c as "OTA failed, err=N" */
#define BL_OTA_OK               0
#define BL_OTA_ERR_RX_HEADER   -1   /* no header from the host before the timeout */
#define BL_OTA_ERR_MAGIC       -2   /* header magic is not APP_MAGIC */
#define BL_OTA_ERR_SIZE        -3   /* image size is 0 or larger than APP_MAX_SIZE */
#define BL_OTA_ERR_ERASE       -4   /* erasing the header or application sectors failed */
#define BL_OTA_ERR_RX_DATA     -5   /* a data chunk did not arrive before the timeout */
#define BL_OTA_ERR_WRITE       -6   /* programming flash failed */
#define BL_OTA_ERR_CRC         -7   /* CRC of the received data does not match the header */
#define BL_OTA_ERR_VERIFY      -8   /* CRC read back from flash does not match the header */
#define BL_OTA_ERR_HEADER      -9   /* writing the application header failed */

typedef struct
{
    uint32_t write_addr;       /* Current flash write address */
    uint32_t total_received;   /* Bytes written so far */
    uint32_t image_size;       /* Expected firmware size */
    uint32_t expected_crc;     /* Expected final CRC */
    uint32_t running_crc;      /* CRC accumulator */
} bl_ota_ctx_t;

/* First 16 bytes the host sends, little-endian */
typedef struct
{
    uint32_t magic;        /* APP_MAGIC */
    uint32_t image_size;   /* Application binary size only */
    uint32_t crc;          /* CRC-32 of the application binary only */
    uint32_t version;      /* Firmware version */
} ota_image_hdr_t;

typedef struct
{
    int  (*read)(uint8_t *buf, uint32_t len);   /* returns 0 once exactly len bytes arrived */
    void (*set_total_size)(uint32_t total_size);
} ota_stream_t;

int bl_ota_run(bl_ota_ctx_t *ctx, ota_stream_t *stream);

#endif /* INC_BL_OTA_H_ */
