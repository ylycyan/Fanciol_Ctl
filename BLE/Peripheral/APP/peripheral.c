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
#include "include/flash.h"
#include "ota.h"
#include "OTAprofile.h"
#include "timer.h"
#include "device_protocol.h"
#include "device_service.h"
#include "ota_guard.h"
#include "ota_update.h"
#include "config_store.h"
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

uint32_t OpParaDataLen = 0;
uint32_t OpAdd = 0;

static uint8_t localOtaActive;

typedef int (*pImageTaskFn)(void);
pImageTaskFn user_image_tasks;

uint32_t EraseAdd = 0;
uint32_t EraseBlockNum = 0;
uint32_t EraseBlockCnt = 0;

static ota_guard_t otaGuard;
static device_reassembler_t deviceReassembler;
static uint8_t deviceFrame[DEVICE_MAX_FRAME_SIZE];
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
static void peripheralEnableAdvertising(const char *reason);
void OTA_IAPReadDataComplete(unsigned char index);
void OTA_IAPWriteData(unsigned char index, unsigned char *p_data, unsigned char w_len);
static void ProcessOtaCommand(const uint8_t *command);
void OTA_IAP_SendCMDDealSta(uint8_t deal_status);
void DisableAllIRQ(void);

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

static void peripheralEnableAdvertising(const char *reason)
{
    uint8_t enable = TRUE;
    uint8_t status = GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED,
                                          sizeof(enable), &enable);
    if(status != SUCCESS) {
        PRINT("BLE advertising restart failed: %s status=%02x\n", reason, status);
    } else {
        PRINT("BLE advertising restart requested: %s\n", reason);
    }
}
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
    bStatus_t gapServiceStatus;
    bStatus_t gattServiceStatus;
    bStatus_t devInfoStatus;
    bStatus_t simpleServiceStatus;
    bStatus_t otaServiceStatus;
    Peripheral_TaskID = TMOS_ProcessEventRegister(Peripheral_ProcessEvent);
#if defined(BLE_BASELINE_DIAGNOSTIC)
    {
        static uint8_t baselineAdv[] = {
            0x02, GAP_ADTYPE_FLAGS,
            BT_DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
            0x08, GAP_ADTYPE_LOCAL_NAME_COMPLETE,
            'S','p','l','i','t','A','C'
        };
        uint8_t enable = TRUE;
        uint16_t advInt = BT_DEFAULT_ADVERTISING_INTERVAL;
        uint8_t advStatus;
        uint8_t enableStatus;

        GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advInt);
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advInt);
        advStatus = GAPRole_SetParameter(GAPROLE_ADVERT_DATA,
                                         sizeof(baselineAdv), baselineAdv);
        enableStatus = GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED,
                                            sizeof(enable), &enable);
        gapServiceStatus = GGS_AddService(GATT_ALL_SERVICES);
        gattServiceStatus = GATTServApp_AddService(GATT_ALL_SERVICES);
        GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(attDeviceName), attDeviceName);
        peripheralInitConnItem(&peripheralConnList);
        PRINT("BLE baseline cfg: task=%u adv=%02x enable=%02x gap=%02x gatt=%02x\r\n",
              Peripheral_TaskID, advStatus, enableStatus,
              gapServiceStatus, gattServiceStatus);
        tmos_set_event(Peripheral_TaskID, SBP_START_DEVICE_EVT);
        return;
    }
#endif
    DeviceProtocol_Reset(&deviceReassembler);
    DeviceService_Init();
    OtaGuard_Reset(&otaGuard);

    {
        uint8_t advLen = peripheralBuildAdvData();
        uint8_t initial_advertising_enable = FALSE;
        uint16_t desired_min_interval = BT_DEFAULT_DESIRED_MIN_CONN_INTERVAL;
        uint16_t desired_max_interval = BT_DEFAULT_DESIRED_MAX_CONN_INTERVAL;

        uint8_t scanStatus = GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
        uint8_t advStatus = GAPRole_SetParameter(GAPROLE_ADVERT_DATA, advLen, advertData);
        uint8_t minStatus = GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(uint16_t), &desired_min_interval);
        uint8_t maxStatus = GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(uint16_t), &desired_max_interval);
        initial_advertising_enable = TRUE;
        {
            uint8_t enableStatus = GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
            PRINT("BLE adv cfg: task=%u len=%u scan=%02x adv=%02x min=%02x max=%02x enable=%02x\r\n",
                  Peripheral_TaskID, advLen, scanStatus, advStatus,
                  minStatus, maxStatus, enableStatus);
            (void)enableStatus;
        }
        (void)scanStatus;
        (void)advStatus;
        (void)minStatus;
        (void)maxStatus;
        PrintHex("BLE adv data", advertData, advLen);
    }

    {
        uint16_t advInt = BT_DEFAULT_ADVERTISING_INTERVAL;

        // Set advertising interval
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advInt);
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advInt);

        // Enable scan req notify
        GAP_SetParamValue(TGAP_ADV_SCAN_REQ_NOTIFY, ENABLE);
    }

    // The provisioning and OTA characteristics do not require link pairing.
    // Keep BLE connectionless from an identity perspective: the production
    // UID remains the only device identifier and no passkey/bond is stored.
    {
        uint8_t  pairMode = GAPBOND_PAIRING_MODE_NO_PAIRING;
        uint8_t  bonding = FALSE;
        GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof(uint8_t), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED, sizeof(uint8_t), &bonding);
    }

    // Initialize GATT attributes
    gapServiceStatus = GGS_AddService(GATT_ALL_SERVICES);           // GAP
    gattServiceStatus = GATTServApp_AddService(GATT_ALL_SERVICES);  // GATT attributes
    devInfoStatus = DevInfo_AddService();                           // Device Information Service
    simpleServiceStatus = SimpleProfile_AddService(GATT_ALL_SERVICES);
    otaServiceStatus = OTAProfile_AddService(GATT_ALL_SERVICES);
    PRINT("BLE services: gap=%02x gatt=%02x dev=%02x simple=%02x ota=%02x\r\n",
          gapServiceStatus, gattServiceStatus, devInfoStatus,
          simpleServiceStatus, otaServiceStatus);
    (void)gapServiceStatus;
    (void)gattServiceStatus;
    (void)devInfoStatus;
    (void)simpleServiceStatus;
    (void)otaServiceStatus;

    GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(attDeviceName), attDeviceName);
    PRINT("Device Name: %s\n", attDeviceName);
    PRINT("Product firmware: %lu.%lu.%lu connectivity schema=%u\r\n",
          (unsigned long)((FIRMWARE_BUILD_VERSION >> 16) & 0xFFU),
          (unsigned long)((FIRMWARE_BUILD_VERSION >> 8) & 0xFFU),
          (unsigned long)(FIRMWARE_BUILD_VERSION & 0xFFU),
          CONNECTIVITY_SCHEMA);


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

void Peripheral_RefreshDeviceName(void)
{
    uint8_t advLen = peripheralBuildAdvData();
    uint8_t advStatus = GAPRole_SetParameter(GAPROLE_ADVERT_DATA, advLen, advertData);
    uint8_t nameStatus = GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(attDeviceName), attDeviceName);
    (void)advStatus;
    (void)nameStatus;
    PRINT("BLE name applied: %s adv=%02x gatt=%02x\r\n", attDeviceName, advStatus, nameStatus);
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
        bStatus_t startStatus;
        // Start the Device
        startStatus = GAPRole_PeripheralStartDevice(Peripheral_TaskID,
                                                    &Peripheral_BondMgrCBs,
                                                    &Peripheral_PeripheralCBs);
        PRINT("BLE start status=%02x\r\n", startStatus);
        (void)startStatus;
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
        bStatus_t phyStatus;
        // start phy update
        phyStatus = GAPRole_UpdatePHY(peripheralConnList.connHandle, 0,
                                      GAP_PHY_BIT_LE_2M, GAP_PHY_BIT_LE_2M,
                                      GAP_PHY_OPTIONS_NOPRE);
        PRINT("PHY Update %x...\n", phyStatus);
        (void)phyStatus;

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
            OtaGuard_EndErase(&otaGuard, 0);
            OTA_IAP_SendCMDDealSta(status);
            return (events ^ OTA_FLASH_ERASE_EVT);
        }

        EraseBlockCnt++;

        if(EraseBlockCnt >= EraseBlockNum)
        {
            PRINT("ERASE Complete\r\n");
            OtaGuard_EndErase(&otaGuard, 1);
            OTA_IAP_SendCMDDealSta(status);
            return (events ^ OTA_FLASH_ERASE_EVT);
        }

        /* 每次只擦一个 4 KB 块，把调度权还给 BLE/LoRa，避免长时间独占主循环。 */
        tmos_start_task(Peripheral_TaskID, OTA_FLASH_ERASE_EVT, MS1_TO_SYSTEM_TIME(2));
        return (events ^ OTA_FLASH_ERASE_EVT);
    }

    if(events & OTA_FLASH_VERIFY_EVT)
    {
        uint8_t status = Ota_VerifyStep();
        if(status == DEVICE_STATUS_BUSY) {
            tmos_start_task(Peripheral_TaskID, OTA_FLASH_VERIFY_EVT, MS1_TO_SYSTEM_TIME(1));
        } else if(status == DEVICE_STATUS_OK && Ota_MarkInstall() == DEVICE_STATUS_OK) {
            localOtaActive = 0U;
            OTA_IAP_SendCMDDealSta(0U);
            tmos_start_task(Peripheral_TaskID, SBP_DEVICE_RESET_EVT, MS1_TO_SYSTEM_TIME(100));
        } else {
            localOtaActive = 0U;
            OTA_IAP_SendCMDDealSta(0xFFU);
        }
        return (events ^ OTA_FLASH_VERIFY_EVT);
    }

    if(events & SBP_DEVICE_RESET_EVT)
    {
        /* The device response is sent before this delayed event is scheduled.
         * Waiting here avoids turning an intentional restart into a BLE
         * response timeout on the installer application. */
        DisableAllIRQ();
        SYS_ResetExecute();
        return (events ^ SBP_DEVICE_RESET_EVT);
    }

    // Discard unknown events
    return 0;
}

void Peripheral_RequestReset(void)
{
    tmos_start_task(Peripheral_TaskID, SBP_DEVICE_RESET_EVT, MS1_TO_SYSTEM_TIME(600));
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
        tmos_stop_task(Peripheral_TaskID, OTA_FLASH_ERASE_EVT);
        tmos_stop_task(Peripheral_TaskID, OTA_FLASH_VERIFY_EVT);
        DeviceProtocol_Reset(&deviceReassembler);
        DeviceService_ResetSession();
        OtaGuard_Reset(&otaGuard);
        if(localOtaActive) {
            Ota_Cancel();
            localOtaActive = 0U;
        }

        // Restart advertising
        {
            peripheralEnableAdvertising("link terminated");
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
                peripheralEnableAdvertising("discoverable ended");
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
                    peripheralEnableAdvertising("connection attempt failed");
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

static void __attribute__((noinline)) SendDeviceFrame(const uint8_t *frame, uint16_t frameLen)
{
    uint8_t fragment[DEVICE_MAX_FRAGMENT_CHUNK + DEVICE_FRAGMENT_HEADER_SIZE];
    uint16_t chunkSize = (peripheralMTU > 8u) ? (uint16_t)(peripheralMTU - 8u) : 15u;
    uint8_t count;
    uint8_t index;
    uint16_t seq;
    if(chunkSize > DEVICE_MAX_FRAGMENT_CHUNK) chunkSize = DEVICE_MAX_FRAGMENT_CHUNK;
    count = (uint8_t)((frameLen + chunkSize - 1u) / chunkSize);
    seq = (frameLen >= 5u) ? ((uint16_t)frame[3] | ((uint16_t)frame[4] << 8)) : 0;
    for(index = 0; index < count; ++index) {
        uint16_t offset = (uint16_t)index * chunkSize;
        uint16_t length = (uint16_t)(frameLen - offset);
        if(length > chunkSize) length = chunkSize;
        fragment[0] = (index == 0u ? 0x80u : 0u) | (index + 1u == count ? 0x40u : 0u);
        fragment[1] = index; fragment[2] = count; fragment[3] = (uint8_t)seq; fragment[4] = (uint8_t)(seq >> 8);
        tmos_memcpy(fragment + 5, frame + offset, length);
        peripheralCharNotify(SIMPLEPROFILE_CHAR1, fragment, (uint16_t)(length + 5u));
        if(index + 1u < count) DelayMs(6);
    }
}

static uint8_t peripheralBuildAdvData(void)
{
    uint8_t p = 0;
    uint8_t localName[23];
    static const char hex[]="0123456789ABCDEF";
    const char *baseName = DeviceProfile_GetName();
    uint8_t nameLen = (uint8_t)strlen(baseName);
    uint16_t shortId;
    const char *deviceUid = DeviceUid_Get();
    shortId = DeviceUid_Valid(deviceUid) ?
              DeviceProtocol_Crc16((const uint8_t *)deviceUid, DEVICE_UID_LENGTH) : 0U;
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

    memcpy(localName,baseName,nameLen);
    if(nameLen<=17u){localName[nameLen++]='-';localName[nameLen++]=hex[(shortId>>12)&0x0Fu];localName[nameLen++]=hex[(shortId>>8)&0x0Fu];localName[nameLen++]=hex[(shortId>>4)&0x0Fu];localName[nameLen++]=hex[shortId&0x0Fu];}
    memset(attDeviceName, 0, sizeof(attDeviceName));
    memcpy(attDeviceName, localName, nameLen < sizeof(attDeviceName) ? nameLen : sizeof(attDeviceName));
    advertData[p++] = (uint8_t)(nameLen + 1);
    advertData[p++] = GAP_ADTYPE_LOCAL_NAME_COMPLETE;
    memcpy(&advertData[p], localName, nameLen);
    p = (uint8_t)(p + nameLen);

    return p;
}

static void simpleProfileChangeCB(uint8_t paramID, uint8_t *pValue, uint16_t len)
{
    switch(paramID)
    {
        case SIMPLEPROFILE_CHAR1:
        {
            if((len >= DEVICE_FRAGMENT_HEADER_SIZE) && ((pValue[0] & 0x80u) || deviceReassembler.active)) {
                uint16_t requestLen = 0;
                uint16_t responseLen = 0;
                uint8_t frameStatus = DeviceProtocol_Reassemble(&deviceReassembler, pValue, len, deviceFrame, sizeof(deviceFrame), &requestLen);
                if(frameStatus == DEVICE_STATUS_BUSY) break;
                if(frameStatus == DEVICE_STATUS_OK && DeviceService_HandleFrame(deviceFrame, requestLen, deviceFrame, sizeof(deviceFrame), &responseLen) == DEVICE_STATUS_OK) {
                    SendDeviceFrame(deviceFrame, responseLen);
                } else {
                    DeviceProtocol_Reset(&deviceReassembler);
                    PRINT("BLE device frame rejected: %u\r\n", frameStatus);
                }
                break;
            }
            /*
             * 量产固件只接受 BLE device protocol。旧协议可以绕过配置版本、范围校验和原子提交，
             * 不能作为隐藏写入口保留；旧协议处理器与 SendBtResponse 已一并删除。
             */
            DeviceProtocol_Reset(&deviceReassembler);
            PRINT("BLE legacy frame rejected\r\n");
            break;
        }

        case SIMPLEPROFILE_CHAR2:
        {
            if(len == 0u || len > SIMPLEPROFILE_CHAR2_LEN) {
                PRINT("IR passthrough rejected: len=%u\r\n", len);
                break;
            }
            PrintHex("char2 rx",pValue,len);
            if(!Ir_TransmitRawAsync(pValue, len)) {
                PRINT("IR passthrough busy\r\n");
            }
            break;
        }

        default:
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

void DisableAllIRQ(void)
{
    SYS_DisableAllIrq(NULL);
}

static uint32_t ota_u32(const uint8_t *value)
{
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static uint32_t local_ota_address(uint16_t encoded)
{
    uint32_t linked_address = (uint32_t)encoded * 16UL;
    if(linked_address < OTA_APP_ADDRESS) return 0U;
    return Ota_StagingAddress() + linked_address - OTA_APP_ADDRESS;
}

static void ProcessOtaCommand(const uint8_t *command)
{
    switch(command[0])
    {
        case CMD_IAP_PROM:
        {
            uint8_t status;

            OpParaDataLen = command[1];
            OpAdd = (uint32_t)command[2];
            OpAdd |= ((uint32_t)command[3] << 8);
            OpAdd = local_ota_address((uint16_t)OpAdd);

            PRINT("IAP_PROM: %08x len:%d \r\n", (int)OpAdd, (int)OpParaDataLen);

            if(!OtaGuard_CanProgram(&otaGuard, OpAdd, (uint16_t)OpParaDataLen)) {
                PRINT("IAP_PROM rejected: state/range/order\r\n");
                OTA_IAP_SendCMDDealSta(0xFF);
                break;
            }
            status = FLASH_ROM_WRITE(OpAdd, (uint8_t *)command + 4, (uint16_t)OpParaDataLen);
            OtaGuard_EndProgram(&otaGuard, (uint16_t)OpParaDataLen, status == SUCCESS);
            if(status) PRINT("IAP_PROM err \r\n");
            OTA_IAP_SendCMDDealSta(status);
            break;
        }
        case CMD_IAP_ERASE:
        {
            OpAdd = (uint32_t)command[2];
            OpAdd |= ((uint32_t)command[3] << 8);
            OpAdd = local_ota_address((uint16_t)OpAdd);

            EraseBlockNum = (uint32_t)command[4];
            EraseBlockNum |= ((uint32_t)command[5] << 8);
            EraseAdd = OpAdd;
            EraseBlockCnt = 0;

            PRINT("IAP_ERASE start:%08x num:%d\r\n", (int)OpAdd, (int)EraseBlockNum);

            if(!localOtaActive ||
               !OtaGuard_BeginErase(&otaGuard, EraseAdd, EraseBlockNum,
                                    FLASH_BLOCK_SIZE, Ota_StagingAddress(),
                                    Ota_StagingAddress() + OTA_APP_SIZE))
            {
                OtaGuard_Reset(&otaGuard);
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
            /* Legacy client compatibility.  New clients skip this command;
             * the complete image receives one CRC32 check at IAP_END. */
            OTA_IAP_SendCMDDealSta(localOtaActive ? 0U : 0xFFU);
            break;
        }
        case CMD_IAP_END:
        {
            PRINT("IAP_END \r\n");

            if(!OtaGuard_CanFinish(&otaGuard) ||
               otaGuard.program_next != Ota_StagingAddress() + Ota_Get()->image_size) {
                PRINT("IAP_END rejected: image not fully programmed\r\n");
                OTA_IAP_SendCMDDealSta(0xFF);
                break;
            }

            if(Ota_FinishLocal() != DEVICE_STATUS_OK) {
                PRINT("IAP_END rejected: metadata commit failed\r\n");
                OTA_IAP_SendCMDDealSta(0xFF);
                break;
            }
            tmos_set_event(Peripheral_TaskID, OTA_FLASH_VERIFY_EVT);
            break;
        }
        case CMD_IAP_MANIFEST:
        {
            uint8_t status;
            uint32_t version = ota_u32(command + 2);
            uint32_t size = ota_u32(command + 6);
            uint32_t crc = ota_u32(command + 10);
            status = Ota_BeginLocal(version, size, crc);
            if(status == DEVICE_STATUS_OK) {
                localOtaActive = 1U;
                OtaGuard_Reset(&otaGuard);
                OTA_IAP_SendCMDDealSta(0U);
            } else OTA_IAP_SendCMDDealSta(0xFFU);
            break;
        }
        case CMD_IAP_INFO:
        {
            uint8_t send_buf[20] = {0};

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
            send_buf[9] = 1U;
            send_buf[10] = (uint8_t)Ota_Get()->current_version;
            send_buf[11] = (uint8_t)(Ota_Get()->current_version >> 8);
            send_buf[12] = (uint8_t)(Ota_Get()->current_version >> 16);
            send_buf[13] = (uint8_t)(Ota_Get()->current_version >> 24);
            send_buf[14] = 1U;

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
    if(rec_data == NULL || rec_len == 0 || rec_len > IAP_LEN) {
        OTA_IAP_CMDErrDeal();
        return;
    }
    if((rec_data[0] == CMD_IAP_ERASE && rec_len != 6u) ||
       ((rec_data[0] == CMD_IAP_PROM || rec_data[0] == CMD_IAP_VERIFY) &&
        (rec_len < 4u || rec_len != (unsigned char)(rec_data[1] + 4u))) ||
       (rec_data[0] == CMD_IAP_MANIFEST && (rec_len != 14u || rec_data[1] != 12u)) ||
       ((rec_data[0] == CMD_IAP_END || rec_data[0] == CMD_IAP_INFO) && rec_len != 1u)) {
        OTA_IAP_CMDErrDeal();
        return;
    }
    ProcessOtaCommand(rec_data);
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
