/********************************** (C) COPYRIGHT *******************************
 * File Name          : peripheral.C
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/10
 * Description        : Peripheral slave multi-connection application, initialize 
 *                      broadcast connection parameters, then broadcast, after 
 *                      connecting to the host, request to update connection parameters, 
 *                      and transmit data through custom services
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "CONFIG.h"
#include "devinfoservice.h"
#include "gattprofile.h"
#include "peripheral.h"
#include "board.h"
#include "include/board.h"
#include "include/ir_tab.h"
#include "ota.h"
#include "OTAprofile.h"
#include <stdint.h>
#include <string.h>

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

// How often to perform periodic event
#define SBP_PERIODIC_EVT_PERIOD              1600

// How often to perform read rssi event
#define SBP_READ_RSSI_EVT_PERIOD             3200

// Parameter update delay
#define SBP_PARAM_UPDATE_DELAY               6400

// PHY update delay
#define SBP_PHY_UPDATE_DELAY                 2400

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8_t Peripheral_TaskID = INVALID_TASK_ID; // Task ID for internal task/event processing

static uint8_t scanRspData[] = {
    0x02,
    GAP_ADTYPE_POWER_LEVEL,
    0
};

static uint8_t advertData[31];

// GAP GATT Attributes
static uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = BT_DEVICE_NAME;

// Connection item list
static peripheralConnItem_t peripheralConnList;

static uint16_t peripheralMTU = SIMPLEPROFILE_CHAR3_LEN;

OTA_IAP_CMD_t iap_rec_data;

uint32_t OpParaDataLen = 0;
uint32_t OpAdd = 0;

__attribute__((aligned(8))) uint8_t block_buf[16];

typedef int (*pImageTaskFn)(void);
pImageTaskFn user_image_tasks;

uint32_t EraseAdd = 0;
uint32_t EraseBlockNum = 0;
uint32_t EraseBlockCnt = 0;

uint8_t VerifyStatus = 0;
/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg);
static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent);
static void performPeriodicTask(void);
static void simpleProfileChangeCB(uint8_t paramID, uint8_t *pValue, uint16_t len);
static void peripheralParamUpdateCB(uint16_t connHandle, uint16_t connInterval,
                                    uint16_t connSlaveLatency, uint16_t connTimeout);
static void peripheralInitConnItem(peripheralConnItem_t *peripheralConnList);
static void peripheralRssiCB(uint16_t connHandle, int8_t rssi);
void peripheralCharNotify(uint8_t charIndex, uint8_t *pValue, uint16_t len);
static uint8_t peripheralBuildAdvData(void);
void OTA_IAPReadDataComplete(unsigned char index);
void OTA_IAPWriteData(unsigned char index, unsigned char *p_data, unsigned char w_len);
void Rec_OTA_IAP_DataDeal(void);
void OTA_IAP_SendCMDDealSta(uint8_t deal_status);

/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Role Callbacks
static gapRolesCBs_t Peripheral_PeripheralCBs = {
    peripheralStateNotificationCB, // Profile State Change Callbacks
    peripheralRssiCB,              // When a valid RSSI is read from controller (not used by application)
    peripheralParamUpdateCB
};

// Broadcast Callbacks
static gapRolesBroadcasterCBs_t Broadcaster_BroadcasterCBs = {
    NULL, // Not used in peripheral role
    NULL  // Receive scan request callback
};

// GAP Bond Manager Callbacks
static gapBondCBs_t Peripheral_BondMgrCBs = {
    NULL, // Passcode callback (not used by application)
    NULL, // Pairing / Bonding state Callback (not used by application)
    NULL  // oob callback
};

// Simple GATT Profile Callbacks
static simpleProfileCBs_t Peripheral_SimpleProfileCBs = {
    simpleProfileChangeCB // Characteristic value change callback
};

static OTAProfileCBs_t Peripheral_OTA_IAPProfileCBs = {
    OTA_IAPReadDataComplete,
    OTA_IAPWriteData
};
/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      Peripheral_Init
 *
 * @brief   Initialization function for the Peripheral App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notificaiton ... ).
 *
 * @param   task_id - the ID assigned by TMOS.  This ID should be
 *                    used to send messages and set timers.
 *
 * @return  none
 */
void Peripheral_Init()
{
    Peripheral_TaskID = TMOS_ProcessEventRegister(Peripheral_ProcessEvent);

    {
        uint8_t advLen = peripheralBuildAdvData();
        uint8_t initial_advertising_enable = FALSE;
        uint16_t desired_min_interval = BT_DEFAULT_DESIRED_MIN_CONN_INTERVAL;
        uint16_t desired_max_interval = BT_DEFAULT_DESIRED_MAX_CONN_INTERVAL;

        GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
        GAPRole_SetParameter(GAPROLE_ADVERT_DATA, advLen, advertData);
        GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(uint16_t), &desired_min_interval);
        GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(uint16_t), &desired_max_interval);
        initial_advertising_enable = TRUE;
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
    }

    {
        uint16_t advInt = BT_DEFAULT_ADVERTISING_INTERVAL;

        // Set advertising interval
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advInt);
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advInt);

        // Enable scan req notify
        GAP_SetParamValue(TGAP_ADV_SCAN_REQ_NOTIFY, ENABLE);
    }

    // Setup the GAP Bond Manager
    {
        uint32_t passkey = 0; // passkey "000000"
        uint8_t  pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
        uint8_t  mitm = TRUE;
        uint8_t  bonding = TRUE;
        uint8_t  ioCap = GAPBOND_IO_CAP_DISPLAY_ONLY;
        GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE, sizeof(uint32_t), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof(uint8_t), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION, sizeof(uint8_t), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
        GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED, sizeof(uint8_t), &bonding);
    }

    // Initialize GATT attributes
    GGS_AddService(GATT_ALL_SERVICES);           // GAP
    GATTServApp_AddService(GATT_ALL_SERVICES);   // GATT attributes
    DevInfo_AddService();                        // Device Information Service
    SimpleProfile_AddService(GATT_ALL_SERVICES); // Simple GATT Profile
    OTAProfile_AddService(GATT_ALL_SERVICES);

    GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(attDeviceName), attDeviceName);
    PRINT("Device Name: %s\n", attDeviceName);


    // Init Connection Item
    peripheralInitConnItem(&peripheralConnList);

    // Register callback with SimpleGATTprofile
    SimpleProfile_RegisterAppCBs(&Peripheral_SimpleProfileCBs);

    OTAProfile_RegisterAppCBs(&Peripheral_OTA_IAPProfileCBs);

    // Register receive scan request callback
    GAPRole_BroadcasterSetCB(&Broadcaster_BroadcasterCBs);

    // Setup a delayed profile startup
    tmos_set_event(Peripheral_TaskID, SBP_START_DEVICE_EVT);
}

/*********************************************************************
 * @fn      peripheralInitConnItem
 *
 * @brief   Init Connection Item
 *
 * @param   peripheralConnList -
 *
 * @return  NULL
 */
static void peripheralInitConnItem(peripheralConnItem_t *peripheralConnList)
{
    peripheralConnList->connHandle = GAP_CONNHANDLE_INIT;
    peripheralConnList->connInterval = 0;
    peripheralConnList->connSlaveLatency = 0;
    peripheralConnList->connTimeout = 0;
}

/*********************************************************************
 * @fn      Peripheral_ProcessEvent
 *
 * @brief   Peripheral Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   task_id - The TMOS assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  events not processed
 */
uint16_t Peripheral_ProcessEvent(uint8_t task_id, uint16_t events)
{
    //  VOID task_id; // TMOS required parameter that isn't used in this function

    if(events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;

        if((pMsg = tmos_msg_receive(Peripheral_TaskID)) != NULL)
        {
            Peripheral_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
            // Release the TMOS message
            tmos_msg_deallocate(pMsg);
        }
        // return unprocessed events
        return (events ^ SYS_EVENT_MSG);
    }

    if(events & SBP_START_DEVICE_EVT)
    {
        // Start the Device
        GAPRole_PeripheralStartDevice(Peripheral_TaskID, &Peripheral_BondMgrCBs, &Peripheral_PeripheralCBs);
        return (events ^ SBP_START_DEVICE_EVT);
    }

    if(events & SBP_PERIODIC_EVT)
    {
        // Restart timer
        if(SBP_PERIODIC_EVT_PERIOD)
        {
            tmos_start_task(Peripheral_TaskID, SBP_PERIODIC_EVT, SBP_PERIODIC_EVT_PERIOD);
        }
        // Perform periodic application task
        performPeriodicTask();
        return (events ^ SBP_PERIODIC_EVT);
    }

    if(events & SBP_PARAM_UPDATE_EVT)
    {
        GAPRole_PeripheralConnParamUpdateReq(peripheralConnList.connHandle,
                                             BT_DEFAULT_DESIRED_MIN_CONN_INTERVAL,
                                             BT_DEFAULT_DESIRED_MAX_CONN_INTERVAL,
                                             BT_DEFAULT_DESIRED_SLAVE_LATENCY,
                                             BT_DEFAULT_DESIRED_CONN_TIMEOUT,
                                             Peripheral_TaskID);

        return (events ^ SBP_PARAM_UPDATE_EVT);
    }

    if(events & SBP_PHY_UPDATE_EVT)
    {
        // start phy update
        PRINT("PHY Update %x...\n", GAPRole_UpdatePHY(peripheralConnList.connHandle, 0, 
                    GAP_PHY_BIT_LE_2M, GAP_PHY_BIT_LE_2M, GAP_PHY_OPTIONS_NOPRE));

        return (events ^ SBP_PHY_UPDATE_EVT);
    }

    if(events & SBP_READ_RSSI_EVT)
    {
        GAPRole_ReadRssiCmd(peripheralConnList.connHandle);
        tmos_start_task(Peripheral_TaskID, SBP_READ_RSSI_EVT, SBP_READ_RSSI_EVT_PERIOD);
        return (events ^ SBP_READ_RSSI_EVT);
    }

    if(events & OTA_FLASH_ERASE_EVT)
    {
        uint8_t status;

        PRINT("ERASE:%08x num:%d\r\n", (int)(EraseAdd + EraseBlockCnt * FLASH_BLOCK_SIZE), (int)EraseBlockCnt);
        status = FLASH_ROM_ERASE(EraseAdd + EraseBlockCnt * FLASH_BLOCK_SIZE, FLASH_BLOCK_SIZE);

        if(status != SUCCESS)
        {
            OTA_IAP_SendCMDDealSta(status);
            return (events ^ OTA_FLASH_ERASE_EVT);
        }

        EraseBlockCnt++;

        if(EraseBlockCnt >= EraseBlockNum)
        {
            PRINT("ERASE Complete\r\n");
            OTA_IAP_SendCMDDealSta(status);
            return (events ^ OTA_FLASH_ERASE_EVT);
        }

        return events;
    }

    // Discard unknown events
    return 0;
}

/*********************************************************************
 * @fn      Peripheral_ProcessGAPMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void Peripheral_ProcessGAPMsg(gapRoleEvent_t *pEvent)
{
    switch(pEvent->gap.opcode)
    {
        case GAP_SCAN_REQUEST_EVENT:
        {
            #if _BT_INFO_
            PRINT("Receive scan req from %x %x %x %x %x %x  ..\n", pEvent->scanReqEvt.scannerAddr[0],
                  pEvent->scanReqEvt.scannerAddr[1], pEvent->scanReqEvt.scannerAddr[2], pEvent->scanReqEvt.scannerAddr[3],
                  pEvent->scanReqEvt.scannerAddr[4], pEvent->scanReqEvt.scannerAddr[5]);
            #endif
            break;
        }

        case GAP_PHY_UPDATE_EVENT:
        {
            PRINT("Phy update Rx:%x Tx:%x ..\n", pEvent->linkPhyUpdate.connRxPHYS, pEvent->linkPhyUpdate.connTxPHYS);
            break;
        }

        default:
            break;
    }
}

/*********************************************************************
 * @fn      Peripheral_ProcessTMOSMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg)
{
    switch(pMsg->event)
    {
        case GAP_MSG_EVENT:
        {
            Peripheral_ProcessGAPMsg((gapRoleEvent_t *)pMsg);
            break;
        }

        case GATT_MSG_EVENT:
        {
            gattMsgEvent_t *pMsgEvent;

            pMsgEvent = (gattMsgEvent_t *)pMsg;
            if(pMsgEvent->method == ATT_MTU_UPDATED_EVENT)
            {
                peripheralMTU = pMsgEvent->msg.exchangeMTUReq.clientRxMTU;
                PRINT("mtu exchange: %d\n", pMsgEvent->msg.exchangeMTUReq.clientRxMTU);
            }
            break;
        }

        default:
            break;
    }
}

/*********************************************************************
 * @fn      Peripheral_LinkEstablished
 *
 * @brief   Process link established.
 *
 * @param   pEvent - event to process
 *
 * @return  none
 */
static void Peripheral_LinkEstablished(gapRoleEvent_t *pEvent)
{
    gapEstLinkReqEvent_t *event = (gapEstLinkReqEvent_t *)pEvent;

    // See if already connected
    if(peripheralConnList.connHandle != GAP_CONNHANDLE_INIT)
    {
        GAPRole_TerminateLink(pEvent->linkCmpl.connectionHandle);
        PRINT("Connection max...\n");
    }
    else
    {
        peripheralConnList.connHandle = event->connectionHandle;
        peripheralConnList.connInterval = event->connInterval;
        peripheralConnList.connSlaveLatency = event->connLatency;
        peripheralConnList.connTimeout = event->connTimeout;
        peripheralMTU = ATT_MTU_SIZE;
        // Set timer for periodic event
        tmos_start_task(Peripheral_TaskID, SBP_PERIODIC_EVT, SBP_PERIODIC_EVT_PERIOD);

        // Set timer for param update event
        tmos_start_task(Peripheral_TaskID, SBP_PARAM_UPDATE_EVT, SBP_PARAM_UPDATE_DELAY);

        // Start read rssi
        tmos_start_task(Peripheral_TaskID, SBP_READ_RSSI_EVT, SBP_READ_RSSI_EVT_PERIOD);

        PRINT("Conn %x - Int %x \n", event->connectionHandle, event->connInterval);
    }
}

/*********************************************************************
 * @fn      Peripheral_LinkTerminated
 *
 * @brief   Process link terminated.
 *
 * @param   pEvent - event to process
 *
 * @return  none
 */
static void Peripheral_LinkTerminated(gapRoleEvent_t *pEvent)
{
    gapTerminateLinkEvent_t *event = (gapTerminateLinkEvent_t *)pEvent;

    if(event->connectionHandle == peripheralConnList.connHandle)
    {
        peripheralConnList.connHandle = GAP_CONNHANDLE_INIT;
        peripheralConnList.connInterval = 0;
        peripheralConnList.connSlaveLatency = 0;
        peripheralConnList.connTimeout = 0;
        tmos_stop_task(Peripheral_TaskID, SBP_PERIODIC_EVT);
        tmos_stop_task(Peripheral_TaskID, SBP_READ_RSSI_EVT);

        // Restart advertising
        {
            uint8_t advertising_enable = TRUE;
            GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advertising_enable);
        }
    }
    else
    {
        PRINT("ERR..\n");
    }
}

/*********************************************************************
 * @fn      peripheralRssiCB
 *
 * @brief   RSSI callback.
 *
 * @param   connHandle - connection handle
 * @param   rssi - RSSI
 *
 * @return  none
 */
static void peripheralRssiCB(uint16_t connHandle, int8_t rssi)
{
    #if _BT_INFO_
    PRINT("RSSI -%d dB Conn  %x \n", -rssi, connHandle);
    #endif
}

/*********************************************************************
 * @fn      peripheralParamUpdateCB
 *
 * @brief   Parameter update complete callback
 *
 * @param   connHandle - connect handle
 *          connInterval - connect interval
 *          connSlaveLatency - connect slave latency
 *          connTimeout - connect timeout
 *
 * @return  none
 */
static void peripheralParamUpdateCB(uint16_t connHandle, uint16_t connInterval,
                                    uint16_t connSlaveLatency, uint16_t connTimeout)
{
    if(connHandle == peripheralConnList.connHandle)
    {
        peripheralConnList.connInterval = connInterval;
        peripheralConnList.connSlaveLatency = connSlaveLatency;
        peripheralConnList.connTimeout = connTimeout;

        PRINT("Update %x - Int %x \n", connHandle, connInterval);
    }
    else
    {
        PRINT("ERR..\n");
    }
}

/*********************************************************************
 * @fn      peripheralStateNotificationCB
 *
 * @brief   Notification from the profile of a state change.
 *
 * @param   newState - new state
 *
 * @return  none
 */
static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    switch(newState & GAPROLE_STATE_ADV_MASK)
    {
        case GAPROLE_STARTED:
            PRINT("Initialized..\n");
            break;

        case GAPROLE_ADVERTISING:
            if(pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT)
            {
                Peripheral_LinkTerminated(pEvent);
                PRINT("Disconnected.. Reason:%x\n", pEvent->linkTerminate.reason);
                PRINT("Advertising..\n");
            }
            else if(pEvent->gap.opcode == GAP_MAKE_DISCOVERABLE_DONE_EVENT)
            {
                PRINT("Advertising..\n");
            }
            break;

        case GAPROLE_CONNECTED:
            if(pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT)
            {
                Peripheral_LinkEstablished(pEvent);
                PRINT("Connected..\n");
            }
            break;

        case GAPROLE_CONNECTED_ADV:
            if(pEvent->gap.opcode == GAP_MAKE_DISCOVERABLE_DONE_EVENT)
            {
                PRINT("Connected Advertising..\n");
            }
            break;

        case GAPROLE_WAITING:
            if(pEvent->gap.opcode == GAP_END_DISCOVERABLE_DONE_EVENT)
            {
                PRINT("Waiting for advertising..\n");
            }
            else if(pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT)
            {
                Peripheral_LinkTerminated(pEvent);
                PRINT("Disconnected.. Reason:%x\n", pEvent->linkTerminate.reason);
            }
            else if(pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT)
            {
                if(pEvent->gap.hdr.status != SUCCESS)
                {
                    PRINT("Waiting for advertising..\n");
                }
                else
                {
                    PRINT("Error..\n");
                }
            }
            else
            {
                PRINT("Error..%x\n", pEvent->gap.opcode);
            }
            break;

        case GAPROLE_ERROR:
            PRINT("Error..\n");
            break;

        default:
            break;
    }
}

/*********************************************************************
 * @fn      performPeriodicTask
 *
 * @brief   Perform a periodic application task. This function gets
 *          called every five seconds as a result of the SBP_PERIODIC_EVT
 *          TMOS event. In this example, the value of the third
 *          characteristic in the SimpleGATTProfile service is retrieved
 *          from the profile, and then copied into the value of the
 *          the fourth characteristic.
 *
 * @param   none
 *
 * @return  none
 */
static void performPeriodicTask(void)
{
    static uint8_t notiData[SIMPLEPROFILE_CHAR3_LEN] = {0x88,0x99,0x11,0x11,0x23};
    notiData[0]++;
    peripheralCharNotify(SIMPLEPROFILE_CHAR3, notiData, 5);
}

/*********************************************************************
 * @fn      peripheralCharNotify
 *
 * @brief   Prepare and send SimpleProfile characteristic notification
 *
 * @param   charIndex - SIMPLEPROFILE_CHAR1/2/3
 *          pValue - data to notify
 *          len - length of data
 *
 * @return  none
 */
void peripheralCharNotify(uint8_t charIndex, uint8_t *pValue, uint16_t len)
{
    attHandleValueNoti_t noti;
    if(len > (peripheralMTU - 3))
    {
        PRINT("Too large noti\n");
        return;
    }
    noti.len = len;
    noti.pValue = GATT_bm_alloc(peripheralConnList.connHandle, ATT_HANDLE_VALUE_NOTI, noti.len, NULL, 0);
    if(noti.pValue)
    {
        tmos_memcpy(noti.pValue, pValue, noti.len);
        if(simpleProfile_Notify(peripheralConnList.connHandle, charIndex, &noti) != SUCCESS)
        {
            GATT_bm_free((gattMsg_t *)&noti, ATT_HANDLE_VALUE_NOTI);
        }
    }
}

static uint8_t peripheralBuildAdvData(void)
{
    uint8_t p = 0;
    uint8_t nameLen = (uint8_t)strlen(BT_DEVICE_NAME);
    if(nameLen > 22)
    {
        nameLen = 22;
    }

    advertData[p++] = 2;
    advertData[p++] = GAP_ADTYPE_FLAGS;
    advertData[p++] = BT_DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED;

    advertData[p++] = 3;
    advertData[p++] = GAP_ADTYPE_16BIT_MORE;
    advertData[p++] = LO_UINT16(SIMPLEPROFILE_SERV_UUID);
    advertData[p++] = HI_UINT16(SIMPLEPROFILE_SERV_UUID);

    advertData[p++] = (uint8_t)(nameLen + 1);
    advertData[p++] = GAP_ADTYPE_LOCAL_NAME_COMPLETE;
    memcpy(&advertData[p], BT_DEVICE_NAME, nameLen);
    p = (uint8_t)(p + nameLen);

    return p;
}

//M:len(1)cmd(1)DATA(N)Crc(1)
//S:Len(1)cmd(1)Crc(1)
static void simpleProfileChangeCB(uint8_t paramID, uint8_t *pValue, uint16_t len)
{
    switch(paramID)
    {
        case SIMPLEPROFILE_CHAR1:
        {
            uint8_t rxbuf[50];
            tmos_memcpy(rxbuf, pValue, len);
            PrintHex("char1 rx",rxbuf,len);

            //处理指令
            if(len != rxbuf[0])
            {
                //指令长度不足
                PRINT("char1 rx len error\n");
                break;
            }
            if(!ChkCrc(rxbuf,len))
            {
                //crc 
                PRINT("char1 rx crc error\n");
                break;
            }
            BT_CMD_t cmd = rxbuf[1];
            switch(cmd)
            {
                case BT_CMD_FAIL:
                {
                    //失败指令
                    break;
                }
                case BT_CMD_DATA:
                {
                    //成功指令
                    break;
                }
                case BT_CMD_OPERATE:
                {
                    Dev.errorCode.bit.irMatch = 0;
                    Ir_cmd(rxbuf[2]);
                    //操作指令
                    break;
                }
                case BT_CMD_IRTRANS:
                {
                    //红外指令透传
                    Dev.errorCode.bit.irMatch = 0;
                    tmos_memcpy(IrBuf.txbuf,rxbuf+2,len-2);
                    IrBuf.rxlen = 0;
                    IrBuf.isFinish = 0;
                    PrintHex("send ",IrBuf.txbuf,len-2);
                    UART3_SendString(IrBuf.txbuf,len-2);
                    break;
                }
                case BT_CMD_IRMATCH:
                {
                    Dev.errorCode.bit.irMatch = 0;
                    tmos_memcpy(IrBuf.txbuf,rxbuf+2,len-2);
                    IrBuf.rxlen = 0;
                    IrBuf.isFinish = 0;
                    IrBuf.type = IR_TYPE_MATCH;
                    IrBuf.txbuf[0] = 0x30;
                    IrBuf.txbuf[1] = 0x70;
                    IrBuf.txbuf[2] = 0xa0;
                    PrintHex("send ",IrBuf.txbuf,3);
                    UART3_SendString(IrBuf.txbuf,3);
                    break;
                }
                case BT_CMD_IRLEARN:
                {
                    Dev.errorCode.bit.irMatch = 0;
                    tmos_memcpy(IrBuf.txbuf,rxbuf+2,len-2);
                    IrBuf.rxlen = 0;
                    IrBuf.isFinish = 0;
                    IrBuf.type = IR_TYPE_LEARNing;
                    IrBuf.txbuf[0] = 0x30;
                    IrBuf.txbuf[1] = 0x20;
                    IrBuf.txbuf[2] = 0x50;
                    PrintHex("send ",IrBuf.txbuf,3);
                    UART3_SendString(IrBuf.txbuf,3);
                    break;
                }
                case BT_CMD_SYSPARAMS:
                {
                    //系统参数指令
                    // tx:(len(1)cmd(1:8)option(1:1)Crc(1))
                    // rx:(len(1)cmd(1:8)option(1:1)ver(1)nodeId(2)channel(1)irIdx(1)tem(1)LoadTime(1)errorCode(2)Crc(1))
                    // PRINT("time:%s\n",__DATE__);
                    if(rxbuf[2] == eBtSet){
                        //����ָ��
                        Dev.nodeId = *(uint16_t*)(rxbuf + 4);
                        Dev.channel = rxbuf[6];
                        Dev.irIdx = rxbuf[7];
                        Dev.tem = rxbuf[8];
                        Dev.runTime = rxbuf[9];
                        // SaveDevInfo(1); //1s�󱣴�Dev����
                        PRINT("Set SysParams success.\r\n");
                    }else if(rxbuf[2] == eBtGet){
                        //��ѯָ��
                        BTFrame.dat[0] = 13; 
                        BTFrame.dat[1] = BT_CMD_SYSPARAMS; 
                        BTFrame.dat[2] = eBtGet; 
                        BTFrame.dat[3] = 0xfe; 
                        *(uint16_t*)(BTFrame.dat + 4) = Dev.nodeId; 
                        BTFrame.dat[6] = Dev.channel; 
                        BTFrame.dat[7] = g_arc_info[Dev.irIdx].cmd[Dev.irType]; 
                        BTFrame.dat[8] = Dev.tem;
                        BTFrame.dat[9] = Dev.runTime;
                        *(uint16_t*)(BTFrame.dat + 10) = Dev.errorCode.u16Val;
                        AddCrc(BTFrame.dat, 12);
                        peripheralCharNotify(SIMPLEPROFILE_CHAR1, BTFrame.dat, 13);
                    }
                    break;
                }
                case BT_CMD_UPDATE:
                {
                    //����ָ��
                    break;
                }
                case BT_CMD_RESET: //������λ
                {
                    PRINT("reset device.\r\n");
                    SYS_ResetExecute();
                    break;
                }
                case BT_CMD_ILLEGAL:
                {
                    //�Ƿ�ָ��
                    break;
                }
                default:
                {
                    //δָ֪��
                    break;
                }
            }

            // peripheralCharNotify(SIMPLEPROFILE_CHAR1, rxbuf, len-1);  //����֪ͨ



            break;
        }

        case SIMPLEPROFILE_CHAR2:
        {
            uint8_t rxbuf[SIMPLEPROFILE_CHAR2_LEN];
            tmos_memcpy(rxbuf, pValue, len);
            PrintHex("char2 rx",rxbuf,len);
            IrBuf.rxlen = 0;
            IrBuf.isFinish = 0;
            UART3_SendString(rxbuf,len);
            // peripheralCharNotify(SIMPLEPROFILE_CHAR2, rxbuf, len);
            break;
        }


        default:
            // should not reach here!
            break;
    }
}

void OTA_IAP_SendData(uint8_t *p_send_data, uint8_t send_len)
{
    OTAProfile_SendData(OTAPROFILE_CHAR, p_send_data, send_len);
}

void OTA_IAP_SendCMDDealSta(uint8_t deal_status)
{
    uint8_t send_buf[2];

    send_buf[0] = deal_status;
    send_buf[1] = 0;
    OTA_IAP_SendData(send_buf, 2);
}

void OTA_IAP_CMDErrDeal(void)
{
    OTA_IAP_SendCMDDealSta(0xfe);
}

void SwitchImageFlag(uint8_t new_flag)
{
    uint16_t i;
    uint32_t ver_flag;

    EEPROM_READ(OTA_DATAFLASH_ADD, (uint32_t *)&block_buf[0], 4);

    EEPROM_ERASE(OTA_DATAFLASH_ADD, EEPROM_PAGE_SIZE);

    block_buf[0] = new_flag;

    EEPROM_WRITE(OTA_DATAFLASH_ADD, (uint32_t *)&block_buf[0], 4);
}

void DisableAllIRQ(void)
{
    SYS_DisableAllIrq(NULL);
}

void Rec_OTA_IAP_DataDeal(void)
{
    switch(iap_rec_data.other.buf[0])
    {
        case CMD_IAP_PROM:
        {
            uint32_t i;
            uint8_t status;

            OpParaDataLen = iap_rec_data.program.len;
            OpAdd = (uint32_t)(iap_rec_data.program.addr[0]);
            OpAdd |= ((uint32_t)(iap_rec_data.program.addr[1]) << 8);
            OpAdd = OpAdd * 16;

            OpAdd += IMAGE_A_SIZE;

            PRINT("IAP_PROM: %08x len:%d \r\n", (int)OpAdd, (int)OpParaDataLen);

            status = FLASH_ROM_WRITE(OpAdd, iap_rec_data.program.buf, (uint16_t)OpParaDataLen);
            if(status) PRINT("IAP_PROM err \r\n");
            OTA_IAP_SendCMDDealSta(status);
            break;
        }
        case CMD_IAP_ERASE:
        {
            OpAdd = (uint32_t)(iap_rec_data.erase.addr[0]);
            OpAdd |= ((uint32_t)(iap_rec_data.erase.addr[1]) << 8);
            OpAdd = OpAdd * 16;

            OpAdd += IMAGE_A_SIZE;

            EraseBlockNum = (uint32_t)(iap_rec_data.erase.block_num[0]);
            EraseBlockNum |= ((uint32_t)(iap_rec_data.erase.block_num[1]) << 8);
            EraseAdd = OpAdd;
            EraseBlockCnt = 0;

            VerifyStatus = 0;

            PRINT("IAP_ERASE start:%08x num:%d\r\n", (int)OpAdd, (int)EraseBlockNum);

            if(EraseAdd < IMAGE_B_START_ADD || (EraseAdd + (EraseBlockNum - 1) * FLASH_BLOCK_SIZE) > IMAGE_IAP_START_ADD)
            {
                OTA_IAP_SendCMDDealSta(0xFF);
            }
            else
            {
                tmos_set_event(Peripheral_TaskID, OTA_FLASH_ERASE_EVT);
            }
            break;
        }
        case CMD_IAP_VERIFY:
        {
            uint32_t i;
            uint8_t status = 0;

            OpParaDataLen = iap_rec_data.verify.len;

            OpAdd = (uint32_t)(iap_rec_data.verify.addr[0]);
            OpAdd |= ((uint32_t)(iap_rec_data.verify.addr[1]) << 8);
            OpAdd = OpAdd * 16;

            OpAdd += IMAGE_A_SIZE;
            PRINT("IAP_VERIFY: %08x len:%d \r\n", (int)OpAdd, (int)OpParaDataLen);

            status = FLASH_ROM_VERIFY(OpAdd, iap_rec_data.verify.buf, OpParaDataLen);
            if(status)
            {
                PRINT("IAP_VERIFY err \r\n");
            }
            VerifyStatus |= status;
            OTA_IAP_SendCMDDealSta(VerifyStatus);
            break;
        }
        case CMD_IAP_END:
        {
            PRINT("IAP_END \r\n");

            DisableAllIRQ();

            SwitchImageFlag(IMAGE_IAP_FLAG);

            mDelaymS(10);
            SYS_ResetExecute();

            break;
        }
        case CMD_IAP_INFO:
        {
            uint8_t send_buf[20];

            PRINT("IAP_INFO \r\n");

            send_buf[0] = IMAGE_B_FLAG;

            send_buf[1] = (uint8_t)(IMAGE_SIZE & 0xff);
            send_buf[2] = (uint8_t)((IMAGE_SIZE >> 8) & 0xff);
            send_buf[3] = (uint8_t)((IMAGE_SIZE >> 16) & 0xff);
            send_buf[4] = (uint8_t)((IMAGE_SIZE >> 24) & 0xff);

            send_buf[5] = (uint8_t)(FLASH_BLOCK_SIZE & 0xff);
            send_buf[6] = (uint8_t)((FLASH_BLOCK_SIZE >> 8) & 0xff);

            send_buf[7] = CHIP_ID & 0xFF;
            send_buf[8] = (CHIP_ID >> 8) & 0xFF;

            OTA_IAP_SendData(send_buf, 20);

            break;
        }

        default:
        {
            OTA_IAP_CMDErrDeal();
            break;
        }
    }
}

void OTA_IAPReadDataComplete(unsigned char index)
{
    PRINT("OTA Send Comp \r\n");
}

void OTA_IAPWriteData(unsigned char index, unsigned char *p_data, unsigned char w_len)
{
    unsigned char rec_len;
    unsigned char *rec_data;

    rec_len = w_len;
    rec_data = p_data;
    tmos_memcpy((unsigned char *)&iap_rec_data, rec_data, rec_len);
    Rec_OTA_IAP_DataDeal();
}

//��֧��Characteristic1 (0xFFE1)������
uint16_t ReadCharCB(){
    static uint8_t charValue1[10] = {1,2,3,4,5};
    uint8_t len = 5;
    charValue1[0]++;
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR1, len, charValue1);
    return len;
}
/*********************************************************************
*********************************************************************/
