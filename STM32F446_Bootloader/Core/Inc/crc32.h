#ifndef INC_CRC32_H_
#define INC_CRC32_H_

#include <stdint.h>

/* Standard CRC-32 (IEEE 802.3), the same value as Python's zlib.crc32(). */

#define CRC32_INIT  0xFFFFFFFFu

/* Feed bytes into a running CRC that started at CRC32_INIT. */
uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length);

/* Turn a running CRC into the final value. */
static inline uint32_t crc32_final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

/* CRC of a whole buffer. */
uint32_t crc32(const uint8_t *data, uint32_t length);

#endif /* INC_CRC32_H_ */
