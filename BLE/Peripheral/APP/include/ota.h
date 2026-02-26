#ifndef __OTA_H
#define __OTA_H

#define FLASH_BLOCK_SIZE       EEPROM_BLOCK_SIZE
#define IMAGE_SIZE             216 * 1024

#define IMAGE_A_FLAG           0x01
#define IMAGE_A_START_ADD      4 * 1024
#define IMAGE_A_SIZE           IMAGE_SIZE

#define IMAGE_B_FLAG           0x02
#define IMAGE_B_START_ADD      (IMAGE_A_START_ADD + IMAGE_SIZE)
#define IMAGE_B_SIZE           IMAGE_SIZE

#define IMAGE_IAP_FLAG         0x03
#define IMAGE_IAP_START_ADD    (IMAGE_B_START_ADD + IMAGE_SIZE)
#define IMAGE_IAP_SIZE         12 * 1024

#define CMD_IAP_PROM           0x80
#define CMD_IAP_ERASE          0x81
#define CMD_IAP_VERIFY         0x82
#define CMD_IAP_END            0x83
#define CMD_IAP_INFO           0x84

#define IAP_LEN                247

#define OTA_DATAFLASH_ADD      0x00077000 - FLASH_ROM_MAX_SIZE

typedef struct
{
    unsigned char ImageFlag;
    unsigned char Revd[3];
} OTADataFlashInfo_t;

typedef union
{
    struct
    {
        unsigned char cmd;
        unsigned char len;
        unsigned char addr[2];
        unsigned char block_num[2];
    } erase;
    struct
    {
        unsigned char cmd;
        unsigned char len;
        unsigned char status[2];
    } end;
    struct
    {
        unsigned char cmd;
        unsigned char len;
        unsigned char addr[2];
        unsigned char buf[IAP_LEN - 4];
    } verify;
    struct
    {
        unsigned char cmd;
        unsigned char len;
        unsigned char addr[2];
        unsigned char buf[IAP_LEN - 4];
    } program;
    struct
    {
        unsigned char cmd;
        unsigned char len;
        unsigned char buf[IAP_LEN - 2];
    } info;
    struct
    {
        unsigned char buf[IAP_LEN];
    } other;
} OTA_IAP_CMD_t;

extern unsigned char CurrImageFlag;

#endif

