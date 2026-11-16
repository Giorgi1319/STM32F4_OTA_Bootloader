#ifndef INC_FLASH_LAYOUT_H_
#define INC_FLASH_LAYOUT_H_

/*
 * STM32F446RE flash map (512 KB)
 *
 *   Sector 0-1   0x08000000    32 KB   bootloader
 *   Sector 2     0x08008000    16 KB   application header (app_header_t)
 *   Sector 3-7   0x0800C000   464 KB   application
 *
 * STM32F446_Application must match: FLASH ORIGIN/LENGTH in its linker script
 * and VECT_TAB_OFFSET in its CMakeLists.txt.
 */

#define BL_START_ADDR       0x08000000u

#define APP_HEADER_ADDR     0x08008000u
#define APP_HEADER_SECTOR   FLASH_SECTOR_2

#define APP_START_ADDR      0x0800C000u
#define APP_END_ADDR        0x08080000u                       /* end of flash, exclusive */
#define APP_START_SECTOR    FLASH_SECTOR_3
#define APP_END_SECTOR      FLASH_SECTOR_7
#define APP_MAX_SIZE        (APP_END_ADDR - APP_START_ADDR)   /* 0x74000 = 464 KB */

#define RAM_START_ADDR      0x20000000u
#define RAM_END_ADDR        0x20020000u                       /* 128 KB, exclusive */

#endif /* INC_FLASH_LAYOUT_H_ */
