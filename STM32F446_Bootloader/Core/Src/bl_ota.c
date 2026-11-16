#include "main.h"
#include "bl_ota.h"
#include "flash_layout.h"
#include "flash_operations.h"
#include "app_header.h"
#include "crc32.h"
#include <stddef.h>
#include <string.h>

/* Invalidate the old application: erase its header first, then its sectors. */
static int bl_flash_erase_app(void)
{
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = flash_erase_header();
    if (st == HAL_OK)
        st = flash_erase_app();
    HAL_FLASH_Lock();
    return (st == HAL_OK) ? 0 : -1;
}

/* Write a fresh header. The magic word goes in last, so a header cut short
   by a power loss is never taken as valid. */
static int bl_flash_write_app_header(uint32_t size, uint32_t crc, uint32_t version)
{
    const app_header_t hdr = {
        .reserved = 0xFFFFFFFFu,
        .magic    = APP_MAGIC,
        .size     = size,
        .crc      = crc,
        .version  = version,
    };
    const uint32_t *words = (const uint32_t *)&hdr;
    const uint32_t magic_index = offsetof(app_header_t, magic) / 4;
    HAL_StatusTypeDef st;

    HAL_FLASH_Unlock();
    st = flash_erase_header();

    for (uint32_t i = 0; st == HAL_OK && i < sizeof(hdr) / 4; i++)
    {
        if (i != magic_index && words[i] != 0xFFFFFFFFu)
            st = flash_write_word(APP_HEADER_ADDR + 4 * i, words[i]);
    }
    if (st == HAL_OK)
        st = flash_write_word(APP_HEADER_ADDR + 4 * magic_index, words[magic_index]);

    HAL_FLASH_Lock();
    return (st == HAL_OK) ? 0 : -1;
}

static int bl_ota_begin(bl_ota_ctx_t *ctx, uint32_t image_size, uint32_t expected_crc)
{
    ctx->write_addr     = APP_START_ADDR;
    ctx->total_received = 0;
    ctx->image_size     = image_size;
    ctx->expected_crc   = expected_crc;
    ctx->running_crc    = CRC32_INIT;
    return bl_flash_erase_app();
}

static int bl_ota_write_chunk(bl_ota_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    if ((ctx->total_received + len) > ctx->image_size)
        return -1;

    /* CRC over the real data bytes only */
    ctx->running_crc = crc32_update(ctx->running_crc, data, len);

    HAL_StatusTypeDef st = HAL_OK;
    HAL_FLASH_Unlock();
    for (uint32_t i = 0; st == HAL_OK && i < len; i += 4)
    {
        uint32_t word  = 0xFFFFFFFFu;                    /* a short last word is padded with erased bytes */
        uint32_t bytes = (len - i >= 4) ? 4 : (len - i);
        memcpy(&word, &data[i], bytes);
        st = flash_write_word(ctx->write_addr, word);
        ctx->write_addr += 4;
    }
    HAL_FLASH_Lock();

    ctx->total_received += len;
    return (st == HAL_OK) ? 0 : -1;
}

static int bl_ota_finalize(bl_ota_ctx_t *ctx, const ota_image_hdr_t *hdr)
{
    if (ctx->total_received != ctx->image_size ||
        crc32_final(ctx->running_crc) != ctx->expected_crc)
        return BL_OTA_ERR_CRC;

    /* Check what actually ended up in flash, not just what was received */
    if (crc32((const uint8_t *)APP_START_ADDR, ctx->image_size) != ctx->expected_crc)
        return BL_OTA_ERR_VERIFY;

    if (bl_flash_write_app_header(ctx->image_size, ctx->expected_crc, hdr->version) != 0)
        return BL_OTA_ERR_HEADER;

    return BL_OTA_OK;
}

int bl_ota_run(bl_ota_ctx_t *ctx, ota_stream_t *stream)
{
    ota_image_hdr_t hdr;
    uint8_t buf[BL_OTA_CHUNK_SIZE];

    if (stream->read((uint8_t *)&hdr, sizeof(hdr)) != 0)
        return BL_OTA_ERR_RX_HEADER;

    if (hdr.magic != APP_MAGIC)
        return BL_OTA_ERR_MAGIC;

    if (hdr.image_size == 0 || hdr.image_size > APP_MAX_SIZE)
        return BL_OTA_ERR_SIZE;

    if (stream->set_total_size)
        stream->set_total_size(sizeof(ota_image_hdr_t) + hdr.image_size);

    /* The current application is erased here. If anything fails after this point
       there is no valid app, and main.c stays in recovery mode until an update succeeds. */
    if (bl_ota_begin(ctx, hdr.image_size, hdr.crc) != 0)
        return BL_OTA_ERR_ERASE;

    for (uint32_t remaining = hdr.image_size; remaining > 0; )
    {
        uint32_t len = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;

        if (stream->read(buf, len) != 0)
            return BL_OTA_ERR_RX_DATA;

        if (bl_ota_write_chunk(ctx, buf, len) != 0)
            return BL_OTA_ERR_WRITE;

        remaining -= len;
    }

    return bl_ota_finalize(ctx, &hdr);
}
