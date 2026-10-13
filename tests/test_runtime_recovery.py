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
    def test_led_running_and_network_indicators_do_not_blink(self):
        source = no_includes((APP / 'led.c').read_text(encoding='utf-8'))
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
#define GAPROLE_CONNECTED 2U
#define GAPROLE_CONNECTED_ADV 3U
#define ML307_ERROR_MODEM 1U
#define ML307_ERROR_RX_OVERFLOW 2U
#define ML307_ERROR_UART 3U
typedef struct { uint8_t mqtt_online, last_error; } ml307_status_t;
static ml307_status_t cell;
static struct {
    uint8_t loraStatus;
    struct { struct { uint8_t flash, ad, power, rtc, lora; } bit; } errorCode;
} Dev;
static uint32_t CurTick, lights;
static uint8_t lora, cellular, identify, ble;
static void GPIOB_ResetBits(uint32_t pins) { lights |= pins; }
static void GPIOB_SetBits(uint32_t pins) { lights &= ~pins; }
static const ml307_status_t *Ml307_GetStatus(void) { return &cell; }
static uint8_t Connectivity_LoraEnabled(void) { return lora; }
static uint8_t Connectivity_CellularEnabled(void) { return cellular; }
static uint8_t DeviceService_IdentifyActive(void) { return identify; }
static void GAPRole_GetParameter(unsigned p, uint8_t *s) { (void)p; *s=ble; }
''' + source + r'''
int main(void) {
    for(CurTick=0; CurTick<4000; CurTick+=100) {
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
    CurTick=UINT32_MAX-199U; LED_NotifyIrTx();
    for(unsigned offset=0; offset<=500; offset+=100) {
        CurTick=(UINT32_MAX-199U)+offset; LED_Pro();
        assert(!!(lights & LED_GREEN_PIN)==(offset>=400 || (offset/100U)%2U==0));
        assert(!(lights & LED_WHITE_PIN));
    }
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
uint8_t Config_CommitIfChanged(void) {return 0;}
uint8_t Runtime_Append(void) {return runtime_status;}
uint8_t IrStore_SaveIfChanged(void) {return ir_status;}
uint32_t Config_GetRevision(void) {return 1;}
uint8_t Storage_GetStartupFlags(void) {return startup_flags;}
''' + source + r'''
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
                                    '-DFIRMWARE_BUILD_VERSION=0x21613', '-I' + str(ROOT),
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
    assert(!RTC_SetTimestamp(1U) && !RTC_SetTimestamp(2147483001U));
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
    assert(!RTC_SyncTimestamp(1U) && !RTC_SyncTimestamp(2147483001U));
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
    assert(topic_matches("sub/ac/F1234",12));
    assert(!topic_matches("sub/ac/F1235",12));
    assert(!topic_matches("sub/ac/F1234x",13));
    assert(!topic_matches("sub/ac/F123",11));
    strcpy(uid,"1234");assert(topic_matches("sub/ac/1234",11));
    strcpy(uid,"A26091421");assert(topic_matches("sub/ac/A26091421",16));
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
static uint8_t topic_matches(const char *t,uint8_t n) {return n==11 && !memcmp(t,"sub/ac/1234",11);}
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
#define ML307_MGMT_MAGIC 0xC7U
#define ML307_MGMT_VERSION 1U
''' + context + r'''
static ml307_context_t modem;
static uint8_t management_frame[72],publishing_management_frame[72];
static char tx_buffer[192];
static uint32_t CurTick;
static uint16_t device_jitter_ms;
static unsigned declared_length,resets;
typedef struct {uint8_t mqtt_qos;const char *publish_topic;} connectivity_config_t;
static connectivity_config_t config={1,"pub/ac/test"};
static const connectivity_config_t *Connectivity_Get(void) {return &config;}
static uint8_t build_publish_frame(uint8_t *f) {memset(f,0,38);return 38;}
static uint8_t tx_append_text(uint16_t *l,const char *t) {(void)l;(void)t;return 1;}
static uint8_t tx_append_topic(uint16_t *l,const char *t) {(void)l;(void)t;return 1;}
static uint8_t tx_append_u32(uint16_t *l,uint32_t v) {(void)l;declared_length=v;return 1;}
static uint8_t tx_start(uint16_t l) {(void)l;return 1;}
static void transition_wait(uint8_t p,uint32_t d) {(void)d;modem.status.phase=p;}
static void frame_put16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static uint8_t GatewayLora_Checksum(const uint8_t *p,uint16_t l) {uint8_t s=0;while(l--)s+=*p++;return s;}
static void status_phase(uint8_t p) {modem.status.phase=p;}
static void uart_disable(void) {}
#define SYS_DisableAllIrq(p) ((void)(p))
#define mDelaymS(ms) ((void)(ms))
#define SYS_ResetExecute() (++resets)
''' + '\n'.join(function(source, name) for name in names) + r'''
int main(void) {
    uint8_t payload[12]={0};
    queue_management_frame(8,1,payload,12);
    assert(start_publish() && declared_length==21);
    queue_management_frame(4,2,payload,2);
    queue_management_frame(4,3,payload,2); /* 满槽不能覆盖已接受的下一条响应 */
    assert(management_frame[4]==2);
    assert(send_publish_payload() && (uint8_t)tx_buffer[4]==1);
    enter_backoff(ML307_ERROR_MQTT);
    assert(modem.publishing_management && modem.management_pending);
    assert(start_publish() && declared_length==21);
    assert(send_publish_payload() && (uint8_t)tx_buffer[4]==1);
    modem.ota_activate_after_publish=1; /* 另一在途状态上报不能触发重启 */
    complete_publish();
    assert(!resets && modem.management_pending);
    assert(start_publish() && declared_length==11);
    assert(send_publish_payload() && (uint8_t)tx_buffer[4]==2);
    complete_publish();
    assert(!modem.management_pending && !resets);
    payload[0]=6;payload[1]=0;
    queue_management_frame(4,4,payload,2);
    assert(start_publish());complete_publish();
    assert(resets==1);
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
static unsigned http_calls;
typedef struct {uint8_t mqtt_qos;} connectivity_config_t;
static connectivity_config_t config={1};
static const connectivity_config_t *Connectivity_Get(void) {return &config;}
static void uart_disable(void) {}
static uint8_t at_process_line(const char *l,uint16_t n) {(void)l;(void)n;return 0;}
static uint8_t http_process_line(char *l,uint16_t n) {(void)l;(void)n;if(!modem.http_step)return 0;http_calls++;return 1;}
static void complete_mqtt_connection(void) {}
static void queue_registration(void) {}
static void complete_publish(void) {}
static uint8_t copy_numeric_identity(char *o,uint8_t c,const char *l,uint8_t n,uint8_t m) {(void)o;(void)c;(void)l;(void)n;(void)m;return 1;}
static void parse_network_clock(const char *l,uint16_t n) {(void)l;(void)n;}
static uint8_t tx_command(const char *l) {(void)l;return 1;}
''' + '\n'.join(function(source,n) for n in (
    'mqtt_urc_is','mqtt_urc_number','parse_cereg','parse_signal','parse_extended_signal',
    'status_phase','transition_wait','enter_backoff','process_line')) + r'''
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
static void GAPRole_TerminateLink(uint16_t h) {(void)h;terminated++;}
''' + fields + '\n'.join(function(source, n) for n in ('peripheralCharNotify', 'SendDeviceFrame', 'DeviceTxStep')) + r'''
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
