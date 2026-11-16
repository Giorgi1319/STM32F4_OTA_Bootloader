#include "main.h"
#include "flash_operations.h"
#include "flash_layout.h"

/* Error flags left over from an earlier operation (or a debugger session)
   make the next erase or program fail, so clear them first. */
static void flash_clear_errors(void)
{
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
#if defined(FLASH_FLAG_RDERR)
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_RDERR);
#endif
}

static HAL_StatusTypeDef flash_erase_sectors(uint32_t first, uint32_t count)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0;

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;   /* 2.7-3.6 V, 32-bit parallelism */
    erase.Sector       = first;
    erase.NbSectors    = count;

    flash_clear_errors();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &sector_error);
    if (st == HAL_OK && sector_error != 0xFFFFFFFFu)   /* 0xFFFFFFFF means every sector erased */
        st = HAL_ERROR;
    return st;
}

HAL_StatusTypeDef flash_erase_app(void)
{
    return flash_erase_sectors(APP_START_SECTOR, APP_END_SECTOR - APP_START_SECTOR + 1);
}

HAL_StatusTypeDef flash_erase_header(void)
{
    return flash_erase_sectors(APP_HEADER_SECTOR, 1);
}

HAL_StatusTypeDef flash_write_word(uint32_t addr, uint32_t data)
{
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, data);
}
