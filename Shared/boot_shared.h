/*
 * boot_shared.h
 *
 * Shared between STM32F446_Bootloader and STM32F446_Application.
 * Both CMake projects add ../Shared to their include path.
 *
 * The application asks the bootloader for an OTA update by writing
 * BOOT_REQUEST_OTA into the first word of the backup SRAM and resetting.
 * Backup SRAM keeps its contents across a software reset (and across power
 * cycles when VBAT is powered), and neither program's startup code touches it.
 * The bootloader reads the word once at boot and clears it, so a request is
 * consumed even if the update then times out.
 */

#ifndef BOOT_SHARED_H_
#define BOOT_SHARED_H_

#include "stm32f4xx_hal.h"

#define BOOT_REQUEST_OTA   0x4F544121u   /* "OTA!" */

/* Enable access to the backup SRAM and return the request word. */
static inline volatile uint32_t *boot_request_word(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();          /* allow writes to the backup domain */
    __HAL_RCC_BKPSRAM_CLK_ENABLE();
    return (volatile uint32_t *)BKPSRAM_BASE;
}

/* Application side: request OTA and reset into the bootloader. Does not return. */
static inline void boot_request_ota_and_reset(void)
{
    *boot_request_word() = BOOT_REQUEST_OTA;
    __DSB();
    NVIC_SystemReset();
}

/* Bootloader side: returns 1 if the application requested OTA, and clears the request. */
static inline int boot_take_ota_request(void)
{
    volatile uint32_t *req = boot_request_word();
    int requested = (*req == BOOT_REQUEST_OTA);
    *req = 0;
    return requested;
}

#endif /* BOOT_SHARED_H_ */
