#include "board.h"
#include "CH58x_common.h"
//EEPROM_BLOCK_SIZE 4096
//@return 0:success !0:fail
int Flash_Delay = 0;
int Flash_Erase(void){
    return EEPROM_ERASE(0, EEPROM_BLOCK_SIZE);
}
int Flash_Write(uint8_t *data, uint32_t len){
    return EEPROM_WRITE(0, data, len);
}
int Flash_Read(uint8_t *data, uint32_t len){
    return EEPROM_READ(0, data, len);
}

void SaveDevInfo(uint16_t delay){
    Flash_Delay = delay;
}
    
void Flash_Poll(void){
    if(Flash_Delay-- == 0){
        Flash_Delay = 1000;
        SaveDevInfo(1000);
    }
    if(Flash_Erase()){
        Dev.errorCode.bit.flash = 1;
        return;
    }
    if(Flash_Write((uint8_t*)&Dev,sizeof(Dev))){
        Dev.errorCode.bit.flash = 1;
        return;
    }
}

void LoadDevInfo(void){
    Flash_Read((uint8_t*)&Dev,sizeof(Dev));
    if(Dev.magicCode != MAGIC_CODE){ //首次上电，清空所有设备数据
        memset(&Dev,0,sizeof(Dev));
        Dev.magicCode = MAGIC_CODE;
        Dev.errorCode.bit.irMatch = 1;
    }
    //上电默认初始化
    Dev.onOff = PowerOff; //默认关
    Dev.mode = Mode_Auto; //上电模式:自动
    Dev.wind = Wind_Auto; //上电风速:自动
    Dev.lastOnTime = 0;
    Dev.lastReportTime = 0;
    Dev.runTime = 0;
    Dev.loadPower = 0;
    // Dev.setTemp = 25;
    if(Dev.irIdx >= (sizeof(g_arc_info)/sizeof(t_arc))){
        Dev.errorCode.bit.irMatch = 1;
    }
}

