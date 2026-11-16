#ifndef INC_BL_JUMP_H_
#define INC_BL_JUMP_H_

/* bootloader_is_app_valid() results */
#define APP_OK              0
#define APP_ERR_MAGIC       1   /* no header, or header magic is wrong */
#define APP_ERR_SIZE        2   /* header size is too small or larger than the app area */
#define APP_ERR_STACK       3   /* initial stack pointer is not in RAM */
#define APP_ERR_RESET       4   /* reset vector does not point inside the image */
#define APP_ERR_CRC         5   /* image CRC does not match the header */

int  bootloader_is_app_valid(void);
void JumpToApplication(void) __attribute__((noreturn));

#endif /* INC_BL_JUMP_H_ */
