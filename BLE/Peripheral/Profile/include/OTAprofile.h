#ifndef OTAPROFILE_H
#define OTAPROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#define OTAPROFILE_CHAR         0

#define OTAPROFILE_SERV_UUID    0xFEE0

#define OTAPROFILE_CHAR_UUID    0xFEE1

#define OTAPROFILE_SERVICE      0x00000001

typedef void (*OTAProfileRead_t)(unsigned char paramID);
typedef void (*OTAProfileWrite_t)(unsigned char paramID, unsigned char *p_data, unsigned char w_len);

typedef struct
{
    OTAProfileRead_t  pfnOTAProfileRead;
    OTAProfileWrite_t pfnOTAProfileWrite;
} OTAProfileCBs_t;

bStatus_t OTAProfile_AddService(uint32_t services);

bStatus_t OTAProfile_RegisterAppCBs(OTAProfileCBs_t *appCallbacks);

bStatus_t OTAProfile_SendData(unsigned char paramID, unsigned char *p_data, unsigned char send_len);

#ifdef __cplusplus
}
#endif

#endif

