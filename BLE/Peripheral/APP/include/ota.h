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
#define CMD_IAP_END            0x83
#define CMD_IAP_INFO           0x84
#define CMD_IAP_MANIFEST       0x85

#define IAP_LEN                247

typedef struct
{
    unsigned char ImageFlag;
    unsigned char Revd[3];
} OTADataFlashInfo_t;

extern unsigned char CurrImageFlag;

#endif

