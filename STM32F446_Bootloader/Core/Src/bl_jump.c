#include "main.h"
#include "bl_jump.h"
#include "flash_layout.h"
#include "app_header.h"
#include "crc32.h"

typedef void (*pFunction)(void);

int bootloader_is_app_valid(void)
{
    const app_header_t *hdr = (const app_header_t *)APP_HEADER_ADDR;

    if (hdr->magic != APP_MAGIC)
        return APP_ERR_MAGIC;

    if (hdr->size < 8 || hdr->size > APP_MAX_SIZE)   /* at least the stack pointer and reset vector */
        return APP_ERR_SIZE;

    uint32_t stack = *(const volatile uint32_t *)APP_START_ADDR;
    if (stack <= RAM_START_ADDR || stack > RAM_END_ADDR)
        return APP_ERR_STACK;

    uint32_t reset = *(const volatile uint32_t *)(APP_START_ADDR + 4);
    if ((reset & 1u) == 0 ||                          /* must be a Thumb address */
        reset < APP_START_ADDR || reset >= APP_START_ADDR + hdr->size)
        return APP_ERR_RESET;

    if (crc32((const uint8_t *)APP_START_ADDR, hdr->size) != hdr->crc)
        return APP_ERR_CRC;

    return APP_OK;
}

void JumpToApplication(void)
{
    uint32_t  appStack = *(const volatile uint32_t *)APP_START_ADDR;                  /* entry 0: stack pointer */
    pFunction appEntry = (pFunction)*(const volatile uint32_t *)(APP_START_ADDR + 4); /* entry 1: Reset_Handler */

    /* Put every peripheral the bootloader used (UART, GPIO, ...) back to its reset state */
    HAL_DeInit();

    __disable_irq();

    /* Stop SysTick and drop a tick that may already be pending */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* Disable and clear every NVIC interrupt */
    for (uint32_t i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }

    /* Use the application's vector table. The application also sets this in its own
       SystemInit() (USER_VECT_TAB_ADDRESS); doing it here covers a build without that. */
    SCB->VTOR = APP_START_ADDR;
    __DSB();
    __ISB();

    /* After a real reset interrupts are enabled, and the startup code relies on that */
    __enable_irq();

    __set_MSP(appStack);
    appEntry();

    while (1) { }   /* not reached */
}
