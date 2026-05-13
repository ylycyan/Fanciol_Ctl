#ifndef __FLASH_H__
#define __FLASH_H__

#include <stdint.h>

/*flash分布
    ch583-m  flash(512kB) = dataflash + codeflash
    代码段 codeflash(448KB)
        0x0 ~ 0x6FFFF
    数据段 dataflash(32KB) 
        0x70000 ~ 0x77FFF
    非易失存储区 backupFlash:8KB (可行性待测试)
        0x7E000 ~ 0x7FFFF 

    codeflash分布: JumpIAP + AppA + AppB + AppUpdater
        JumpIAP(0x0 + 4K):固定4K,仅做固件跳转AppUpdate
        AppA(0x00001000 + 216K):业务运行固件
        AppB(0x00037000 + 216K):App升级,大小等同AppA
        AppbootLoader(0x6D000 + 12K):无更新固件直接跳转,若有更新固件,将AppB搬运到AppA并跳转。
    
    dataflash分布: 最小erase block(4096B)
        0x0 ~ 0x 1000  len:4k  dev设备信息
        0x2000 ~ 0x 6FFF 24K  暂不使用
        0x7000 ~ 0x7004 u32   ota flag
        0x7e00 ~ 0x7FFF 蓝牙栈使用?
*/

#define DATAFLASH_ADDR_OTA      0x7000
#define DATAFLASH_ADDR_DEV      0x0       
// bool Flash_Init(void);
int Flash_Erase(void);
int Flash_Write(uint8_t *data, uint32_t len);
int Flash_Read(uint8_t *data, uint32_t len);
void CheckFirstPower(void);
void SaveDevInfo(uint16_t delay);
void LoadDevInfo(void);
void Flash_Poll(void);
#endif
