"""Compile actual driver functions with hardware/transport mocks; no target test code."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'BLE/Peripheral/APP'
version_source = (APP / 'include/product_version.h').read_text(encoding='utf-8')
version_parts = re.search(r'#define FIRMWARE_VERSION "([0-9.]+)"', version_source).group(1).split('.')
FIRMWARE_BUILD_VERSION = sum(int(value) << (16 - 8 * index)
                             for index, value in enumerate(version_parts))


def function(source, name):
    match = re.search(r'\b' + name + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        raise AssertionError(f'missing C definition: {name}')
    start = source.rfind('\n', 0, match.start()) + 1
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def no_includes(source):
    return re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)


class RuntimeRecoveryTests(unittest.TestCase):
    def test_local_rule_records_are_bounded_fifo_and_never_replayed_offline(self):
        source = (APP / 'ml307r.c').read_text(encoding='utf-8')
        context = re.search(r'typedef struct \{.*?\} ml307_context_t;', source, re.S).group()
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "board.h"
#include "ml307r.h"
#include "device_protocol.h"
#define ML307_MGMT_MAGIC 0xC7U
#define ML307_MGMT_VERSION 1U
#define ML307_MGMT_RULE_RESULT 13U
#define ML307_RULE_RECORD_SIZE 16U
#define ML307_RULE_RECORD_COUNT 6U
''' + context + r'''
static ml307_context_t modem;
static uint8_t management_frame[251],publishing_management_frame[251];
static uint8_t rule_records[ML307_RULE_RECORD_COUNT][ML307_RULE_RECORD_SIZE];
static uint8_t rule_record_head,rule_record_count;
t_dev Dev;
volatile uint32_t CurTick;
uint32_t LocalTimestamp;
static uint8_t clock_valid=1,sensor_valid=1;
uint8_t RTC_IsTimeValid(void) {return clock_valid;}
uint8_t ADC_IsValid(void) {return sensor_valid;}
uint8_t Ml307_IsOnline(void) {return modem.status.mqtt_online;}
static uint8_t GatewayLora_Checksum(const uint8_t *p,uint16_t n) {uint8_t sum=0xEC;while(n--)sum+=*p++;return sum;}
''' + '\n'.join(function(source,n) for n in ('frame_put16','frame_put32','queue_management_frame','Ml307_RecordRule','poll_rule_records')) + r'''
int main(void) {
    Dev.irActType=ACT_TYPE_IR;Dev.roomTempX10=282;
    Dev.rules[2].act.raw[6]=26;Dev.rules[2].act.raw[7]=2;
    LocalTimestamp=1758379264;CurTick=0x04030201;
    Ml307_RecordRule(2,1,3,1);assert(!rule_record_count); /* offline: not stored */
    modem.status.mqtt_online=1;modem.status.phase=ML307_PHASE_SIGNAL_QUERY;
    Ml307_RecordRule(2,1,3,1);poll_rule_records();assert(rule_record_count==1 && !modem.management_pending);
    const uint8_t golden[]={0x00,0xBD,0xCE,0x68,1,2,3,4,3,1,3,0,26,2,0x1A,1};
    assert(!memcmp(rule_records[0],golden,16));
    for(unsigned i=1;i<10;i++){CurTick++;Ml307_RecordRule(0,5,2,1);}
    assert(rule_record_count==6 && !memcmp(rule_records[0],golden,16));
    modem.status.phase=ML307_PHASE_ONLINE;modem.management_pending=1;management_frame[2]=12;
    poll_rule_records();assert(rule_record_count==6 && management_frame[2]==12);
    modem.management_pending=0;poll_rule_records();assert(rule_record_count==5 && management_frame[2]==13);
    assert(!memcmp(management_frame+8,golden,16));
    modem.management_pending=0;poll_rule_records();assert(rule_record_count==4 && management_frame[16]==1 && management_frame[18]==2);
    memcpy(publishing_management_frame,management_frame,25);modem.publishing_management=1;
    modem.status.mqtt_online=0;poll_rule_records();assert(!rule_record_count && !modem.management_pending && !modem.publishing_management);
    modem.management_pending=modem.publishing_management=1;management_frame[2]=12;publishing_management_frame[2]=4;
    poll_rule_records();assert(modem.management_pending && modem.publishing_management); /* preserve other RPC replies */
    modem.management_pending=modem.publishing_management=0;modem.status.mqtt_online=1;
    poll_rule_records();assert(!modem.management_pending); /* reconnect: no replay */
    clock_valid=sensor_valid=0;Ml307_RecordRule(0,5,2,0);poll_rule_records();
    assert(!memcmp(management_frame+8,"\0\0\0\0",4));assert(management_frame[19]==6 && management_frame[23]==0x80);
    assert(management_frame[20]==0 && management_frame[21]==0);
    return 0;
}
''')

    def test_calendar_rule_parser_matches_js_vector_and_rejects_invalid_extensions(self):
        source = (APP / 'device_service.c').read_text(encoding='utf-8')
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "board.h"
#include "device_protocol.h"
t_dev Dev;
static uint8_t clock_valid=1;
uint8_t RTC_IsTimeValid(void) {return clock_valid;}
uint8_t Rule_SchedulesOverlap(const DEV_RULE_T *a,const RULE_SCHEDULE_T *sa,const DEV_RULE_T *b,const RULE_SCHEDULE_T *sb) {(void)a;(void)sa;(void)b;(void)sb;return 0;}
''' + function(source,'get16') + function(source,'parse_rules') + r'''
int main(void) {
    uint8_t p[]={0x01,0x01,0x7f,0x38,0x04,0x38,0x04,0xfa,0x00,0x18,0x01,0x05,0x00,0x02,0x00,0x00,0x00,0x1a,0x00,0x01,0x02,0x39,0x26,0x39,0x26,0x0a,0x00};
    assert(parse_rules(p,sizeof(p),Dev.rules,Dev.ruleSchedules)==DEVICE_STATUS_OK);
    assert(Dev.rules[0].ctrl.trig_type==TRIG_TIME && Dev.rules[0].trig_val==1080);
    assert(Dev.ruleSchedules[0].date_mode==2 && Dev.ruleSchedules[0].start_day==9785 && Dev.ruleSchedules[0].hysteresis_x10==10);
    p[26]=1;assert(parse_rules(p,sizeof(p),Dev.rules,Dev.ruleSchedules)==DEVICE_STATUS_INVALID_ARG);p[26]=0;
    p[25]=0;assert(parse_rules(p,sizeof(p),Dev.rules,Dev.ruleSchedules)==DEVICE_STATUS_INVALID_ARG);p[25]=10;
    p[20]=4;assert(parse_rules(p,sizeof(p),Dev.rules,Dev.ruleSchedules)==DEVICE_STATUS_INVALID_ARG);p[20]=2;
    clock_valid=0;assert(parse_rules(p,sizeof(p),Dev.rules,Dev.ruleSchedules)==DEVICE_STATUS_CONFLICT);
    p[1]=0;assert(parse_rules(p,sizeof(p),Dev.rules,Dev.ruleSchedules)==DEVICE_STATUS_OK);
    p[0]=7;assert(parse_rules(p,sizeof(p),Dev.rules,Dev.ruleSchedules)==DEVICE_STATUS_INVALID_ARG);
    return 0;
}
''')

    def test_led_running_and_network_indicators_do_not_blink(self):
        source = no_includes((APP / 'led.c').read_text(encoding='utf-8'))
        led_init = function((APP / 'include/board.h').read_text(encoding='utf-8'), 'Led_Init')
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#define LED_GREEN_PIN 1U
#define LED_WHITE_PIN 2U
#define LED_BLUE_PIN 4U
#define LED_RED_PIN 8U
#define Status_Connected 4U
#define GAPROLE_STATE 0U
#define GAPROLE_STATE_ADV_MASK 15U
#define GAPROLE_ADVERTISING 1U
#define GAPROLE_CONNECTED 2U
#define GAPROLE_CONNECTED_ADV 3U
#define SUCCESS 0U
#define GPIO_ModeOut_PP_5mA 0U
#define ML307_ERROR_MODEM 1U
#define ML307_ERROR_RX_OVERFLOW 2U
#define ML307_ERROR_UART 3U
typedef struct { uint8_t mqtt_online, last_error, sim_ready; } ml307_status_t;
typedef struct { uint8_t valid; } HLW8110_Status_t;
static HLW8110_Status_t meter;
static ml307_status_t cell;
static struct {
    uint8_t loraStatus;
    struct { struct { uint8_t flash, ad, power, rtc, lora, cellular, ble; } bit; } errorCode;
} Dev;
static uint32_t CurTick, lights;
static uint8_t lora, cellular, identify, ble, ble_error, adc_valid;
static void GPIOB_ResetBits(uint32_t pins) { lights |= pins; }
static void GPIOB_SetBits(uint32_t pins) { lights &= ~pins; }
static void GPIOB_ModeCfg(uint32_t pins, unsigned mode) { (void)pins; (void)mode; }
static uint8_t ADC_IsValid(void) { return adc_valid; }
static const HLW8110_Status_t *HLW8110_GetStatus(void) { return &meter; }
static const ml307_status_t *Ml307_GetStatus(void) { return &cell; }
static uint8_t Connectivity_LoraEnabled(void) { return lora; }
static uint8_t Connectivity_CellularEnabled(void) { return cellular; }
static uint8_t DeviceService_IdentifyActive(void) { return identify; }
static uint8_t GAPRole_GetParameter(unsigned p, uint8_t *s) { (void)p; *s=ble; return ble_error; }
''' + led_init + source + r'''
int main(void) {
    Led_Init();assert(lights==(LED_GREEN_PIN|LED_RED_PIN));
    cellular=1;ble=GAPROLE_ADVERTISING;
    for(CurTick=0; CurTick<5000; CurTick+=100) {
        /* Temporary startup failures must not make the self-test lamp flicker. */
        Dev.errorCode.bit.cellular=(CurTick/100U)&1U;
        LED_Pro();assert(lights==(LED_GREEN_PIN|LED_RED_PIN));
    }
    Dev.errorCode.bit.cellular=0;
    LED_Pro();assert(lights & LED_RED_PIN);
    adc_valid=1;LED_Pro();assert(lights & LED_RED_PIN);
    meter.valid=1;LED_Pro();assert(lights & LED_RED_PIN); /* SIM still unready */
    cell.sim_ready=1;ble_error=1;LED_Pro();assert(lights & LED_RED_PIN);
    ble_error=0;ble=0;LED_Pro();assert(lights & LED_RED_PIN);
    ble=GAPROLE_ADVERTISING;LED_Pro();assert(!(lights & LED_RED_PIN));
    assert(bootChecked && !cell.mqtt_online); /* no network required */
    cell.sim_ready=0;LED_Pro();assert(!(lights & LED_RED_PIN)); /* no second boot test */
    bootChecked=0;cellular=0;LED_Pro();assert(bootChecked && !(lights & LED_RED_PIN));
    ble=0;
    for(CurTick=6000; CurTick<10000; CurTick+=100) {
        lora=1; cellular=0; Dev.loraStatus=0; LED_Pro();
        assert(lights==LED_GREEN_PIN);
        Dev.loraStatus=Status_Connected; LED_Pro();
        assert(lights==(LED_GREEN_PIN|LED_WHITE_PIN));
        lora=0; cellular=1; cell.mqtt_online=0; LED_Pro();
        assert(lights==LED_GREEN_PIN);
        cell.mqtt_online=1; ble=GAPROLE_CONNECTED; LED_Pro();
        assert(lights==(LED_GREEN_PIN|LED_WHITE_PIN|LED_BLUE_PIN));
        identify=1; LED_Pro();
        assert((lights & (LED_GREEN_PIN|LED_WHITE_PIN))==(LED_GREEN_PIN|LED_WHITE_PIN));
        assert(!!(lights & LED_BLUE_PIN)==((CurTick/100U)&1U));
        identify=0; ble=0;
        cellular=0; LED_Pro(); assert(lights==LED_GREEN_PIN);
        Dev.errorCode.bit.flash=1; LED_Pro();
        assert(lights==(LED_GREEN_PIN|LED_RED_PIN));
        Dev.errorCode.bit.flash=0;
    }
    cellular=1;cell.mqtt_online=0;cell.last_error=ML307_ERROR_MODEM;
    LED_Pro();assert(!(lights & LED_RED_PIN)); /* a status code alone is not a hardware diagnosis */
    Dev.errorCode.bit.cellular=1;LED_Pro();assert(lights & LED_RED_PIN);
    identify=1;LED_Pro();assert(lights & LED_RED_PIN); /* identify never hides a hardware fault */
    identify=0;cellular=0;LED_Pro();assert(!(lights & LED_RED_PIN));
    Dev.errorCode.bit.cellular=0;Dev.errorCode.bit.ble=1;LED_Pro();assert(lights & LED_RED_PIN);
    Dev.errorCode.bit.ble=0;
    CurTick=UINT32_MAX-199U; LED_NotifyIrTx();
    for(unsigned offset=0; offset<=500; offset+=100) {
        CurTick=(UINT32_MAX-199U)+offset; LED_Pro();
        assert(!!(lights & LED_GREEN_PIN)==(offset>=400 || (offset/100U)%2U==0));
        assert(!(lights & LED_WHITE_PIN));
        assert(!(lights & LED_RED_PIN)); /* completed self-test survives tick wrap */
    }
    return 0;
}
''')

    def test_at_selftest_is_early_bounded_and_recovers_without_mqtt(self):
        source = (APP / 'ml307r.c').read_text(encoding='utf-8')
        boot = int(re.search(r'#define ML307_BOOT_WAIT_MS\s+(\d+)U', source).group(1))
        wait = int(re.search(r'#define ML307_SYNC_TIMEOUT_MS\s+(\d+)U', source).group(1))
        retry = int(re.search(r'#define ML307_SYNC_RETRY_MS\s+(\d+)U', source).group(1))
        self.assertEqual(boot, 3000)
        self.assertLessEqual(1000 + boot + wait * 2 + retry, 5000)
        self.compile_run(r'''
#include <assert.h>
#include "board.h"
t_dev Dev;
static uint8_t at_sync_failures, sim_failures;
''' + function(source, 'record_at_response') + r'''
int main(void) {
    Dev.errorCode.bit.cellular=1;assert(Dev.errorCode.u16Val==0x80);
    Dev.errorCode.u16Val=0;Dev.errorCode.bit.ble=1;assert(Dev.errorCode.u16Val==0x100);
    assert(sizeof(Dev.errorCode)==2);
    record_at_response(1);assert(!Dev.errorCode.bit.cellular);
    record_at_response(0);assert(!Dev.errorCode.bit.cellular);
    record_at_response(0);assert(Dev.errorCode.bit.cellular);
    for(unsigned i=0;i<1000;i++)record_at_response(0);
    assert(at_sync_failures==2 && Dev.errorCode.bit.cellular);
    Dev.errorCode.bit.power=1;record_at_response(1);
    assert(!Dev.errorCode.bit.cellular && Dev.errorCode.bit.power && Dev.errorCode.bit.ble);
    record_at_response(0);assert(!Dev.errorCode.bit.cellular);
    sim_failures=2;record_at_response(1);assert(Dev.errorCode.bit.cellular);
    sim_failures=0;record_at_response(1);assert(!Dev.errorCode.bit.cellular);
    return 0;
}
''')

    def test_ble_selftest_only_restarts_missing_advertising_and_handles_tick_wrap(self):
        source = (APP / 'peripheral.c').read_text(encoding='utf-8')
        self.compile_run(r'''
#include <assert.h>
#include "board.h"
#define SUCCESS 0U
#define GAPROLE_STATE 0U
#define GAPROLE_STATE_ADV_MASK 15U
#define GAPROLE_ADVERTISING 1U
#define GAPROLE_CONNECTED 2U
#define GAPROLE_CONNECTED_ADV 3U
t_dev Dev;
volatile uint32_t CurTick;
static uint32_t bleCheckTick,bleReadyTick;
static uint8_t bleInitFault,state,get_status;
static unsigned retries;
static uint8_t GAPRole_GetParameter(unsigned p,uint8_t *s) {(void)p;*s=state;return get_status;}
static void peripheralEnableAdvertising(const char *reason) {(void)reason;retries++;}
''' + function(source, 'Peripheral_CheckHealth') + r'''
int main(void) {
    CurTick=999;Peripheral_CheckHealth();assert(!retries && !Dev.errorCode.bit.ble);
    CurTick=1000;Peripheral_CheckHealth();assert(retries==1 && !Dev.errorCode.bit.ble);
    CurTick=4999;Peripheral_CheckHealth();assert(!Dev.errorCode.bit.ble);
    CurTick=5000;Peripheral_CheckHealth();assert(Dev.errorCode.bit.ble);
    state=GAPROLE_ADVERTISING;Peripheral_CheckHealth();assert(!Dev.errorCode.bit.ble);
    unsigned before=retries;
    for(CurTick=5100;CurTick<20000;CurTick+=100) {
        state=GAPROLE_CONNECTED;Peripheral_CheckHealth();
        assert(retries==before && !Dev.errorCode.bit.ble);
    }
    get_status=1;CurTick=24899;Peripheral_CheckHealth();assert(!Dev.errorCode.bit.ble);
    CurTick=24900;Peripheral_CheckHealth();assert(Dev.errorCode.bit.ble);
    get_status=0;state=GAPROLE_CONNECTED_ADV;Peripheral_CheckHealth();assert(!Dev.errorCode.bit.ble);
    bleInitFault=1;Peripheral_CheckHealth();assert(Dev.errorCode.bit.ble);
    bleInitFault=0;state=0;bleCheckTick=bleReadyTick=UINT32_MAX-2499U;
    Dev.errorCode.bit.ble=0;CurTick=2499;Peripheral_CheckHealth();assert(!Dev.errorCode.bit.ble);
    CurTick=2500;Peripheral_CheckHealth();assert(Dev.errorCode.bit.ble);
    return 0;
}
''')

    def test_startup_watchdog_precedes_ble_initialization(self):
        source = (APP / 'peripheral_main.c').read_text(encoding='utf-8')
        main = source[source.rindex('int main(void)'):]
        self.assertLess(main.index('WWDG_Init()'), main.index('CH58X_BLEInit()'))
        self.assertIn('LoadDevInfo();\n    WWDG_Refresh();', main)
        self.assertLess(main.index('RTC_ProductInit()'), main.index('Health_Init('))
        self.assertLess(main.index('Health_Init('), main.index('Peripheral_Init()'))
        self.assertIn('Health_Init(retainedResetReason, RTC_IsTimeValid() ? LocalTimestamp : 0U)', main)

    def test_flash_faults_clear_only_after_the_same_source_recovers(self):
        source = no_includes((APP / 'flash.c').read_text(encoding='utf-8'))
        source = source[:source.index('void LoadDevInfo')]
        self.compile_run(r'''
#include <assert.h>
#include "board.h"
#include "device_protocol.h"
#include "config_store.h"
t_dev Dev;
static uint8_t runtime_status,ir_status,startup_flags;
static uint8_t storage_faults;
uint8_t Config_CommitIfChanged(void) {return 0;}
uint8_t Runtime_Append(void) {Storage_ReportFault(STORAGE_FAULT_RUNTIME,runtime_status!=0);return runtime_status;}
uint8_t IrStore_SaveIfChanged(void) {Storage_ReportFault(STORAGE_FAULT_IR,ir_status!=0);return ir_status;}
uint32_t Config_GetRevision(void) {return 1;}
uint8_t Storage_GetStartupFlags(void) {return startup_flags;}
''' + function((APP / 'config_store.c').read_text(encoding='utf-8'), 'Storage_ReportFault') + source + r'''
int main(void) {
 runtime_status=DEVICE_STATUS_IO_ERROR;SaveDevInfo(0);Flash_Poll();
 assert(Dev.errorCode.bit.flash);
 SaveIrInfo();Flash_Poll();assert(Dev.errorCode.bit.flash);
 runtime_status=0;SaveDevInfo(0);Flash_Poll();assert(!Dev.errorCode.bit.flash);
 ir_status=DEVICE_STATUS_IO_ERROR;SaveIrInfo();Flash_Poll();
 SaveDevInfo(0);Flash_Poll();assert(Dev.errorCode.bit.flash);
 ir_status=0;SaveIrInfo();Flash_Poll();assert(!Dev.errorCode.bit.flash);
 startup_flags=STORAGE_STARTUP_DEGRADED;SaveIrInfo();Flash_Poll();
 assert(Dev.errorCode.bit.flash);
 return 0;
}
''')

    def test_ble_ota_manifest_rejects_inflight_http_before_changing_session(self):
        source = (APP / 'peripheral.c').read_text(encoding='utf-8')
        case = source[source.index('case CMD_IAP_MANIFEST:'):source.index('case CMD_IAP_INFO:')]
        modem = (APP / 'ml307r.c').read_text(encoding='utf-8')
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#define CMD_IAP_MANIFEST 1
#define DEVICE_STATUS_BUSY 3
#define DEVICE_STATUS_OK 0
#define ML307_HTTP_IDLE 0
static struct {uint8_t http_step,http_raw_state;} modem;
static uint8_t localOtaActive,otaGuard,reply;
static unsigned started;
static uint32_t ota_u32(const uint8_t *p){return p[0];}
static uint8_t Ota_BeginLocal(uint32_t v,uint32_t s,uint32_t c){(void)v;(void)s;(void)c;started++;return 0;}
static void OtaGuard_Reset(uint8_t *p){*p=0;}
static void OTA_IAP_SendCMDDealSta(uint8_t s){reply=s;}
''' + function(modem, 'Ml307_HttpBusy') + '\nstatic void manifest(const uint8_t *command) {switch(command[0]) {\n' + case + r'''
}}
int main(void) {
 uint8_t command[14]={1};
 modem.http_step=1;manifest(command);assert(!started && reply==0xFF && !localOtaActive);
 modem.http_step=0;modem.http_raw_state=2;
 manifest(command);assert(!started && reply==0xFF);
 modem.http_raw_state=0;manifest(command);assert(started==1 && reply==0 && localOtaActive);
 return 0;
}
''')
        self.assertIn('Ota_Get()->url_length == 0U ||', modem)

    def test_gateway_frame_loop_rejects_invalid_lengths_and_stops_at_buffer_end(self):
        gateway = Path('F:/learn_code/GWH743/GWH743')
        source = (gateway / 'Core/Src/main.c').read_text(encoding='utf-8')
        util = (gateway / 'Core/Src/util.c').read_text(encoding='utf-8')
        start = source.index('while (result >= 2)')
        loop = source[start:source.index('Gateway.lastSvrTimestamp', start)] + '}\n'
        loop = loop.replace('rxPtr = RxBuf + offset;', 'assert(++iterations < 4);rxPtr = RxBuf + offset;')
        check_start = source.index('if (cmd == (uint8_t)eCmdOperate &&')
        check = source[check_start:source.index('tmpCfgTimestamp =', check_start)]
        check = check.replace('continue;', 'return 0;')
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define PRINTF(...) ((void)0)
#define THEKEY 0
''' + function(util, 'AddCrc32') + function(util, 'ChkCrc32') + r'''
static unsigned parse(uint8_t *RxBuf,int result) {
 unsigned iterations=0;
 uint16_t offset=0,rxLen;
 uint8_t *rxPtr;
''' + loop + r'''
 return offset;
}
static uint8_t valid_operation(uint8_t *rxPtr,uint16_t rxLen) {
 const uint8_t cmd=9,eCmdOperate=9;
 #define SERVER_OPERATE_DATA_LENGTH 18U
''' + check + r'''
 return 1;
}
int main(void) {
 uint8_t data[13]={0};
 assert(parse(data,0)==0 && parse(data,1)==0 && parse(data,2)==0);
 data[0]=1;assert(parse(data,2)==0);
 data[0]=11;assert(parse(data,13)==0);
 data[0]=14;assert(parse(data,13)==0);
 data[0]=12;AddCrc32(data,8,0);
 assert(parse(data,12)==12 && parse(data,13)==12);
 data[3]=1;assert(!valid_operation(data,12) && valid_operation(data,14));
 data[3]=0;data[8]=1;
 assert(!valid_operation(data,13) && !valid_operation(data,30) && valid_operation(data,31));
 return 0;
}
''')

    def test_mqtt_management_state_control_and_fault_coalescing(self):
        source = (APP / 'ml307r.c').read_text(encoding='utf-8')
        alarm = re.search(r'    if\(modem.status.phase == ML307_PHASE_ONLINE && !modem.management_pending\) \{\n        uint16_t fault.*?\n    }', source, re.S).group(0)
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define ML307_MGMT_MAGIC 0xC7U
#define ML307_MGMT_VERSION 1U
#define ML307_MGMT_MAX_FRAME 160U
#define ML307_MGMT_REGISTRATION 1U
#define ML307_MGMT_RESULT 4U
#define ML307_MGMT_OTA_OFFER 5U
#define ML307_MGMT_OTA_ACTIVATE 6U
#define ML307_MGMT_PARAMETERS 12U
#ifndef DEVICE_MAX_PAYLOAD
#define DEVICE_MAX_PAYLOAD 240U
#endif
#define ML307_MGMT_OTA_CANCEL 7U
#define ML307_MGMT_OTA_STATUS 8U
#define ML307_MGMT_STATE 9U
#define ML307_MGMT_CONTROL 10U
#define ML307_MGMT_ALARM 11U
#define ML307_HARDWARE_FAULT_MASK 0x1F9U
#define ML307_PHASE_ONLINE 1U
#define ML307_HTTP_IDLE 0U
#define ML307_HTTP_TERM 7U
#define ML307_ERROR_NONE 0U
#define OTA_STATE_IDLE 0U
#define OTA_STATE_INSTALLING 5U
#define GATEWAY_LORA_REPORT_LENGTH 38U
#define DEVICE_STATUS_OK 0U
#define DEVICE_STATUS_INVALID_ARG 1U
#define DEVICE_STATUS_NOT_SUPPORTED 4U
#define DEVICE_STATUS_IO_ERROR 6U
typedef struct { uint8_t state;uint32_t update_version,downloaded_bytes; } ota_metadata_t;
static ota_metadata_t ota;
static struct {
 uint8_t management_pending,management_length,http_step,ota_retry_count,ota_retry_blocked;
 uint8_t waiting,http_raw_state,http_raw_comma_count,http_read_type,alarm_reported_valid;
 uint8_t configuration_after_publish,control_reply;uint16_t configuration_transaction,control_transaction;
 uint16_t http_header_remaining,http_raw_length,http_raw_received,line_length,alarm_reported_fault;
 uint32_t ota_retry_at_ms,ota_reported_bytes,last_alarm_ms;
 struct { uint8_t last_error,phase;int8_t signal_rssi; } status;
} modem;
static uint8_t management_frame[72],report[38],result;
static uint8_t operation,valid_time;
static uint16_t tag;
static uint32_t parameter,LocalTimestamp=1785987000,CurTick;
static struct { struct { uint16_t u16Val; } errorCode; } Dev;
static const ota_metadata_t *Ota_Get(void){return &ota;}
static uint8_t Ota_Cancel(void){return 0;}
static unsigned reset_requests;
static uint8_t install_result;
static void Peripheral_RequestReset(void){reset_requests++;}
static uint8_t Ota_MarkInstall(void){if(!install_result)ota.state=OTA_STATE_INSTALLING;return install_result;}
static uint8_t Ota_BeginRemote(uint32_t a,uint32_t b,uint32_t c,const char*d,uint8_t e){(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
static void queue_registration(uint16_t t){(void)t;}
static void queue_ota_status(uint16_t t,uint8_t s){(void)t;(void)s;}
static uint8_t RTC_IsTimeValid(void){return valid_time;}
static uint8_t Lora_BuildNodeReportRssi(uint8_t*out,uint8_t t,uint8_t e,int8_t r){(void)t;(void)e;(void)r;memcpy(out,report,38);return 38;}
static uint8_t execute_cloud_control(uint8_t o,uint16_t t,uint32_t p){operation=o;tag=t;parameter=p;return result;}
static uint8_t DeviceService_HandleRemote(uint8_t o,const uint8_t*p,uint16_t n,uint8_t*r,uint16_t*l,uint8_t*a){(void)o;(void)p;(void)n;(void)r;*l=0;*a=0;return 0;}
''' + function((APP / 'gateway_lora_codec.c').read_text(encoding='utf-8'), 'GatewayLora_Checksum') +
            ''.join(function(source, name) for name in ('reached','frame_u16','frame_u32','frame_put16','frame_put32','queue_management_frame','queue_management_result','handle_management')) +
            'static void alarm_step(uint32_t now){\n' + alarm + '\n}\n' + r'''
static void request(uint8_t type,uint16_t transaction,uint8_t length,const uint8_t *payload) {
 uint8_t frame[160]={0xC7,1,0,0};frame[2]=type;frame_put16(frame+4,transaction);frame_put16(frame+6,length);
 if(length)memcpy(frame+8,payload,length);
 frame[8+length]=GatewayLora_Checksum(frame,8+length);
 handle_management(frame,9+length);
}
int main(void){
 uint8_t command[7]={23,0,0,26,0,0,0};
 report[0]=1;report[2]=0x34;report[3]=0x12;report[37]=0xCC;
 request(9,42,0,0);assert(modem.management_length==46&&management_frame[2]==9);
 assert(frame_u16(management_frame+4)==42&&frame_u16(management_frame+6)==37);
 assert(memcmp(management_frame+8,report,37)==0);
 assert(management_frame[45]==GatewayLora_Checksum(management_frame,45));
 operation=0;request(10,43,7,command);assert(operation==0); /* occupied response slot */
 modem.management_pending=0;request(10,43,7,command);assert(operation==23&&tag==0&&parameter==26);
 assert(modem.control_reply==2&&modem.control_transaction==43&&!modem.management_pending);
 modem.control_reply=0; /* actual completion path tested separately */
 result=2;modem.management_pending=0;request(10,44,7,command);assert(management_frame[9]==4);
 modem.management_pending=0;command[0]=26;request(10,45,7,command);assert(operation==23&&management_frame[9]==1);
 modem.management_pending=0;request(10,46,6,command);assert(operation==23&&management_frame[9]==1);
 modem.management_pending=0;modem.status.phase=1;Dev.errorCode.u16Val=2;alarm_step(1);
 assert(management_frame[2]==11&&frame_u16(management_frame+8)==0&&frame_u32(management_frame+10)==0);
 modem.management_pending=0;alarm_step(100);assert(!modem.management_pending);
 Dev.errorCode.u16Val=16;alarm_step(9999);assert(!modem.management_pending);
 valid_time=1;alarm_step(10001);assert(frame_u16(management_frame+8)==16&&frame_u32(management_frame+10)==LocalTimestamp);
 modem.management_pending=0;Dev.errorCode.u16Val=0;alarm_step(10002);assert(!modem.management_pending);
 modem.status.phase=0;alarm_step(21001);assert(!modem.management_pending);
 modem.status.phase=1;alarm_step(21001);assert(frame_u16(management_frame+8)==0);
 modem.management_pending=0;Dev.errorCode.u16Val=16;
 alarm_step(0x80010000U);assert(modem.management_pending && frame_u16(management_frame+8)==16);
 modem.management_pending=0;modem.last_alarm_ms=UINT32_MAX-5000U;Dev.errorCode.u16Val=0;
 alarm_step(6000U);assert(modem.management_pending && frame_u16(management_frame+8)==0);
 uint8_t offer[14]={0};offer[12]=1;offer[13]='x';modem.management_pending=0;
 CurTick=0x90000000U;request(5,47,sizeof(offer),offer);
 assert(modem.ota_retry_at_ms==CurTick && reached(CurTick,modem.ota_retry_at_ms));
 /* Schedule reset immediately after durable activation, without any PUBACK. */
 uint8_t version[4]={0};modem.management_pending=0;
 request(6,48,sizeof(version),version);
 assert(reset_requests==1 && ota.state==OTA_STATE_INSTALLING && modem.management_pending);
 assert(management_frame[2]==4 && management_frame[8]==6 && management_frame[9]==0);
 /* A failed metadata commit must not schedule a reset. */
 modem.management_pending=0;install_result=6;
 request(6,49,sizeof(version),version);assert(reset_requests==1 && management_frame[9]==6);
 return 0;
}
''')

    def test_remote_parameters_reuse_dispatch_and_protect_topology(self):
        source = (APP / 'device_service.c').read_text(encoding='utf-8')
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "device_protocol.h"
#include "config_store.h"
#define OTA_STATE_IDLE 0U
static struct {uint16_t nodeId,parentRelayId;uint8_t channel,linkRole;} Dev={0x1234,0,9,1};
static connectivity_config_t config={.transport_mask=2,.lora_register_sf=9,.lora_register_bw=4,.lora_listen_sf=10,.lora_listen_bw=5};
typedef struct {uint8_t state;} ota_metadata_t;
static ota_metadata_t ota;
const connectivity_config_t *Connectivity_Get(void){return &config;}
static const ota_metadata_t *Ota_Get(void){return &ota;}
static uint16_t get16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static uint8_t last_opcode,dispatches;
static uint8_t dispatch(const device_frame_t *request,uint8_t *response,uint16_t *length,uint8_t *action){
  last_opcode=request->opcode;dispatches++;*length=0;
  if(request->opcode==DEVICE_OP_SET_CONNECTIVITY_CONFIG)*action=1;
  if(request->opcode==DEVICE_OP_GET_CONFIG){memset(response,0,11);*length=11;}
  return DEVICE_STATUS_OK;
}
''' + function(source,'DeviceService_HandleRemote') + r'''
int main(void){
  uint8_t request[240]={0x34,0x12,9,1,0,0,0,0,0,0,255},response[240],action;uint16_t length;
  assert(DeviceService_HandleRemote(DEVICE_OP_COMMIT_CONFIG,request,11,response,&length,&action)==0);
  assert(length==11&&dispatches==2&&last_opcode==DEVICE_OP_GET_CONFIG);
  request[0]++;
  assert(DeviceService_HandleRemote(DEVICE_OP_COMMIT_CONFIG,request,11,response,&length,&action)==DEVICE_STATUS_CONFLICT);
  assert(dispatches==2);request[0]--;
  ota.state=1;
  assert(DeviceService_HandleRemote(DEVICE_OP_SET_RULES,request,1,response,&length,&action)==DEVICE_STATUS_BUSY);
  ota.state=0;
  assert(DeviceService_HandleRemote(DEVICE_OP_RESTART_DEVICE,0,0,response,&length,&action)==0&&action==2);
  assert(DeviceService_HandleRemote(DEVICE_OP_SET_DEVICE_ID,request,1,response,&length,&action)==DEVICE_STATUS_NOT_SUPPORTED);
  assert(DeviceService_HandleRemote(DEVICE_OP_SET_RULES,0,1,response,&length,&action)==DEVICE_STATUS_INVALID_ARG);
  return 0;
}
''')

    def test_shared_storage_commit_never_leaves_failed_install_marker(self):
        source = (APP / 'config_store.c').read_text(encoding='utf-8')
        self.compile_run(r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "config_store.h"
#include "device_protocol.h"
#include "CH58x_common.h"
static uint8_t flash[256];
static unsigned reads,fail_read,writes,fail_write;
uint32_t Test_EepromRead(uint32_t a,void *p,uint32_t n){if(++reads==fail_read)return 1;memcpy(p,flash+a,n);return 0;}
uint32_t Test_EepromWrite(uint32_t a,const void *p,uint32_t n){writes++;memcpy(flash+a,p,n);return writes==fail_write;}
''' + function(source, 'Storage_WriteVerified') + r'''
int main(void){
 uint32_t record[42];for(unsigned i=0;i<42;i++)record[i]=i+123;
 for(unsigned phase=1;phase<=5;phase++){
  memset(flash,255,sizeof(flash));reads=writes=0;fail_read=phase;fail_write=0;
  assert(Storage_WriteVerified(0,record,sizeof(record))!=0);
  assert(memcmp(flash,record,4)!=0);
 }
 for(unsigned phase=1;phase<=2;phase++){
  memset(flash,255,sizeof(flash));reads=writes=0;fail_read=0;fail_write=phase;
  assert(Storage_WriteVerified(0,record,sizeof(record))!=0);
  assert(memcmp(flash,record,4)!=0);
 }
 memset(flash,255,sizeof(flash));reads=writes=fail_read=fail_write=0;
 assert(Storage_WriteVerified(0,record,sizeof(record))==0&&!memcmp(flash,record,sizeof(record)));
 return 0;
}
''')

    def test_manual_control_updates_only_after_uart_completion(self):
        source = (APP / 'device_service.c').read_text(encoding='utf-8')
        constants = ''.join('#define '+n+' '+v+'\n' for n,v in re.findall(
            r'\b(IR_CMD_[A-Z0-9_]+)\s*=\s*(0x[0-9a-fA-F]+)',
            (APP / 'include/ir_tab.h').read_text(encoding='utf-8')))
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "board.h"
#include "device_protocol.h"
#include "device_service.h"
t_dev Dev;uint32_t LocalTimestamp;
static uint8_t control_pending,control_field;
static uint16_t control_value;
static unsigned saved,reports,ble_replies,mqtt_replies,transmitted;
static uint8_t accepted=1,last_status;
uint8_t Ir_ConfiguredCommandSupported(IR_CMD_t c){(void)c;return 1;}
uint8_t Ir_ExecuteConfiguredVerified(IR_CMD_t c){(void)c;transmitted++;return accepted;}
uint8_t Ir_SendLearnedVerified(uint8_t c){(void)c;transmitted++;return accepted;}
void Rule_ManualOverride(void){}
void Rule_RecordPowerChange(void){}
void SaveDevInfo(uint16_t n){assert(n==50);saved++;}
void Ml307_RequestReport(void){reports++;}
void Peripheral_ControlCompleted(uint8_t s){last_status=s;ble_replies++;}
void Ml307_ControlCompleted(uint8_t s){last_status=s;mqtt_replies++;}
''' + constants + ''.join(function(source,n) for n in (
            'update_power_state','update_learned_state','SplitAcControl_Execute',
            'SplitAcControl_IsPending','SplitAcControl_IrCompleted')) + r'''
int main(void){
 Dev.irActType=ACT_TYPE_IR;Dev.onOff=PowerOff;
 assert(SplitAcControl_Execute(CONTROL_FIELD_POWER,1)==0);
 assert(Dev.onOff==PowerOff&&!saved&&SplitAcControl_IsPending());
 assert(SplitAcControl_Execute(CONTROL_FIELD_POWER,0)==DEVICE_STATUS_BUSY&&transmitted==1);
 assert(SplitAcControl_IrCompleted(0));
 assert(Dev.onOff==PowerOff&&!saved&&last_status==DEVICE_STATUS_IO_ERROR);
 assert(!SplitAcControl_IsPending()&&!SplitAcControl_IrCompleted(1));
 assert(ble_replies==1&&mqtt_replies==1);
 assert(SplitAcControl_Execute(CONTROL_FIELD_POWER,1)==0);
 assert(SplitAcControl_IrCompleted(1)&&Dev.onOff==PowerOn&&saved==1&&!last_status);
 assert(Dev.meter.onoff_count==1);
 Dev.irActType=ACT_TYPE_LEARN;Dev.learnCode[6].enable=1;Dev.temSet=25;
 assert(SplitAcControl_Execute(CONTROL_FIELD_LEARNED_CHANNEL,6)==0&&Dev.temSet==25);
 assert(SplitAcControl_IrCompleted(1)&&Dev.temSet==26&&saved==2);
 accepted=0;
 assert(SplitAcControl_Execute(CONTROL_FIELD_POWER,0)==DEVICE_STATUS_BUSY);
 assert(!SplitAcControl_IsPending()&&saved==2&&reports==3);
 return 0;
}
''')

    def test_adc_fallback_conversion_is_bounded_and_restores_configuration(self):
        source = (APP / 'adc.c').read_text(encoding='utf-8')
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#define RB_ADC_START 1U
#define RB_ADC_DATA 4095U
#define RB_ADC_OFS_TEST 2U
#define RB_ADC_DIFF_EN 4U
#define GPIO_Pin_4 16U
#define GPIO_ModeIN_Floating 0U
#define SampleFreq_3_2 0U
#define ADC_PGA_0 0U
static uint32_t CurTick;
static uint8_t R8_ADC_CFG,reg,stuck;
static uint16_t adc_value;
static unsigned conversions;
static uint8_t *conversion_register(void){CurTick+=10U;if(!stuck)reg=0;return &reg;}
#define R8_ADC_CONVERT (*conversion_register())
#define R16_ADC_DATA ((R8_ADC_CFG & RB_ADC_OFS_TEST)?2047U:2000U)
static void GPIOA_ModeCfg(unsigned p,unsigned m){(void)p;(void)m;}
static void ADC_ExtSingleChSampInit(unsigned s,unsigned p){(void)s;(void)p;R8_ADC_CFG=0;}
static void ADC_ChannelCfg(unsigned c){(void)c;conversions++;}
static uint8_t NtcB3950_AdcToTempX10(uint16_t v,int16_t *t){adc_value=v;*t=260;return 1;}
''' + function(source,'adc_convert') + function(source,'ntc_sample') + r'''
int main(void){
 int16_t t=0;uint32_t before;
 assert(ntc_sample(&t)&&t==260&&adc_value==2000&&R8_ADC_CFG==0);
 stuck=1;CurTick=UINT32_MAX-9U;before=CurTick;
 assert(!ntc_sample(&t)&&R8_ADC_CFG==0);
 assert((uint32_t)(CurTick-before)<=40U);
 stuck=0;assert(ntc_sample(&t)&&t==260);
 return 0;
}
''')

    def test_mode_commit_is_busy_during_ir_without_touching_configuration(self):
        source = (APP / 'device_service.c').read_text(encoding='utf-8')
        context = re.search(r'typedef struct.*?staged_config_t;', source, re.S).group()
        case = source[source.index('    case DEVICE_OP_COMMIT_CONFIG:\n'):source.index('    case DEVICE_OP_EXEC_CONTROL:\n')]
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "board.h"
#include "device_protocol.h"
#include "config_store.h"
t_dev Dev;IRBUF_t IrBuf;
static unsigned commits,resets;
static uint8_t idle;
static uint32_t Timer_Lora;
#define LORA_SEC_TO_TICKS(n) (n)
static uint16_t get16(const uint8_t*p){return p[0]|(uint16_t)p[1]<<8;}
uint8_t Ir_PrepareConfigurationChange(void){return idle;}
uint8_t Config_Commit(void){commits++;return 0;}
void Peripheral_RefreshDeviceName(void){}
void Ml307_ApplyConfiguration(void){}
void Rule_Reset(void){resets++;}
''' + context + function(source,'parse_config') + '\nstatic uint8_t commit(const device_frame_t *req){uint8_t status=0;staged_config_t cfg;switch(req->opcode){\n'+case+'}return status;}\n'+r'''
int main(void){
 uint8_t p[11]={0x34,0x12,9,0,0,0,1,0,1,0,0};
 device_frame_t req={0};req.opcode=DEVICE_OP_COMMIT_CONFIG;req.payload=p;req.payload_len=11;
 Dev.nodeId=0x1234;Dev.channel=9;Dev.irType=1;Dev.irActType=ACT_TYPE_IR;
 assert(commit(&req)==DEVICE_STATUS_BUSY&&!commits&&!resets&&Dev.mode==0);
 idle=1;assert(commit(&req)==0&&commits==1&&resets==1&&Dev.mode==1);
 return 0;
}
''')

    def test_mqtt_control_results_wait_for_completion_and_reserve_reply_slot(self):
        source = (APP / 'ml307r.c').read_text(encoding='utf-8')
        context = re.search(r'typedef struct \{.*?\} ml307_context_t;',source,re.S).group()
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "board.h"
#include "ml307r.h"
#include "device_protocol.h"
#define ML307_MGMT_MAGIC 0xC7U
#define ML307_MGMT_VERSION 1U
#define ML307_MGMT_RESULT 4U
#define ML307_MGMT_CONTROL 10U
''' + context + r'''
static ml307_context_t modem;
static uint8_t management_frame[251],field,result;
static uint16_t value;
uint8_t SplitAcControl_Execute(uint8_t f,uint16_t v){field=f;value=v;return result;}
uint8_t ExecuteGatewayControl(uint8_t o,uint16_t t,uint32_t p){(void)o;(void)t;(void)p;return 2;}
static uint8_t GatewayLora_Checksum(const uint8_t*p,uint16_t n){uint8_t s=0xEC;while(n--)s+=*p++;return s;}
''' + ''.join(function(source,n) for n in ('frame_put16','queue_management_frame','queue_management_result','execute_cloud_control','Ml307_ControlCompleted')) + r'''
int main(void){
 assert(execute_cloud_control(21,0,0)==0&&field==CONTROL_FIELD_POWER&&value==1);
 assert(execute_cloud_control(23,0,26)==0&&field==CONTROL_FIELD_TEMPERATURE&&value==260);
 assert(execute_cloud_control(23,1,26)==2&&execute_cloud_control(23,0,999)==2);
 result=DEVICE_STATUS_BUSY;assert(execute_cloud_control(22,0,0)==1);
 modem.control_reply=2;modem.control_transaction=123;
 queue_management_result(10,99,0);assert(!modem.management_pending); /* slot reserved */
 Ml307_ControlCompleted(DEVICE_STATUS_IO_ERROR);
 assert(!modem.control_reply&&modem.management_pending&&modem.status.command_rejected_count==1);
 assert(management_frame[4]==123&&management_frame[8]==10&&management_frame[9]==6);
 modem.management_pending=0;modem.control_reply=1;
 Ml307_ControlCompleted(0);
 assert(modem.pending_command_valid&&!modem.pending_command_result&&modem.status.command_executed_count==1);
 assert(!modem.management_pending&&modem.report_requested);
 Ml307_ControlCompleted(0);assert(modem.status.command_executed_count==1); /* no duplicate completion */
 return 0;
}
''')

    def test_failed_ota_activation_stays_ready_after_power_cycle(self):
        store = (APP/'config_store.c').read_text(encoding='utf-8')
        ota = no_includes((APP/'ota_update.c').read_text(encoding='utf-8'))
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "CH58x_common.h"
#include "config_store.h"
#include "device_protocol.h"
#include "ota_update.h"
static uint8_t df[32768],cf[480*1024];
static unsigned reads,failed_read;
uint32_t Test_EepromRead(uint32_t a,void *p,uint32_t n){if(++reads==failed_read)return 1;memcpy(p,df+a,n);return 0;}
uint32_t Test_EepromWrite(uint32_t a,const void *p,uint32_t n){memcpy(df+a,p,n);return 0;}
uint32_t Test_EepromErase(uint32_t a,uint32_t n){memset(df+a,255,n);return 0;}
uint32_t Test_FlashErase(uint32_t a,uint32_t n){memset(cf+a,255,n);return 0;}
uint32_t Test_FlashWrite(uint32_t a,const void *p,uint32_t n){memcpy(cf+a,p,n);return 0;}
uint32_t Test_FlashVerify(uint32_t a,const void *p,uint32_t n){return memcmp(cf+a,p,n)!=0;}
void Test_FlashRead(uint32_t a,void *p,uint32_t n){memcpy(p,cf+a,n);}
void Storage_ReportFault(uint8_t s,uint8_t f){(void)s;(void)f;}
''' + function(store,'Storage_WriteVerified') + ota + r'''
uint32_t Config_Crc32(const uint8_t *p,uint16_t n){return crc32_update(0xFFFFFFFFU,p,n)^0xFFFFFFFFU;}
int main(void){
 uint8_t image[64];memset(image,0xA5,sizeof(image));
 for(unsigned phase=1;phase<=5;phase++){
  memset(df,255,sizeof(df));memset(cf,255,sizeof(cf));reads=failed_read=0;
  Ota_Init();
  assert(!Ota_BeginLocal(FIRMWARE_BUILD_VERSION+1,sizeof(image),Config_Crc32(image,sizeof(image))));
  assert(!Ota_Write(0,image,sizeof(image))&&!Ota_FinishLocal());
  while(Ota_VerifyStep()==DEVICE_STATUS_BUSY){}
  assert(Ota_Get()->state==OTA_STATE_READY);
  failed_read=reads+phase;
  assert(Ota_MarkInstall()==DEVICE_STATUS_IO_ERROR&&Ota_Get()->state==OTA_STATE_READY);
  failed_read=0;
  Ota_Init();assert(Ota_Get()->state==OTA_STATE_READY); /* reset must not install */
  assert(!Ota_MarkInstall()&&Ota_Get()->state==OTA_STATE_INSTALLING);
 }
 return 0;
}
''')

    def compile_run(self, source):
        gcc = os.environ.get('CC') or shutil.which('gcc')
        if not gcc and Path('D:/w64devkit/w64devkit/bin/gcc.exe').exists():
            gcc = 'D:/w64devkit/w64devkit/bin/gcc.exe'
        self.assertTrue(gcc, 'GCC is required for native recovery regressions')
        with tempfile.TemporaryDirectory(prefix='splitac-recovery-') as directory:
            cfile = Path(directory) / 'test.c'
            exe = Path(directory) / 'test.exe'
            cfile.write_text(source, encoding='utf-8')
            build = subprocess.run([gcc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-function', '-Wno-unused-variable',
                                    f'-DFIRMWARE_BUILD_VERSION={FIRMWARE_BUILD_VERSION}',
                                    '-I' + str(ROOT),
                                    '-I' + str(ROOT / 'tests/stubs'),
                                    '-I' + str(APP / 'include'), str(cfile), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_external_rtc_bus_retention_faults_and_sync(self):
        source = (APP / 'timer.c').read_text(encoding='utf-8')
        self.assertNotIn('RTC_GetTime(', source)
        self.assertNotIn('RTC_InitTime(', source)
        prefix = no_includes(source[:source.index('//主频60M')])
        clock = '\n'.join(function(source, n) for n in (
            'RTC_SetTimestamp', 'Rtc_GetTimestamp', 'RTC_ProductInit',
            'RTC_IsTimeValid', 'RTC_GetWallTime', 'RTC_SyncTimestamp'))
        conversion = no_includes((APP / 'time_utils.c').read_text(encoding='utf-8'))
        modem_codec = no_includes((APP / 'ml307r_codec.c').read_text(encoding='utf-8'))
        network_clock = function((APP / 'ml307r.c').read_text(encoding='utf-8'), 'parse_network_clock')
        policy_clock = function((APP / 'rule.c').read_text(encoding='utf-8'), 'rule_get_time')
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "board.h"
#include "time_utils.h"
#include "ml307r_codec.h"
#define ML307_CLOCK_INTERVAL_MS 21600000U
void Rule_ClockChanged(void) {}
static struct {uint8_t time_synced; uint32_t next_clock_ms;} modem;
#define GPIO_Pin_2 (1U<<2)
#define GPIO_Pin_18 (1U<<18)
#define GPIO_Pin_19 (1U<<19)
#define GPIO_ModeOut_PP_5mA 1
#define GPIO_ModeIN_PD 0
t_dev Dev;
uint32_t LocalTimestamp;
static uint8_t regs[9], pending[8], command, bits, io_out, present=1, fail_write;
static uint32_t pins, delays;
static void mDelayuS(uint32_t n) {assert(n<=4);delays+=n;}
static void GPIOB_ModeCfg(uint32_t mask,int mode) {if(mask&GPIO_Pin_19)io_out=mode;}
static void GPIOB_SetBits(uint32_t mask) {
    if((mask&GPIO_Pin_2) && !(pins&GPIO_Pin_2)) {
        assert(!(pins&GPIO_Pin_18));bits=command=0;memset(pending,0,8);
    }
    pins|=mask;
    if((mask&GPIO_Pin_18) && (pins&GPIO_Pin_2)) {
        uint8_t value=(pins&GPIO_Pin_19)!=0;
        if(bits<8) {assert(io_out);command|=(uint8_t)(value<<bits);}
        else if(!(command&1)) {
            assert(io_out);
            if(bits<72)pending[(bits-8)/8]|=(uint8_t)(value<<((bits-8)%8));
        }
        bits++;
    }
}
static void GPIOB_ResetBits(uint32_t mask) {
    if((mask&GPIO_Pin_18) && (pins&GPIO_Pin_18) && bits==8 && (command&1))
        assert(!io_out); /* no MCU/DS1302 contention on first data bit */
    if((mask&GPIO_Pin_2) && (pins&GPIO_Pin_2) && present && !fail_write && !(command&1)) {
        if(command==0x8e && bits==16)regs[7]=pending[0];
        else if(!(regs[7]&0x80)) {
            if(command==0xbe && bits==72)memcpy(regs,pending,8);
            else if(command==0x90 && bits==16)regs[8]=pending[0];
        }
    }
    pins&=~mask;
}
static uint32_t GPIOB_ReadPortPin(uint32_t mask) {
    assert(mask==GPIO_Pin_19 && !io_out && (pins&GPIO_Pin_2) && !(pins&GPIO_Pin_18));
    assert(command==0xbf && bits>=8 && bits<72);
    return present && ((regs[(bits-8)/8]>>((bits-8)%8))&1);
}
uint32_t Rtc_GetTimestamp(void);
''' + conversion + prefix + clock + modem_codec + network_clock + policy_clock + r'''
int main(void) {
    RTC_ProductInit(); /* responding chip, calendar not initialised */
    assert(!RTC_IsTimeValid() && !Dev.errorCode.bit.rtc && LocalTimestamp==0);
    assert(regs[7]==0x80 && regs[8]==0 && !(pins&GPIO_Pin_2));
    assert(RTC_SetTimestamp(1709164800U)); /* 2024-02-29 UTC */
    assert(regs[3]==0x29 && regs[4]==2 && regs[6]==0x24 && regs[7]==0x80);
    uint16_t y,m,d,h,mi,s;
    assert(RTC_GetWallTime(&y,&m,&d,&h,&mi,&s) && y==2024 && m==2 && d==29 && h==8);
    uint32_t before=delays;
    Rtc_GetTimestamp();assert(before==delays); /* cache: no extra bus traffic */
    RTC_ProductInit();assert(RTC_IsTimeValid() && LocalTimestamp==1709164800U);
    CurTick=1000;regs[0]=1;
    assert(Rtc_GetTimestamp()==1709164801U && RTC_IsTimeValid());
    CurTick=4000;Rtc_GetTimestamp(); /* valid-looking but stopped clock */
    assert(Dev.errorCode.bit.rtc && !RTC_IsTimeValid());
    assert(!RTC_GetWallTime(0,0,0,0,0,0));
    CurTick=5000;regs[0]=5;Rtc_GetTimestamp();
    assert(RTC_IsTimeValid() && !Dev.errorCode.bit.rtc);
    CurTick=6000;present=0;Rtc_GetTimestamp();
    assert(!RTC_IsTimeValid() && Dev.errorCode.bit.rtc);
    assert(!RTC_SetTimestamp(1709164900U));
    present=1;assert(RTC_SetTimestamp(1709164900U));
    fail_write=1;assert(!RTC_SetTimestamp(1709165000U));
    assert(!RTC_IsTimeValid() && Dev.errorCode.bit.rtc);
    fail_write=0;assert(RTC_SetTimestamp(1709165000U));
    CurTick+=1000;regs[0]=0x6a;Rtc_GetTimestamp(); /* invalid BCD */
    assert(!RTC_IsTimeValid() && Dev.errorCode.bit.rtc);
    assert(RTC_SetTimestamp(1709165000U));
    CurTick+=1000;regs[0]|=0x80;Rtc_GetTimestamp(); /* CH */
    assert(!RTC_IsTimeValid() && Dev.errorCode.bit.rtc);
    present=0;RTC_ProductInit();assert(Dev.errorCode.bit.rtc && !RTC_IsTimeValid());
    present=1;assert(RTC_SetTimestamp(1709164800U));
    CurTick=0xfffffff0;assert(RTC_SetTimestamp(1709164800U));
    CurTick+=1000;regs[0]=1;assert(Rtc_GetTimestamp()==1709164801U);
    assert(!RTC_SetTimestamp(1U) && !RTC_SetTimestamp(TIME_UTIL_MAX_TIMESTAMP+1U));
    /* Sunday UTC becomes Monday in Beijing: policies must use local weekday. */
    assert(RTC_SetTimestamp(1791734400U)); /* 2026-10-12 00:00 Beijing */
    uint16_t weekday;
    assert(RTC_GetWallTime(&y,&m,&d,&h,&mi,&s));
    assert(y==2026 && m==10 && d==12 && h==0 && mi==0);
    assert(rule_get_time(&h,&mi,&weekday,&m) && weekday==1 && h==0);
    assert(!RTC_SyncTimestamp(1791734400U-28800U));
    assert(!RTC_SyncTimestamp(1791734400U+28800U));
    assert(Rtc_GetTimestamp()==1791734400U && RTC_IsTimeValid() && !Dev.errorCode.bit.rtc);
    before=delays;assert(RTC_SyncTimestamp(1791734403U));assert(before==delays);
    assert(!RTC_SyncTimestamp(1791734701U)); /* >5 min: stale/wrong network clock */
    assert(!RTC_SyncTimestamp(1U) && !RTC_SyncTimestamp(TIME_UTIL_MAX_TIMESTAMP+1U));
    assert(RTC_SyncTimestamp(1791734700U)); /* bounded drift may be corrected */
    assert(Rtc_GetTimestamp()==1791734700U);
    /* Explicit phone sync remains unrestricted, including correcting old time. */
    assert(RTC_SetTimestamp(1791735870U)); /* 2026-10-12 00:24:30 Beijing */
    const char network[]="+CCLK: \"26/10/11,16:24:30+32\"";
    parse_network_clock(network,sizeof(network)-1);
    assert(Rtc_GetTimestamp()==1791735870U && modem.time_synced);
    const char utc[]="+CCLK: \"26/10/11,16:24:30+00\"";
    parse_network_clock(utc,sizeof(utc)-1);assert(Rtc_GetTimestamp()==1791735870U);
    const char west[]="+CCLK: \"26/10/11,16:24:30-20\"";
    parse_network_clock(west,sizeof(west)-1);assert(Rtc_GetTimestamp()==1791735870U);
    const char no_zone[]="+CCLK: \"26/10/11,16:24:30\"";
    parse_network_clock(no_zone,sizeof(no_zone)-1);assert(Rtc_GetTimestamp()==1791735870U);
    /* A genuinely wrong network calendar must not overwrite phone sync. */
    const char wrong[]="+CCLK: \"26/10/11,08:24:30+32\"";
    parse_network_clock(wrong,sizeof(wrong)-1);
    assert(Rtc_GetTimestamp()==1791735870U && modem.time_synced && !Dev.errorCode.bit.rtc);
    RTC_ProductInit();assert(Rtc_GetTimestamp()==1791735870U); /* battery retention */
    assert(RTC_SetTimestamp(1798732800U)); /* year boundary */
    assert(RTC_GetWallTime(&y,&m,&d,&h,&mi,&s) && y==2027 && m==1 && d==1 && h==0);
    regs[0]|=0x80;rtcPolled=0;Rtc_GetTimestamp();assert(!RTC_IsTimeValid());
    parse_network_clock(network,sizeof(network)-1); /* unset clock: network may initialise */
    assert(Rtc_GetTimestamp()==1791735870U && RTC_IsTimeValid());
    assert(RTC_GetWallTime(&y,&m,&d,&h,&mi,&s));
    assert(y==2026 && m==10 && d==12 && h==0 && mi==24 && s==30);
    assert(rule_get_time(&h,&mi,&weekday,&m) && weekday==1 && h==0 && mi==24);
    /* No signed-2038 truncation; BCD year and Beijing conversion agree. */
    assert(RTC_SetTimestamp(2147483648U));
    assert(RTC_GetWallTime(&y,&m,&d,&h,&mi,&s) && y==2038 && h==11 && mi==14 && s==8);
    assert(RTC_SetTimestamp(3981312000U));
    assert(regs[6]==0x96 && regs[4]==2 && regs[3]==0x29);
    assert(RTC_GetWallTime(&y,&m,&d,&h,&mi,&s) && y==2096 && m==2 && d==29 && h==8);
    assert(RTC_SetTimestamp(4102415999U)); /* final supported Beijing second */
    assert(regs[6]==0x99 && RTC_GetWallTime(&y,&m,&d,&h,&mi,&s));
    assert(y==2099 && m==12 && d==31 && h==23 && mi==59 && s==59);
    assert(rule_get_time(&h,&mi,&weekday,&m) && m==RULE_LAST_DAY);
    assert(RTC_SetTimestamp(TIME_UTIL_MAX_TIMESTAMP));
    assert(!RTC_GetWallTime(&y,&m,&d,&h,&mi,&s)); /* local date would be 2100 */
    RTC_ProductInit();assert(RTC_IsTimeValid() && LocalTimestamp==TIME_UTIL_MAX_TIMESTAMP);
    regs[0]|=0x80;rtcPolled=0;Rtc_GetTimestamp();
    const char late_clock[]="+CCLK: \"99/12/31,15:59:59+32\"";
    parse_network_clock(late_clock,sizeof(late_clock)-1);
    assert(Rtc_GetTimestamp()==4102415999U && RTC_IsTimeValid());
    return 0;
}
''')

    def test_mqtt_topic_matching_uses_variable_device_id_length(self):
        source = (APP / 'ml307r.c').read_text(encoding='utf-8')
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "config_store.h"
static connectivity_config_t config;
static char uid[DEVICE_UID_SIZE];
const connectivity_config_t *Connectivity_Get(void) {return &config;}
const char *DeviceUid_Get(void) {return uid;}
''' + function(source, 'topic_matches') + r'''
int main(void) {
    strcpy(config.subscribe_topic,"sub/ac/{uid}");
    strcpy(uid,"F1234");
    assert(topic_matches(config.subscribe_topic,"sub/ac/F1234",12));
    assert(!topic_matches(config.subscribe_topic,"sub/ac/F1235",12));
    assert(!topic_matches(config.subscribe_topic,"sub/ac/F1234x",13));
    assert(!topic_matches(config.subscribe_topic,"sub/ac/F123",11));
    strcpy(uid,"1234");assert(topic_matches(config.subscribe_topic,"sub/ac/1234",11));
    strcpy(uid,"A26091421");assert(topic_matches(config.subscribe_topic,"sub/ac/A26091421",16));
    return 0;
}
''')

    def test_sensor_polling_survives_stopped_calendar_and_tick_wrap(self):
        source = (APP / 'adc.c').read_text(encoding='utf-8')
        self.assertNotIn('LocalTimestamp', function(source, 'ADC_Pro'))
        self.compile_run(r'''
#include <assert.h>
#include "board.h"
#define AD_INTERVAL 10U
#define SHT30_MEASURE_TIME_MS 15U
#define TEMP_SENSOR_TEMP_VALID 1
#define TEMP_SENSOR_HUMIDITY_VALID 2
#define TEMP_SENSOR_SHT30_ACTIVE 4
#define TEMP_SENSOR_NTC_FALLBACK 8
#define TEMP_SENSOR_SHT30_FAULT 16
t_dev Dev;
volatile uint32_t CurTick;
static uint8_t adcValid,sensorStatus,sht30Pending;
static uint16_t humidityX10,sht30Errors;
static uint32_t sht30StartedMs;
static uint8_t sht30_begin(void) {return 0;}
static uint8_t sht30_read(int16_t *t,uint16_t *h) {(void)t;(void)h;return 0;}
static uint8_t ntc_sample(int16_t *t) {*t=260;return 1;}
''' + function(source, 'ADC_Pro') + r'''
int main(void) {
    ADC_Pro();assert(sht30Errors==1 && adcValid);
    CurTick=9999;ADC_Pro();assert(sht30Errors==1);
    CurTick=10000;ADC_Pro();assert(sht30Errors==2);
    CurTick=0xfffffff0;ADC_Pro();assert(sht30Errors==3);
    CurTick+=9999;ADC_Pro();assert(sht30Errors==3);
    CurTick++;ADC_Pro();assert(sht30Errors==4 && Dev.roomTempX10==260);
    return 0;
}
''')

    def test_meter_energy_failure_is_not_cleared_by_voltage_success(self):
        source = (APP / 'hlw8110.c').read_text(encoding='utf-8')
        prefix = no_includes(source[:source.index('static void uart_configure')])
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "board.h"
#include "BLE/Peripheral/APP/include/hlw8110.h"
#undef PRINT
#define PRINT(...) test_print(__VA_ARGS__)
static void test_print(const char *format, ...) {(void)format;}
t_dev Dev;
static uint8_t R8_UART0_TFC;
''' + prefix + function(source, 'record_failure') + r'''
static hlw_rx_result_t read_result = HLW_RX_OK;
static uint8_t start_read(uint8_t r, uint8_t b, uint32_t n) {(void)r;(void)b;(void)n;return 1;}
static hlw_rx_result_t poll_response(uint32_t n) {(void)n;return read_result;}
static void begin_recovery(uint32_t n, uint8_t r, uint32_t d) {(void)n;record_failure(r,d);meter_state=HLW_STATE_RESET_LOW;}
static void reject_sample(uint32_t n, uint8_t r, uint32_t d) {begin_recovery(n,r,d);}
uint16_t HLW8110_CalcPowerX10(uint32_t r, uint16_t c) {(void)r;(void)c;return 10;}
uint8_t HLW8110_CalcCurrentMa(uint32_t r, uint16_t c, uint16_t *v) {(void)r;(void)c;*v=100;return 1;}
uint8_t HLW8110_CalcVoltageDv(uint32_t r, uint16_t c, uint16_t *v) {(void)r;(void)c;*v=2200;return 1;}
uint64_t HLW8110_CalcEnergyTenthWattSeconds(uint32_t r,uint16_t c,uint16_t h,uint64_t *f) {(void)c;(void)h;(void)f;return r;}
''' + function(source, 'service_read_state') + r'''
int main(void) {
    meter_status.valid=1;
    sample_power_w_x10=100;
    for(uint8_t i=0;i<3;i++) {
        meter_state=HLW_STATE_SAMPLE_VOLTAGE;request_active=1;read_result=HLW_RX_OK;
        service_read_state(HLW_REG_RMS_U,3,i*1000);
        assert(meter_status.consecutive_errors==i);
        read_result=HLW_RX_FAILED;rx_failure_reason=HLW8110_ERROR_TIMEOUT;
        service_read_state(HLW_REG_ENERGY_PA,3,i*1000+100);
    }
    assert(!meter_status.valid && Dev.errorCode.bit.power);
    assert(meter_status.consecutive_errors==3);
    meter_state=HLW_STATE_SAMPLE_VOLTAGE;read_result=HLW_RX_OK;
    service_read_state(HLW_REG_RMS_U,3,4000);
    assert(!meter_status.valid && Dev.errorCode.bit.power);
    service_read_state(HLW_REG_ENERGY_PA,3,4100);
    assert(meter_status.valid && !Dev.errorCode.bit.power);
    assert(meter_status.consecutive_errors==0 && meter_status.last_sample_ms==4100);
    return 0;
}
''')

    def test_meter_response_accepts_ff_coefficients_only_with_valid_checksum(self):
        source = (APP / 'hlw8110.c').read_text(encoding='utf-8')
        prefix = no_includes(source[:source.index('static void uart_configure')])
        math = (APP / 'hlw8110_math.c').read_text(encoding='utf-8')
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#include "board.h"
#include "BLE/Peripheral/APP/include/hlw8110.h"
#define STA_ERR_BREAK 1U
#define STA_ERR_FRAME 2U
#define STA_ERR_PAR 4U
#define STA_ERR_FIFOOV 8U
static uint8_t R8_UART0_RFC;
static uint8_t UART0_GetLinSTA(void) {return 0;}
static uint8_t UART0_RecvByte(void) {return 0;}
#define UART0_CLR_RXFIFO() ((void)0)
''' + prefix + function(math, 'HLW8110_UartChecksum') + function(source, 'poll_response') + r'''
int main(void) {
    uint8_t data[2]={0xFF,0xFF};
    const uint8_t registers[]={HLW_REG_ENERGY_AC,HLW_REG_RMS_IAC,HLW_REG_RMS_UC,HLW_REG_POWER_PAC};
    for(unsigned i=0;i<sizeof(registers);i++) {
        pending_command=registers[i];response_expected=response_count=3;request_active=1;
        response[0]=response[1]=0xFF;
        response[2]=HLW8110_UartChecksum(pending_command,data,2);
        assert(poll_response(0)==HLW_RX_OK);
        response[2]^=1;
        assert(poll_response(0)==HLW_RX_FAILED && rx_failure_reason==HLW8110_ERROR_CHECKSUM);
    }
    return 0;
}
''')

    def test_http_and_mqtt_binary_receive_are_demultiplexed(self):
        source = (APP / 'ml307r.c').read_text(encoding='utf-8')
        context = re.search(r'typedef struct \{.*?\} ml307_context_t;', source, re.S).group()
        codec = no_includes((APP / 'ml307r_codec.c').read_text(encoding='utf-8'))
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "ml307r.h"
#include "ml307r_codec.h"
#include "device_protocol.h"
#include "ota_update.h"
#define ML307_RX_RING_SIZE 512U
#define ML307_MGMT_MAX_FRAME 160U
#define ML307_COMMAND_TIMEOUT_MS 3000U
#define ML307_HTTP_IDLE 0U
#define ML307_HTTP_READ 6U
#define ML307_HTTP_TERM 7U
#define ML307_HTTP_DELETE 8U
#define ML307_HTTP_RETRY_LIMIT 3U
''' + context + r'''
static ml307_context_t modem;
static uint32_t CurTick;
static uint16_t rx_head,rx_tail;
static uint8_t rx_ring[512],http_raw_data[240];
static char rx_line[512];
static unsigned commands,writes,statuses;
static ota_metadata_t ota={.state=OTA_STATE_DOWNLOADING,.url_length=1};
static uint8_t command[18]={0,13,10,',','>',0xFF,1,2,3,4,5,6,7,8,9,0,0,0};
static uint8_t body[8]={0,13,10,',','>',0xFF,9,0};
const ota_metadata_t *Ota_Get(void) {return &ota;}
uint8_t Ota_Write(uint32_t o,const uint8_t *d,uint16_t n) {assert(o==0 && n==8 && !memcmp(d,body,8));writes++;return 0;}
typedef struct {const char *subscribe_topic,*management_topic;} connectivity_config_t;
static connectivity_config_t config={"sub","manage"};
static const connectivity_config_t *Connectivity_Get(void) {return &config;}
static uint8_t topic_matches(const char *p,const char *t,uint8_t n) {return !strcmp(p,"sub") && n==11 && !memcmp(t,"sub/ac/1234",11);}
static void queue_ota_status(uint16_t t,uint8_t s) {(void)t;(void)s;statuses++;}
static uint32_t http_retry_delay(uint8_t n) {(void)n;return 2000;}
static void handle_mqtt_payload(const uint8_t *p,uint16_t n) {assert(n==18 && !memcmp(p,command,18));commands++;}
static uint8_t send_publish_payload(void) {return 1;}
static void enter_backoff(uint8_t e) {(void)e;assert(0);}
static void process_line(char *l,uint16_t n) {(void)n;if(!strcmp(l,"OK"))modem.http_raw_state=0;}
''' + codec + '\n'.join(function(source,n) for n in (
    'http_fail_status','http_fail','parse_http_read_header','mqtt_header_complete','mqtt_begin_payload','consume_uart')) + r'''
static void feed(const uint8_t *p,unsigned n) {
    for(unsigned i=0;i<n;i++) {
        rx_ring[rx_head]=p[i];rx_head=(uint16_t)((rx_head+1)%512);
        consume_uart(); /* 每次只来一个字节，覆盖任意 UART 分段。 */
    }
}
static void mqtt(void) {
    const char *header="+MQTTURC: \"publish\",0,0,\"sub/ac/1234\",18,18,";
    feed((const uint8_t *)header,strlen(header));feed(command,sizeof(command));
    feed((const uint8_t *)"\r\n",2);
}
int main(void) {
    const char *header="+MHTTPREAD: 0,1,0,8,";
    modem.http_step=ML307_HTTP_READ;modem.http_raw_state=1;
    modem.http_read_type=1;modem.http_range_size=4096;
    mqtt();assert(commands==1 && !writes && modem.http_raw_state==1);
    feed((const uint8_t *)header,strlen(header));feed(body,sizeof(body));
    assert(writes==1 && modem.http_raw_state==3);
    mqtt();assert(commands==2 && modem.http_raw_state==3);
    feed((const uint8_t *)"\r\nOK\r\n",6);assert(!modem.http_raw_state);
    /* HTTP 头失败不得清掉环形缓存里随后到达的合法 MQTT 下行。 */
    modem.http_raw_state=1;modem.http_step=ML307_HTTP_READ;modem.http_raw_comma_count=0;
    const char *bad="+MHTTPREAD: 0,1,0,0,\r\n+MQTTURC: \"publish\",0,0,\"sub/ac/1234\",18,18,";
    memcpy(rx_ring,bad,strlen(bad));memcpy(rx_ring+strlen(bad),command,18);
    rx_tail=0;rx_head=(uint16_t)(strlen(bad)+18);consume_uart();
    assert(commands==3 && modem.status.last_error==ML307_ERROR_OTA);
    assert(modem.http_step==ML307_HTTP_TERM && !modem.http_raw_state);
    return 0;
}
''')

    def test_mqtt_inflight_snapshot_survives_new_response_and_retry(self):
        source = (APP / 'ml307r.c').read_text(encoding='utf-8')
        context = re.search(r'typedef struct \{.*?\} ml307_context_t;', source, re.S).group()
        names = ('start_publish', 'send_publish_payload', 'queue_management_frame', 'complete_publish', 'enter_backoff')
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "ml307r.h"
#include "device_protocol.h"
#define ML307_MQTT_FRAME_SIZE 38U
#define ML307_HTTP_IDLE 0U
#define ML307_HTTP_TERM 7U
#define ML307_MGMT_RESULT 4U
#define ML307_MGMT_OTA_ACTIVATE 6U
#define ML307_MGMT_PARAMETERS 12U
#ifndef DEVICE_MAX_PAYLOAD
#define DEVICE_MAX_PAYLOAD 240U
#endif
#define ML307_MGMT_MAGIC 0xC7U
#define ML307_MGMT_VERSION 1U
''' + context + r'''
static ml307_context_t modem;
static uint8_t management_frame[251],publishing_management_frame[251];
static char tx_buffer[251];
static uint32_t CurTick;
static uint16_t device_jitter_ms;
static unsigned declared_length,resets,configuration_applied,remote_resets;
typedef struct {uint8_t mqtt_qos;const char *publish_topic,*management_topic;} connectivity_config_t;
static connectivity_config_t config={1,"pub/ac/test","mag/ac/test"};
static const connectivity_config_t *Connectivity_Get(void) {return &config;}
static uint8_t build_publish_frame(uint8_t *f) {memset(f,0,38);return 38;}
static uint8_t tx_append_text(uint16_t *l,const char *t) {(void)l;(void)t;return 1;}
static uint8_t tx_append_topic(uint16_t *l,const char *t) {(void)l;assert(!strcmp(t,modem.publishing_management?"mag/ac/test":"pub/ac/test"));return 1;}
static uint8_t tx_append_u32(uint16_t *l,uint32_t v) {(void)l;declared_length=v;return 1;}
static uint8_t tx_start(uint16_t l) {(void)l;return 1;}
static void transition_wait(uint8_t p,uint32_t d) {(void)d;modem.status.phase=p;}
static void frame_put16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static uint8_t GatewayLora_Checksum(const uint8_t *p,uint16_t l) {uint8_t s=0;while(l--)s+=*p++;return s;}
static void status_phase(uint8_t p) {modem.status.phase=p;}
static void uart_disable(void) {}
void Ml307_ApplyConfiguration(void) {configuration_applied++;}
static void Peripheral_RequestReset(void) {remote_resets++;}
#define SYS_DisableAllIrq(p) ((void)(p))
#define mDelaymS(ms) ((void)(ms))
#define SYS_ResetExecute() (++resets)
''' + '\n'.join(function(source, name) for name in names) + r'''
int main(void) {
    uint8_t payload[12]={0};
    modem.next_report_ms=62002;modem.periodic_report_pending=1;
    queue_management_frame(8,1,payload,12);
    assert(management_frame[3]==1);
    assert(start_publish() && declared_length==21);
    assert(modem.periodic_report_pending && modem.next_report_ms==62002);
    queue_management_frame(4,2,payload,2);
    queue_management_frame(4,3,payload,2); /* 满槽不能覆盖已接受的下一条响应 */
    assert(management_frame[4]==2);
    assert(send_publish_payload() && (uint8_t)tx_buffer[4]==1);
    enter_backoff(ML307_ERROR_MQTT);
    assert(modem.publishing_management && modem.management_pending);
    assert(start_publish() && declared_length==21);
    assert(send_publish_payload() && (uint8_t)tx_buffer[4]==1);
    complete_publish();
    assert(!resets && modem.management_pending);
    assert(start_publish() && declared_length==11);
    assert(send_publish_payload() && (uint8_t)tx_buffer[4]==2);
    complete_publish();
    assert(!modem.management_pending && !resets);
    payload[0]=6;payload[1]=0;
    queue_management_frame(4,4,payload,2);
    assert(start_publish());complete_publish();
    assert(!resets); /* PUBACK never owns the already-scheduled OTA reset. */
    modem.pending_command_valid=1;modem.pending_command_result=1;
    assert(start_publish() && modem.periodic_report_pending);
    complete_publish();assert(modem.periodic_report_pending && modem.next_report_ms==62002);
    modem.report_requested=1;
    assert(start_publish() && !modem.periodic_report_pending);
    CurTick=9000;complete_publish();
    assert(modem.status.last_report_ms==9000 && modem.next_report_ms==62002);
    modem.configuration_after_publish=1;modem.configuration_transaction=99;
    payload[0]=85;payload[1]=0;
    queue_management_frame(12,98,payload,2);assert(start_publish());complete_publish();
    assert(!configuration_applied&&modem.configuration_after_publish==1);
    payload[1]=5;
    queue_management_frame(12,99,payload,2);assert(start_publish());complete_publish();
    assert(!configuration_applied&&modem.configuration_after_publish==1);
    payload[1]=0;
    queue_management_frame(12,99,payload,2);assert(start_publish());complete_publish();
    assert(configuration_applied==1&&!modem.configuration_after_publish);
    modem.configuration_after_publish=2;modem.configuration_transaction=100;
    management_frame[8]=85;management_frame[9]=0;
    for(unsigned i=10;i<250;i++)management_frame[i]=(uint8_t)i;
    queue_management_frame(12,100,management_frame+8,242);
    assert(modem.management_length==251&&management_frame[249]==249);
    assert(start_publish()&&declared_length==251&&send_publish_payload());
    assert((uint8_t)tx_buffer[249]==249);complete_publish();
    assert(remote_resets==1&&!modem.configuration_after_publish);
    return 0;
}
''')

    def test_mqtt_fixed_report_cadence_clock_changes_and_tick_wrap(self):
        source = (APP / 'ml307r.c').read_text(encoding='utf-8')
        context = re.search(r'typedef struct \{.*?\} ml307_context_t;', source, re.S).group()
        process = function(source, 'Ml307_Process')
        self.assertLess(process.index('poll_report_schedule(now)'), process.index('if(modem.http_step != ML307_HTTP_IDLE)'))
        self.assertNotIn('now - modem.status.last_report_ms', process)
        self.assertGreater(process.rindex('poll_rule_records();'), process.index('if(modem.tx_length != 0U) return;'))
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "ml307r.h"
''' + context + r'''
static ml307_context_t modem;
static struct {uint16_t report_interval_sec;} config={60};
static struct {uint16_t nodeId;} Dev={2};
static uint32_t CurTick, clock_tick, clock_sec;
static uint8_t clock_valid;
#define Connectivity_Get() (&config)
static uint32_t Rtc_GetTimestamp(void) {return clock_sec+(uint32_t)(CurTick-clock_tick)/1000U;}
static uint8_t RTC_IsTimeValid(void) {return clock_valid;}
''' + '\n'.join(function(source, n) for n in ('reached', 'poll_report_schedule')) + r'''
int main(void) {
    poll_report_schedule(0);assert(modem.next_report_ms==2002 && !modem.periodic_report_pending);
    CurTick=2001;poll_report_schedule(CurTick);assert(!modem.periodic_report_pending);
    CurTick=2002;poll_report_schedule(CurTick);
    assert(modem.periodic_report_pending && modem.next_report_ms==62002);
    modem.periodic_report_pending=0;modem.status.last_report_ms=41000;modem.report_requested=1;
    CurTick=62002;poll_report_schedule(CurTick);assert(modem.periodic_report_pending && modem.next_report_ms==122002);
    CurTick=302002;poll_report_schedule(CurTick);assert(modem.periodic_report_pending && modem.next_report_ms==362002);
    modem.periodic_report_pending=0;CurTick++;poll_report_schedule(CurTick);assert(!modem.periodic_report_pending);
    /* No RTC: a monotonic deadline survives repeated uint32 tick wrap. */
    memset(&modem,0,sizeof(modem));CurTick=UINT32_MAX-10000U;
    poll_report_schedule(CurTick);
    uint32_t deadline=modem.next_report_ms;
    for(unsigned i=0;i<72000;i++) {
        CurTick=deadline-1;poll_report_schedule(CurTick);assert(!modem.periodic_report_pending);
        CurTick=deadline;poll_report_schedule(CurTick);assert(modem.periodic_report_pending);
        deadline+=60000;assert(modem.next_report_ms==deadline);modem.periodic_report_pending=0;
    }
    /* RTC anchors the slot; reconnect/report completion cannot move it. */
    memset(&modem,0,sizeof(modem));Dev.nodeId=0x1234;clock_valid=1;
    clock_sec=1800000000U;clock_tick=CurTick=10000;
    poll_report_schedule(CurTick);deadline=50660;assert(modem.next_report_ms==deadline);
    for(CurTick=11000;CurTick<50660;CurTick+=1000) {
        poll_report_schedule(CurTick);assert(modem.next_report_ms==deadline && !modem.periodic_report_pending);
    }
    CurTick=50660;poll_report_schedule(CurTick);assert(modem.periodic_report_pending && modem.next_report_ms==110660);
    modem.periodic_report_pending=0;CurTick+=1000;
    clock_sec=1800000300U;clock_tick=CurTick;poll_report_schedule(CurTick);
    assert(modem.next_report_ms==CurTick+40660 && !modem.periodic_report_pending);
    clock_sec-=3600;poll_report_schedule(CurTick);assert(!modem.periodic_report_pending);
    clock_valid=0;poll_report_schedule(CurTick);
    uint32_t wait=(40660U+60000U-CurTick%60000U)%60000U;
    assert(modem.next_report_ms==CurTick+wait);
    clock_valid=1;config.report_interval_sec=30;Dev.nodeId=0xffff;clock_tick=CurTick;
    poll_report_schedule(CurTick);assert(modem.next_report_ms==CurTick+15535);
    config.report_interval_sec=0;poll_report_schedule(CurTick); /* no division by zero */
    return 0;
}
''')

    def test_mqtt_management_topic_isolation_and_response_echo(self):
        source = (APP / 'ml307r.c').read_text(encoding='utf-8')
        context = re.search(r'typedef struct \{.*?\} ml307_context_t;', source, re.S).group()
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "ml307r.h"
#include "device_protocol.h"
#include "ota_update.h"
#define ML307_MGMT_MAGIC 0xC7U
#define ML307_MGMT_VERSION 1U
#define ML307_MGMT_MAX_FRAME 160U
#define ML307_MGMT_REGISTRATION 1U
#define ML307_MGMT_OTA_OFFER 5U
#define ML307_MGMT_OTA_ACTIVATE 6U
#define ML307_MGMT_PARAMETERS 12U
#ifndef DEVICE_MAX_PAYLOAD
#define DEVICE_MAX_PAYLOAD 240U
#endif
#define ML307_MGMT_OTA_CANCEL 7U
#define ML307_MGMT_OTA_STATUS 8U
#define ML307_HTTP_IDLE 0U
#define ML307_MGMT_STATE 9U
#define ML307_MGMT_CONTROL 10U
#define GATEWAY_LORA_REPORT_LENGTH 38U
#define ML307_HTTP_TERM 7U
''' + context + r'''
static ml307_context_t modem;
static ota_metadata_t ota;
static unsigned controls, registrations, statuses, results, cancellations;
static uint32_t CurTick;
static uint8_t management_frame[251];
const ota_metadata_t *Ota_Get(void) {return &ota;}
uint8_t Ota_BeginRemote(uint32_t v,uint32_t n,uint32_t c,const char *u,uint8_t l) {(void)v;(void)n;(void)c;(void)u;(void)l;return 0;}
uint8_t Ota_MarkInstall(void) {return 0;}
static void Peripheral_RequestReset(void) {}
uint8_t Ota_Cancel(void) {cancellations++;return 0;}
static void handle_downlink(const uint8_t *p,uint16_t n) {(void)p;(void)n;controls++;}
static uint8_t GatewayLora_Checksum(const uint8_t *p,uint16_t n) {uint8_t c=0xec;while(n--)c+=*p++;return c;}
static void queue_registration(uint16_t t) {assert(t==3);registrations++;}
static void queue_ota_status(uint16_t t,uint8_t s) {assert(t==3 && s==0);statuses++;}
static void queue_management_result(uint8_t type,uint16_t t,uint8_t s) {(void)type;(void)t;(void)s;results++;}
static void queue_management_frame(uint8_t type,uint16_t t,const uint8_t *p,uint8_t n) {(void)type;(void)t;(void)p;(void)n;}
static uint8_t Lora_BuildNodeReportRssi(uint8_t *p,uint8_t t,uint8_t e,int8_t r) {(void)t;(void)e;(void)r;memset(p,0,38);return 38;}
static uint8_t execute_cloud_control(uint8_t o,uint16_t t,uint32_t p) {(void)o;(void)t;(void)p;return 0;}
static uint8_t DeviceService_HandleRemote(uint8_t o,const uint8_t*p,uint16_t n,uint8_t*r,uint16_t*l,uint8_t*a){(void)o;(void)p;(void)n;(void)r;*l=0;*a=0;return 0;}
''' + '\n'.join(function(source,n) for n in ('frame_u16','frame_u32','handle_management','handle_mqtt_payload')) + r'''
static void request(uint8_t *f,uint8_t type,uint8_t flags) {
    memset(f,0,9);f[0]=0xc7;f[1]=1;f[2]=type;f[3]=flags;f[4]=3;f[8]=GatewayLora_Checksum(f,8);
}
int main(void) {
    uint8_t frame[9], control[18]={0x0d};
    request(frame,7,0);modem.mqtt_raw_accept=1;handle_mqtt_payload(frame,9);
    assert(!cancellations && !results); /* management forbidden on sub */
    handle_mqtt_payload(control,18);assert(controls==1);
    modem.mqtt_raw_accept=2;handle_mqtt_payload(control,18);assert(controls==1);
    request(frame,1,0);handle_mqtt_payload(frame,9);assert(registrations==1);
    request(frame,8,0);handle_mqtt_payload(frame,9);assert(statuses==1);
    request(frame,7,0);handle_mqtt_payload(frame,9);assert(cancellations==1 && results==1);
    for(unsigned i=0;i<1000;i++) {
        request(frame,1,1);handle_mqtt_payload(frame,9);
        request(frame,8,1);handle_mqtt_payload(frame,9);
        request(frame,4,1);handle_mqtt_payload(frame,9);
    }
    assert(registrations==1 && statuses==1 && results==1);
    modem.management_pending=1;request(frame,7,0);handle_mqtt_payload(frame,9);assert(cancellations==1);
    modem.management_pending=0;frame[8]++;handle_mqtt_payload(frame,9);assert(cancellations==1);
    return 0;
}
''')

    def test_activation_uses_existing_bounded_reset_event(self):
        source = (APP / 'peripheral.c').read_text(encoding='utf-8')
        reset_event = source[source.index('    if(events & SBP_DEVICE_RESET_EVT)'):source.index('    if(events & SBP_TX_FRAME_EVT)')]
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#define SBP_DEVICE_RESET_EVT 1U
#define MS1_TO_SYSTEM_TIME(ms) (ms)
static uint8_t Peripheral_TaskID=7;
static unsigned scheduled,delay,resets,disabled;
static void tmos_start_task(uint8_t task,uint16_t event,uint32_t ms) {
 assert(task==7 && event==SBP_DEVICE_RESET_EVT);scheduled++;delay=ms;
}
static void DisableAllIRQ(void) {disabled++;}
static void SYS_ResetExecute(void) {resets++;}
''' + function(source, 'Peripheral_RequestReset') +
            'static uint16_t event_step(uint16_t events){\n' + reset_event + 'return events;\n}\n' + r'''
int main(void) {
 Peripheral_RequestReset();assert(scheduled==1 && delay==600 && !resets);
 /* TMOS dispatch requires no modem state, connection or publish completion. */
 assert(event_step(SBP_DEVICE_RESET_EVT)==0 && resets==1 && disabled==1);
 return 0;
}
''')

    def test_mqtt_long_lived_deadlines_are_consumed(self):
        source = (APP / 'ml307r.c').read_text(encoding='utf-8')
        process = function(source, 'Ml307_Process')
        hold = process[process.index('    if(modem.at_status.state != ML307_AT_IDLE && !modem.at_result_read)'):process.index('    if(modem.http_step != ML307_HTTP_IDLE)')]
        online = process[process.index('    case ML307_PHASE_ONLINE:')+len('    case ML307_PHASE_ONLINE:'):process.index('        if(modem.publishing_management || modem.management_pending ||')]
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#define ML307_AT_IDLE 0U
#define ML307_AT_TIMEOUT_MS 5000U
#define ML307_AT_RESULT_HOLD_MS 3000U
static struct {struct {uint8_t state;} at_status;uint8_t at_result_read;uint32_t at_started_ms,next_action_ms;} modem;
static unsigned passed;
''' + function(source, 'reached') + '\nstatic void hold_result(uint32_t now){\n' + hold + '\npassed++;}\nstatic void online_gate(uint32_t now){\n' + online + r'''
}
int main(void) {
    modem.at_status.state=2;modem.at_started_ms=UINT32_MAX-1000U;
    hold_result(6998U);assert(!passed && !modem.at_result_read);
    hold_result(6999U);assert(passed==1 && modem.at_result_read);
    hold_result(0x80010000U);assert(passed==2); /* never re-block after 24.9 days */
    modem.next_action_ms=2002U;
    online_gate(2001U);assert(!reached(2001U,modem.next_action_ms));
    online_gate(2002U);assert(reached(2002U,modem.next_action_ms));
    uint32_t now=2002U;
    for(unsigned minute=0;minute<180000;minute++) {
        now+=60000U;online_gate(now);assert(reached(now,modem.next_action_ms));
    }
    return 0;
}
''')

    def test_mqtt_http_connection_events_and_signal_queries(self):
        source = (APP / 'ml307r.c').read_text(encoding='utf-8')
        context = re.search(r'typedef struct \{.*?\} ml307_context_t;', source, re.S).group()
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "ml307r.h"
#define ML307_HTTP_IDLE 0U
#define ML307_HTTP_TERM 7U
#define ML307_MQTT_TIMEOUT_MS 15000U
#define ML307_COMMAND_TIMEOUT_MS 3000U
#define ML307_SIGNAL_INTERVAL_MS 60000U
''' + context + r'''
static ml307_context_t modem;
static uint32_t CurTick;
static uint16_t device_jitter_ms;
static unsigned http_calls, registrations, publish_completions;
typedef struct {uint8_t mqtt_qos; const char *subscribe_topic,*management_topic;} connectivity_config_t;
static connectivity_config_t config={1,"sub/ac/test","mag/ac/test"};
static const connectivity_config_t *Connectivity_Get(void) {return &config;}
static void uart_disable(void) {}
void Ml307_ApplyConfiguration(void) {}
static void Peripheral_RequestReset(void) {}
#define ML307_SYNC_RETRY_MS 100U
#define ML307_SYNC_TIMEOUT_MS 400U
#define ML307_SIM_RECHECK_MS 5000U
#define ML307_SIM_RESET_LIMIT 14U
static uint8_t at_sync_failures,sim_failures,sim_check_pending;
static unsigned sim_power_cycles;
static struct {struct {struct {uint8_t cellular;} bit;} errorCode;} Dev;
static void start_power_cycle(void) {sim_power_cycles++;modem.waiting=0;modem.status.phase=ML307_PHASE_POWER_OFF;}
static uint8_t at_process_line(const char *l,uint16_t n) {(void)l;(void)n;return 0;}
static uint8_t http_process_line(char *l,uint16_t n) {(void)l;(void)n;if(!modem.http_step)return 0;http_calls++;return 1;}
static void complete_mqtt_connection(void) {}
static void queue_registration(uint16_t t) {(void)t;registrations++;}
static void complete_publish(void) {publish_completions++;modem.waiting=0;modem.status.phase=ML307_PHASE_ONLINE;}
static uint8_t copy_numeric_identity(char *o,uint8_t c,const char *l,uint8_t n,uint8_t m) {(void)o;(void)c;(void)l;(void)n;(void)m;return 1;}
static void parse_network_clock(const char *l,uint16_t n) {(void)l;(void)n;}
static uint8_t tx_command(const char *l) {(void)l;return 1;}
static char subscription[192];
static uint8_t tx_append_text(uint16_t *n,const char *t) {strcpy(subscription+*n,t);*n+=(uint16_t)strlen(t);return 1;}
static uint8_t tx_append_topic(uint16_t *n,const char *t) {return tx_append_text(n,t);}
static uint8_t tx_start(uint16_t n) {(void)n;assert(!strcmp(subscription,modem.mqtt_subscribe_step?
    "AT+MQTTSUB=0,\"mag/ac/test\",1\r\n":"AT+MQTTSUB=0,\"sub/ac/test\",0\r\n"));return 1;}
''' + '\n'.join(function(source,n) for n in (
    'mqtt_urc_is','mqtt_urc_number','parse_cereg','parse_signal','parse_extended_signal',
    'status_phase','transition_wait','send_mqtt_subscribe','enter_backoff','complete_mqtt_subscription',
    'record_at_response','sim_probe_result','mqtt_check_ack','process_line')) + r'''
static void online_http(void) {
    memset(&modem,0,sizeof(modem));http_calls=0;
    modem.status.phase=ML307_PHASE_ONLINE;modem.status.mqtt_online=1;
    modem.http_step=5;modem.http_raw_state=1;modem.waiting=1;
}
int main(void) {
    char down[]="+MQTTURC: \"conn\",0,2";
    char reconnect[]="+MQTTURC: \"conn\",0,1";
    char network[]="+CEREG: 0,0";
    char stale_suback[]="+MQTTURC: \"suback\",0,12,1";
    online_http();process_line(down,strlen(down));
    assert(!http_calls && !modem.status.mqtt_online && modem.status.phase==ML307_PHASE_BACKOFF);
    assert(modem.http_step==ML307_HTTP_TERM && !modem.http_raw_state);
    online_http();process_line(reconnect,strlen(reconnect));
    assert(!http_calls && modem.status.phase==ML307_PHASE_BACKOFF);
    online_http();process_line(network,strlen(network));
    assert(!http_calls && !modem.status.mqtt_online && modem.status.last_error==ML307_ERROR_NETWORK);
    assert(modem.http_step==ML307_HTTP_IDLE && modem.status.phase==ML307_PHASE_BACKOFF);
    online_http();process_line(stale_suback,strlen(stale_suback));
    assert(!http_calls && modem.status.phase==ML307_PHASE_ONLINE && modem.waiting);
    memset(&modem,0,sizeof(modem));
    char csq[]="+CSQ: 14,99", cesq[]="+CESQ: 99,99,255,255,25,40", ok[]="OK", error[]="ERROR";
    modem.status.phase=ML307_PHASE_SIGNAL_QUERY;modem.network_command=2;
    parse_signal(csq);assert(modem.status.signal_rssi==-85);
    process_line(ok,2);assert(modem.network_command==6 && modem.waiting);
    parse_extended_signal(cesq);assert(modem.status.signal_rssi==-85 && modem.status.rsrp_dbm==-100);
    process_line(error,5);assert(modem.status.phase==ML307_PHASE_ONLINE && modem.status.signal_rssi==-85);
    memset(&modem,0,sizeof(modem));assert(send_mqtt_subscribe());
    char first_ack[]="+MQTTURC: \"suback\",0,1,0", second_ack[]="+MQTTURC: \"suback\",0,2,1";
    char first_id[]="+MQTTSUB: 0,1",second_id[]="+MQTTSUB: 0,2";
    process_line(first_id,strlen(first_id));
    process_line(ok,2);assert(!modem.status.mqtt_online && modem.waiting);
    process_line(first_ack,strlen(first_ack));assert(modem.mqtt_subscribe_step==1 && !modem.waiting && !registrations);
    process_line(first_ack,strlen(first_ack));assert(!modem.status.mqtt_online);
    assert(send_mqtt_subscribe());
    process_line(second_id,strlen(second_id));
    process_line(first_ack,strlen(first_ack));assert(!modem.status.mqtt_online);
    process_line(second_ack,strlen(second_ack));assert(!modem.status.mqtt_online && modem.waiting);
    process_line(ok,2);assert(modem.status.mqtt_online && registrations==1);
    process_line(second_ack,strlen(second_ack));assert(registrations==1);
    /* Old/foreign ACKs cannot satisfy a new command or reject it. */
    memset(&modem,0,sizeof(modem));assert(send_mqtt_subscribe());
    process_line(first_ack,strlen(first_ack));process_line(ok,2);
    assert(!modem.mqtt_subscribe_step); /* still missing command's assigned ID */
    process_line(first_id,strlen(first_id));assert(modem.mqtt_subscribe_step==1);
    assert(send_mqtt_subscribe());process_line(second_id,strlen(second_id));process_line(ok,2);
    char stale_denied[]="+MQTTURC: \"suback\",0,1,128";
    process_line(stale_denied,strlen(stale_denied));assert(modem.waiting&&!modem.status.mqtt_online);
    char denied[]="+MQTTURC: \"suback\",0,2,128";
    process_line(denied,strlen(denied));assert(modem.status.phase==ML307_PHASE_BACKOFF);
    memset(&modem,0,sizeof(modem));
    transition_wait(ML307_PHASE_PUBLISH,5000);modem.prompt_seen=1;
    char pub_id[]="+MQTTPUB: 0,72,38",pub_ack[]="+MQTTURC: \"puback\",0,72,0";
    char old_pub[]="+MQTTURC: \"puback\",0,71,0",foreign_pub[]="+MQTTURC: \"puback\",1,72,0";
    process_line(pub_ack,strlen(pub_ack));process_line(ok,2);assert(!publish_completions);
    process_line(pub_id,strlen(pub_id));assert(publish_completions==1);
    transition_wait(ML307_PHASE_PUBLISH,5000);modem.prompt_seen=1;
    process_line(pub_id,strlen(pub_id));process_line(ok,2);
    process_line(old_pub,strlen(old_pub));process_line(foreign_pub,strlen(foreign_pub));
    assert(publish_completions==1&&modem.waiting);
    process_line(pub_ack,strlen(pub_ack));assert(publish_completions==2);
    /* ATE0 must not add a fixed delay before the first CPIN request. */
    memset(&modem,0,sizeof(modem));CurTick=4000;modem.status.phase=ML307_PHASE_SIM;modem.waiting=1;
    process_line(ok,2);assert(modem.next_action_ms==CurTick && !modem.waiting);
    char not_ready[]="+CPIN: NOT READY", locked[]="+CPIN: SIM PIN", ready[]="+CPIN: READY", missing[]="+CME ERROR: 10";
    sim_check_pending=1;modem.waiting=1;process_line(not_ready,strlen(not_ready));
    assert(!modem.status.sim_ready);process_line(ok,2);
    assert(sim_failures==1 && !Dev.errorCode.bit.cellular && modem.next_action_ms==4100);
    CurTick=4100;sim_check_pending=1;modem.waiting=1;process_line(locked,strlen(locked));process_line(ok,2);
    assert(sim_failures==2 && Dev.errorCode.bit.cellular && modem.status.last_error==ML307_ERROR_SIM);
    assert(modem.next_action_ms==9100 && !sim_power_cycles);
    process_line(ok,2);assert(Dev.errorCode.bit.cellular && modem.next_action_ms==9100);
    CurTick=4200;process_line(ready,strlen(ready));
    assert(!Dev.errorCode.bit.cellular && !sim_failures && modem.next_action_ms==4200);
    /* No reply and explicit missing-card errors share the same bounded retry. */
    modem.status.sim_ready=0;sim_check_pending=1;modem.waiting=1;process_line(missing,strlen(missing));
    assert(sim_failures==1 && !modem.waiting && !Dev.errorCode.bit.cellular);
    sim_probe_result(0);assert(Dev.errorCode.bit.cellular);
    record_at_response(1);assert(Dev.errorCode.bit.cellular);
    while(!sim_power_cycles)sim_probe_result(0);
    assert(sim_failures==2 && Dev.errorCode.bit.cellular && modem.status.phase==ML307_PHASE_POWER_OFF);
    sim_probe_result(1);assert(!Dev.errorCode.bit.cellular && modem.status.sim_ready);
    /* Startup NOT READY is not a runtime removal; an already-ready SIM disappearing is. */
    memset(&modem,0,sizeof(modem));modem.status.phase=ML307_PHASE_AT_SYNC;
    process_line(not_ready,strlen(not_ready));assert(!Dev.errorCode.bit.cellular && modem.status.phase==ML307_PHASE_AT_SYNC);
    modem.status.phase=ML307_PHASE_ONLINE;modem.status.sim_ready=1;
    process_line(not_ready,strlen(not_ready));assert(Dev.errorCode.bit.cellular && modem.status.phase==ML307_PHASE_BACKOFF);
    /* No forced PIN/PUK entry; identity-query errors still do not fail SIM health. */
    sim_failures=0;modem.http_step=0;modem.status.phase=ML307_PHASE_SIM;
    modem.status.sim_ready=1;modem.identity_step=1;sim_check_pending=0;modem.waiting=1;
    process_line(error,5);assert(modem.identity_step==2 && !Dev.errorCode.bit.cellular);
    process_line(not_ready,strlen(not_ready));
    assert(Dev.errorCode.bit.cellular && modem.status.phase==ML307_PHASE_BACKOFF);
    return 0;
}
''')

    def test_ir_transmit_timeout_releases_busy_path_even_across_tick_wrap(self):
        source = (APP / 'ir.c').read_text(encoding='utf-8')
        prefix = no_includes(source[:source.index('static uint8_t Ir_IsControlPathIdle')])
        self.compile_run(r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#define PRINT(...) ((void)0)
#define IRBUFSIZE 256U
#define UART_FIFO_SIZE 8U
#define ENABLE 1
#define DISABLE 0
#define RB_IER_THR_EMPTY 1
#define UART3_IRQn 3
#define IR_TYPE_MATCH 1
#define IR_TYPE_LEARNing 2
typedef uint8_t IR_CMD_t;
typedef struct {uint8_t isFinish,type,matchError,learnError,rxlen,txbuf[256];} IRBUF_t;
static uint32_t CurTick;
static uint8_t R8_UART3_TFC=8,R8_UART3_THR;
static unsigned init_calls;
static unsigned completed, failed;
static void LED_NotifyIrTx(void) {}
static void Rule_IrCompleted(uint8_t ok) {if(ok) completed++; else failed++;}
static uint8_t SplitAcControl_IrCompleted(uint8_t ok) {(void)ok;return 0;}
static void UART3_INTCfg(uint8_t e,uint8_t m) {(void)e;(void)m;}
static void PFIC_DisableIRQ(uint8_t irq) {(void)irq;}
static void IR_Init(void);
''' + prefix + '\n'.join(function(source, n) for n in ('Ir_IsControlPathIdle', 'Ir_TxFillFifo', 'Ir_TxStartCopy', 'Ir_TxAbort', 'Ir_HandleTxFifoEmpty')) + r'''
static uint8_t Ir_SubmitInternalCommand(IR_CMD_t c) {return Ir_TxStartCopy(&c,1,0);}
static void IR_Init(void) {
    init_calls++;IrBuf.isFinish=1;irTxActive=0;irProfileCount=irProfileOffset=0;
    if(irCompletionPending){irCompletionPending=0;Rule_IrCompleted(0);}
}
''' + function(source, 'Ir_Pro') + r'''
int main(void) {
    uint8_t data[16]={0};
    IrBuf.type=IR_TYPE_MATCH;
    CurTick=0xFFFFFF00U;
    assert(Ir_TxStartCopy(data,16,1));
    CurTick+=1999;Ir_Pro();assert(irTxActive && !init_calls);
    CurTick++;Ir_Pro();assert(!irTxActive && IrBuf.isFinish && init_calls==1);
    assert(IrBuf.matchError);
    assert(Ir_TxStartCopy(data,16,0));
    CurTick+=2000;Ir_Pro();assert(failed==1 && !completed);
    /* 场景全部发完才回调；FIFO 完成后也不能被另一个请求插队。 */
    R8_UART3_TFC=0;
    assert(Ir_TxStartCopy(data,1,0));
    irProfileCount=2;irProfileOffset=1;irProfileCommands[1]=0x81;
    Ir_HandleTxFifoEmpty();assert(!Ir_IsControlPathIdle() && !completed);
    Ir_Pro();assert(irTxActive && irProfileOffset==2 && !completed);
    Ir_HandleTxFifoEmpty();Ir_Pro();
    assert(completed==1 && Ir_IsControlPathIdle());
    /* Learning/raw timeout exactly at tick zero must still release busy. */
    IrBuf.isFinish=0;IrBuf.type=IR_TYPE_LEARNing;irOperationDeadline=0;
    CurTick=UINT32_MAX;Ir_Pro();assert(!IrBuf.isFinish);
    CurTick=0;Ir_Pro();assert(IrBuf.isFinish && IrBuf.learnError);
    return 0;
}
''')

    def test_ble_notification_failure_retries_same_fragment_then_releases_connection(self):
        source = (APP / 'peripheral.c').read_text(encoding='utf-8')
        fields = source[source.index('static uint8_t deviceTxFrame'):source.index('static uint8_t deviceSessionReady')]
        self.compile_run(r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "device_protocol.h"
#define SUCCESS 0U
#define INVALIDPARAMETER 2U
#define MSG_BUFFER_NOT_AVAIL 4U
#define ATT_HANDLE_VALUE_NOTI 1U
#define SIMPLEPROFILE_CHAR1 1U
#define GAP_CONNHANDLE_INIT 0xFFFFU
#define SBP_TX_FRAME_EVT 1U
#define MS1_TO_SYSTEM_TIME(ms) (ms)
#define PRINT(...) ((void)0)
#define tmos_memcpy memcpy
typedef struct {uint16_t len;uint8_t *pValue;} attHandleValueNoti_t;
typedef struct {uint8_t ignored;} gattMsg_t;
static struct {uint16_t connHandle;} peripheralConnList={1};
static uint16_t peripheralMTU=23;
static uint8_t Peripheral_TaskID;
static uint32_t CurTick;
static uint8_t buffer[256],last_fragment[256];
static uint8_t allocate_ok,notify_ok;
static unsigned freed,terminated;
static uint8_t *GATT_bm_alloc(uint16_t c,uint8_t t,uint16_t l,void *p,uint8_t f) {(void)c;(void)t;(void)l;(void)p;(void)f;return allocate_ok?buffer:0;}
static uint8_t simpleProfile_Notify(uint16_t c,uint8_t i,attHandleValueNoti_t *n) {(void)c;(void)i;memcpy(last_fragment,n->pValue,n->len);return notify_ok?0:4;}
static void GATT_bm_free(gattMsg_t *p,uint8_t t) {(void)p;(void)t;freed++;}
static void tmos_start_task(uint8_t t,uint16_t e,uint32_t d) {(void)t;(void)e;(void)d;}
static void tmos_set_event(uint8_t t,uint16_t e) {(void)t;(void)e;}
uint16_t DeviceProtocol_Crc16(const uint8_t *p,uint16_t n){uint16_t s=0;while(n--)s+=*p++;return s;}
static void GAPRole_TerminateLink(uint16_t h) {(void)h;terminated++;}
''' + fields + '\n'.join(function(source, n) for n in ('peripheralCharNotify', 'SendDeviceFrame', 'Peripheral_ControlCompleted', 'DeviceTxStep')) + r'''
int main(void) {
    uint8_t frame[32]={0xA5,2,2,7};
    SendDeviceFrame(frame,32);
    DeviceTxStep();assert(deviceTxOffset==0 && deviceTxIndex==0);
    allocate_ok=1;DeviceTxStep();assert(deviceTxOffset==0 && freed==1);
    notify_ok=1;DeviceTxStep();assert(deviceTxOffset==15 && deviceTxIndex==1);
    assert(last_fragment[0]==0x80 && last_fragment[1]==0 && last_fragment[3]==7);
    DeviceTxStep();DeviceTxStep();assert(!deviceTxActive);
    SendDeviceFrame(frame,32);notify_ok=0;CurTick=1500;
    DeviceTxStep();assert(!deviceTxActive && terminated==1);
    CurTick=UINT32_MAX-1499U;SendDeviceFrame(frame,32);
    CurTick=UINT32_MAX;DeviceTxStep();assert(deviceTxActive && terminated==1);
    CurTick=0;DeviceTxStep();assert(!deviceTxActive && terminated==2);
    notify_ok=1;SendDeviceFrame(frame,32);deviceControlPending=1;
    DeviceTxStep();assert(deviceTxOffset==0);
    Peripheral_ControlCompleted(6);assert(!deviceControlPending&&deviceTxFrame[6]==6);
    assert(deviceTxFrame[30]==(uint8_t)DeviceProtocol_Crc16(deviceTxFrame,30));
    DeviceTxStep();DeviceTxStep();DeviceTxStep();assert(!deviceTxActive);
    deviceControlPending=0;Peripheral_ControlCompleted(0);assert(!deviceTxActive);
    return 0;
}
''')

    def test_updater_installs_unaligned_tail_and_recovers_interrupted_copy(self):
        source = (ROOT / 'BLE/BackupUpgrade_IAP/APP/peripheral_main.c').read_text(encoding='utf-8')
        self.compile_run(r'''
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include "CH58x_common.h"
#include "ota_update.h"
static ota_metadata_t metadata;
static uint8_t copy_buffer[256] __attribute__((aligned(4)));
static uint8_t flash[480*1024];
static int fail_at=-1,write_count;
uint32_t Test_FlashErase(uint32_t a,uint32_t l) {memset(flash+a,0xFF,l);return 0;}
uint32_t Test_FlashWrite(uint32_t a,const void *b,uint32_t l) {
    assert(!(a&3) && !(l&3) && !((uintptr_t)b&3));
    if(write_count++==fail_at)return 1;
    memcpy(flash+a,b,l);return 0;
}
void Test_FlashRead(uint32_t a,void *b,uint32_t l) {memcpy(b,flash+a,l);}
''' + '\n'.join(function(source, n) for n in ('crc32_update', 'image_crc', 'install_image')) + r'''
int main(void) {
    const uint32_t sizes[]={64,65,67,255,256,257,4095,4097,OTA_MAX_IMAGE_SIZE};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        metadata.image_size=sizes[i];
        memset(flash,0xFF,sizeof(flash));
        for(uint32_t n=0;n<sizes[i];n++)flash[OTA_STAGING_ADDRESS+n]=(uint8_t)n;
        metadata.image_crc32=image_crc(OTA_STAGING_ADDRESS,sizes[i]);
        write_count=0;fail_at=0;
        assert(!install_image());
        fail_at=-1;
        assert(install_image());
        assert(!memcmp(flash+OTA_APP_ADDRESS,flash+OTA_STAGING_ADDRESS,sizes[i]));
        int before=write_count;
        assert(install_image() && write_count==before); /* 完成复制后的重启不能再次擦写 */
        for(uint32_t n=sizes[i];n<((sizes[i]+3)&~3U);n++)assert(flash[OTA_APP_ADDRESS+n]==0xFF);
        flash[OTA_STAGING_ADDRESS]^=1;
        assert(!install_image());
        assert(flash[OTA_APP_ADDRESS]==0);
    }
    return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
