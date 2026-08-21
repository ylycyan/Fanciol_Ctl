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

    codeflash分布: 入口 + 运行应用 + 固件暂存区 + Updater
        入口(0x0 + 4K):固定4K,跳转到Updater
        运行应用(0x00001000 + 216K):业务固件
        固件暂存区(0x00037000 + 216K):BLE/4G升级镜像,不直接执行
        Updater(0x6D000 + 12K):有安装标记时复制暂存镜像,否则启动运行应用
    
    dataflash分布: 最小erase block(4096B)
        0x0000 ~ 0x1FFF  配置双副本
        0x2000 ~ 0x2FFF  LoRa开发参数
        0x3000 ~ 0x3FFF  计量/运行日志(64槽轮转)
        0x4000 ~ 0x4FFF  红外学习数据A槽
        0x5000 ~ 0x5DFF  最近复位与故障快照
        0x5E00 ~ 0x5EFF  计量日志轮转检查点
        0x6000 ~ 0x6FFF  红外学习数据B槽
        0x7000 ~ 0x7004 u32   ota flag
        0x7e00 ~ 0x7FFF 蓝牙栈使用?
*/

#define DATAFLASH_ADDR_OTA      0x7000
#define DATAFLASH_ADDR_DEV      0x0000
void SaveDevInfo(uint16_t delay);
void SaveIrInfo(void);
void LoadDevInfo(void);
void Flash_Poll(void);
#endif
