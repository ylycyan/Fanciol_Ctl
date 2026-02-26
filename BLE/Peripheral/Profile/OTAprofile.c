#include "CONFIG.h"
#include "OTAprofile.h"
#include "CH583SFR.h"
#include "ota.h"

const uint8_t OTAProfileServUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(OTAPROFILE_SERV_UUID), HI_UINT16(OTAPROFILE_SERV_UUID)};

const uint8_t OTAProfilechar1UUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(OTAPROFILE_CHAR_UUID), HI_UINT16(OTAPROFILE_CHAR_UUID)};

static OTAProfileCBs_t *OTAProfile_AppCBs = NULL;

static const gattAttrType_t OTAProfileService = {ATT_BT_UUID_SIZE, OTAProfileServUUID};

static uint8_t OTAProfileCharProps = GATT_PROP_READ | GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;

static uint8_t OTAProfileChar = 0;

static uint8_t OTAProfileCharUserDesp[12] = "OTA Channel";

static uint8_t OTAProfileReadLen;
static uint8_t OTAProfileReadBuf[IAP_LEN];
static uint8_t OTAProfileWriteLen;
static uint8_t OTAProfileWriteBuf[IAP_LEN];

static gattAttribute_t OTAProfileAttrTbl[4] = {
    {
        {ATT_BT_UUID_SIZE, primaryServiceUUID},
        GATT_PERMIT_READ,
        0,
        (uint8_t *)&OTAProfileService},

    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &OTAProfileCharProps},

    {
        {ATT_BT_UUID_SIZE, OTAProfilechar1UUID},
        GATT_PERMIT_READ | GATT_PERMIT_WRITE,
        0,
        &OTAProfileChar},

    {
        {ATT_BT_UUID_SIZE, charUserDescUUID},
        GATT_PERMIT_READ,
        0,
        OTAProfileCharUserDesp},
};

static bStatus_t OTAProfile_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                       uint8_t *pValue, uint16_t *pLen, uint16_t offset, uint16_t maxLen, uint8_t method);
static bStatus_t OTAProfile_WriteAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                        uint8_t *pValue, uint16_t len, uint16_t offset, uint8_t method);

gattServiceCBs_t OTAProfileCBs = {
    OTAProfile_ReadAttrCB,
    OTAProfile_WriteAttrCB,
    NULL};

bStatus_t OTAProfile_AddService(uint32_t services)
{
    uint8_t status = SUCCESS;

    if(services & OTAPROFILE_SERVICE)
    {
        status = GATTServApp_RegisterService(OTAProfileAttrTbl,
                                             GATT_NUM_ATTRS(OTAProfileAttrTbl),
                                             GATT_MAX_ENCRYPT_KEY_SIZE,
                                             &OTAProfileCBs);
    }

    return status;
}

bStatus_t OTAProfile_RegisterAppCBs(OTAProfileCBs_t *appCallbacks)
{
    if(appCallbacks)
    {
        OTAProfile_AppCBs = appCallbacks;
        return SUCCESS;
    }
    else
    {
        return bleAlreadyInRequestedMode;
    }
}

static bStatus_t OTAProfile_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                       uint8_t *pValue, uint16_t *pLen, uint16_t offset, uint16_t maxLen, uint8_t method)
{
    bStatus_t status = SUCCESS;

    if(pAttr->type.len == ATT_BT_UUID_SIZE)
    {
        uint16_t uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);

        switch(uuid)
        {
            case OTAPROFILE_CHAR_UUID:
            {
                *pLen = 0;
                if(OTAProfileReadLen)
                {
                    *pLen = OTAProfileReadLen;
                    tmos_memcpy(pValue, OTAProfileReadBuf, OTAProfileReadLen);
                    OTAProfileReadLen = 0;
                    if(OTAProfile_AppCBs && OTAProfile_AppCBs->pfnOTAProfileRead)
                    {
                        OTAProfile_AppCBs->pfnOTAProfileRead(OTAPROFILE_CHAR);
                    }
                }
                break;
            }
            default:
            {
                *pLen = 0;
                status = ATT_ERR_ATTR_NOT_FOUND;
                break;
            }
        }
    }
    else
    {
        *pLen = 0;
        status = ATT_ERR_INVALID_HANDLE;
    }

    return status;
}

static bStatus_t OTAProfile_WriteAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                        uint8_t *pValue, uint16_t len, uint16_t offset, uint8_t method)
{
    bStatus_t status = SUCCESS;

    if(pAttr->type.len == ATT_BT_UUID_SIZE)
    {
        uint16_t uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);

        switch(uuid)
        {
            case OTAPROFILE_CHAR_UUID:
            {
                if(status == SUCCESS)
                {
                    uint16_t i;
                    uint8_t *p_rec_buf;

                    OTAProfileWriteLen = len;
                    p_rec_buf = pValue;
                    for(i = 0; i < OTAProfileWriteLen; i++)
                    {
                        OTAProfileWriteBuf[i] = p_rec_buf[i];
                    }
                }
                break;
            }

            default:
                status = ATT_ERR_ATTR_NOT_FOUND;
                break;
        }
    }
    else
    {
        status = ATT_ERR_INVALID_HANDLE;
    }

    if(OTAProfileWriteLen && OTAProfile_AppCBs && OTAProfile_AppCBs->pfnOTAProfileWrite)
    {
        OTAProfile_AppCBs->pfnOTAProfileWrite(OTAPROFILE_CHAR, OTAProfileWriteBuf, OTAProfileWriteLen);
        OTAProfileWriteLen = 0;
    }

    return status;
}

bStatus_t OTAProfile_SendData(unsigned char paramID, unsigned char *p_data, unsigned char send_len)
{
    bStatus_t status = SUCCESS;

    if(send_len > 20)
    {
        return 0xfe;
    }

    OTAProfileReadLen = send_len;
    tmos_memcpy(OTAProfileReadBuf, p_data, OTAProfileReadLen);

    return status;
}

