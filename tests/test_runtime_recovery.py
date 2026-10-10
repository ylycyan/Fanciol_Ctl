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
            'RTC_IsTimeValid', 'RTC_GetWallTime'))
        conversion = no_includes((APP / 'time_utils.c').read_text(encoding='utf-8'))
        self.compile_run(r'''
#include <assert.h>
#include <string.h>
#include "board.h"
#include "time_utils.h"
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
''' + conversion + prefix + clock + r'''
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
#define SHT40_MEASURE_TIME_MS 10U
#define TEMP_SENSOR_TEMP_VALID 1
#define TEMP_SENSOR_HUMIDITY_VALID 2
#define TEMP_SENSOR_SHT40_ACTIVE 4
#define TEMP_SENSOR_NTC_FALLBACK 8
#define TEMP_SENSOR_SHT40_FAULT 16
t_dev Dev;
volatile uint32_t CurTick;
static uint8_t adcValid,sensorStatus,sht40Pending;
static uint16_t humidityX10,sht40Errors;
static uint32_t sht40StartedMs;
static uint8_t sht40_begin(void) {return 0;}
static uint8_t sht40_read(int16_t *t,uint16_t *h) {(void)t;(void)h;return 0;}
static uint8_t ntc_sample(int16_t *t) {*t=260;return 1;}
''' + function(source, 'ADC_Pro') + r'''
int main(void) {
    ADC_Pro();assert(sht40Errors==1 && adcValid);
    CurTick=9999;ADC_Pro();assert(sht40Errors==1);
    CurTick=10000;ADC_Pro();assert(sht40Errors==2);
    CurTick=0xfffffff0;ADC_Pro();assert(sht40Errors==3);
    CurTick+=9999;ADC_Pro();assert(sht40Errors==3);
    CurTick++;ADC_Pro();assert(sht40Errors==4 && Dev.roomTempX10==260);
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
static void UART3_INTCfg(uint8_t e,uint8_t m) {(void)e;(void)m;}
static void LED_NotifyIrTx(void) {}
static void PFIC_DisableIRQ(uint8_t irq) {(void)irq;}
static void IR_Init(void);
''' + prefix + '\n'.join(function(source, n) for n in ('Ir_TxFillFifo', 'Ir_TxStartCopy', 'Ir_TxAbort')) + r'''
static uint8_t Ir_SubmitInternalCommand(IR_CMD_t c) {(void)c;return 1;}
static void IR_Init(void) {init_calls++;IrBuf.isFinish=1;irTxActive=0;irProfileCount=irProfileOffset=0;}
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
