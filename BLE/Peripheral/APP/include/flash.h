#ifndef __FLASH_H__
#define __FLASH_H__

// bool Flash_Init(void);
int Flash_Erase(void);
int Flash_Write(uint8_t *data, uint32_t len);
int Flash_Read(uint8_t *data, uint32_t len);
void CheckFirstPower(void);
void SaveDevInfo(uint16_t delay);
void LoadDevInfo(void);
void Flash_Poll(void);
#endif
