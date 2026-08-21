#ifndef TEST_CH58X_COMMON_H
#define TEST_CH58X_COMMON_H

#include <stdint.h>

#define EEPROM_PAGE_SIZE  256u
#define EEPROM_BLOCK_SIZE 4096u
#define RST_STATUS_WTR     2u
#define RST_STATUS_LRM1    6u
#define PRINT(...)         ((void)0)

uint32_t Test_EepromRead(uint32_t address, void *buffer, uint32_t length);
uint32_t Test_EepromErase(uint32_t address, uint32_t length);
uint32_t Test_EepromWrite(uint32_t address, const void *buffer, uint32_t length);
uint32_t SYS_GetLastResetSta(void);
uint32_t Test_FlashErase(uint32_t address, uint32_t length);
uint32_t Test_FlashWrite(uint32_t address, const void *buffer, uint32_t length);
uint32_t Test_FlashVerify(uint32_t address, const void *buffer, uint32_t length);
void Test_FlashRead(uint32_t address, void *buffer, uint32_t length);

#define EEPROM_READ(address, buffer, length) \
    Test_EepromRead((address), (buffer), (length))
#define EEPROM_ERASE(address, length) \
    Test_EepromErase((address), (length))
#define EEPROM_WRITE(address, buffer, length) \
    Test_EepromWrite((address), (buffer), (length))
#define FLASH_ROM_ERASE(address, length) Test_FlashErase((address), (length))
#define FLASH_ROM_WRITE(address, buffer, length) Test_FlashWrite((address), (buffer), (length))
#define FLASH_ROM_VERIFY(address, buffer, length) Test_FlashVerify((address), (buffer), (length))
#define FLASH_ROM_READ(address, buffer, length) Test_FlashRead((address), (buffer), (length))

#endif
