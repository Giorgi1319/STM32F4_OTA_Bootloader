#ifndef INC_FLASH_OPERATIONS_H_
#define INC_FLASH_OPERATIONS_H_

#include "stm32f4xx_hal.h"

/* Call HAL_FLASH_Unlock() before and HAL_FLASH_Lock() after these. */
HAL_StatusTypeDef flash_erase_app(void);
HAL_StatusTypeDef flash_erase_header(void);
HAL_StatusTypeDef flash_write_word(uint32_t addr, uint32_t data);

#endif /* INC_FLASH_OPERATIONS_H_ */
