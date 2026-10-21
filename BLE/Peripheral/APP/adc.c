#include "CH58x_common.h"
#include "board.h"
#include "ntc_b3950.h"
#define _DEBUG_AD 0

#define SHT30_SDA_PIN              GPIO_Pin_8
#define SHT30_SCL_PIN              GPIO_Pin_9
#define SHT30_ADDRESS_WRITE        0x88U
#define SHT30_ADDRESS_READ         0x89U
#define SHT30_MEASURE_COMMAND      0x2400U /* 单次测量、高精度、禁用时钟延展 */
#define SHT30_MEASURE_TIME_MS      15U     /* 高精度单次测量的最大转换时间 */

static uint8_t adcValid;
static uint8_t sensorStatus;
static uint8_t sht30Pending;
static uint16_t humidityX10;
static uint16_t sht30Errors;
static uint32_t sht30StartedMs;

/* CH583 没有开漏模式：输出低电平表示拉低，切换为上拉输入表示释放总线。 */
static void i2c_sda_low(void){ GPIOB_ResetBits(SHT30_SDA_PIN); GPIOB_ModeCfg(SHT30_SDA_PIN, GPIO_ModeOut_PP_5mA); }
static void i2c_sda_release(void){ GPIOB_ModeCfg(SHT30_SDA_PIN, GPIO_ModeIN_PU); }
static void i2c_scl_low(void){ GPIOB_ResetBits(SHT30_SCL_PIN); GPIOB_ModeCfg(SHT30_SCL_PIN, GPIO_ModeOut_PP_5mA); }
static void i2c_scl_release(void){ GPIOB_ModeCfg(SHT30_SCL_PIN, GPIO_ModeIN_PU); }
static uint8_t i2c_sda_read(void){ return GPIOB_ReadPortPin(SHT30_SDA_PIN) ? 1U : 0U; }
static uint8_t i2c_scl_read(void){ return GPIOB_ReadPortPin(SHT30_SCL_PIN) ? 1U : 0U; }
static void i2c_delay(void){ DelayUs(4); }

static void i2c_stop(void)
{
    i2c_sda_low(); i2c_delay();
    i2c_scl_release(); i2c_delay();
    i2c_sda_release(); i2c_delay();
}

static uint8_t i2c_start(void)
{
    uint8_t i;
    i2c_sda_release();
    i2c_scl_release();
    i2c_delay();
    if(!i2c_scl_read()) return 0U;
    if(!i2c_sda_read()) {
        /* 掉电或中断可能令从机停在半字节状态，最多补 9 个时钟释放 SDA。 */
        for(i = 0U; i < 9U && !i2c_sda_read(); i++) {
            i2c_scl_low(); i2c_delay();
            i2c_scl_release(); i2c_delay();
        }
        i2c_stop();
        if(!i2c_sda_read()) return 0U;
    }
    i2c_sda_low(); i2c_delay();
    i2c_scl_low(); i2c_delay();
    return 1U;
}

static uint8_t i2c_write_byte(uint8_t value)
{
    uint8_t i, acknowledged;
    for(i = 0U; i < 8U; i++) {
        if(value & 0x80U) i2c_sda_release(); else i2c_sda_low();
        i2c_delay();
        i2c_scl_release(); i2c_delay();
        if(!i2c_scl_read()) return 0U;
        i2c_scl_low();
        value <<= 1;
    }
    i2c_sda_release(); i2c_delay();
    i2c_scl_release(); i2c_delay();
    acknowledged = i2c_sda_read() ? 0U : 1U;
    i2c_scl_low(); i2c_delay();
    return acknowledged;
}

static uint8_t i2c_read_byte(uint8_t acknowledge)
{
    uint8_t i, value = 0U;
    i2c_sda_release();
    for(i = 0U; i < 8U; i++) {
        value <<= 1;
        i2c_scl_release(); i2c_delay();
        if(i2c_sda_read()) value |= 1U;
        i2c_scl_low(); i2c_delay();
    }
    if(acknowledge) i2c_sda_low(); else i2c_sda_release();
    i2c_scl_release(); i2c_delay();
    i2c_scl_low(); i2c_delay();
    i2c_sda_release();
    return value;
}

static uint8_t sht30_crc(const uint8_t *data)
{
    uint8_t crc = 0xFFU, i, bit;
    for(i = 0U; i < 2U; i++) {
        crc ^= data[i];
        for(bit = 0U; bit < 8U; bit++) crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U) : (uint8_t)(crc << 1);
    }
    return crc;
}

static uint8_t sht30_begin(void)
{
    uint8_t ok;
    if(!i2c_start()) return 0U;
    ok = i2c_write_byte(SHT30_ADDRESS_WRITE) &&
         i2c_write_byte((uint8_t)(SHT30_MEASURE_COMMAND >> 8)) &&
         i2c_write_byte((uint8_t)SHT30_MEASURE_COMMAND);
    i2c_stop();
    return ok;
}

static uint8_t sht30_read(int16_t *temperature, uint16_t *humidity)
{
    uint8_t data[6], i;
    uint16_t rawTemperature, rawHumidity;
    if(!i2c_start()) return 0U;
    if(!i2c_write_byte(SHT30_ADDRESS_READ)) { i2c_stop(); return 0U; }
    for(i = 0U; i < sizeof(data); i++) data[i] = i2c_read_byte(i + 1U < sizeof(data));
    i2c_stop();
    if(sht30_crc(data) != data[2] || sht30_crc(data + 3) != data[5]) return 0U;

    rawTemperature = ((uint16_t)data[0] << 8) | data[1];
    rawHumidity = ((uint16_t)data[3] << 8) | data[4];
    *temperature = (int16_t)(-450L + ((1750L * rawTemperature + 32767L) / 65535L));
    /* SHT3x 湿度换算为 100*raw/65535，raw∈[0,65535] 时结果恒落在 [0,1000]，无需钳位。 */
    *humidity = (uint16_t)((1000L * rawHumidity + 32767L) / 65535L);
    return 1U;
}

static uint8_t ntc_sample(int16_t *temperatureX10)
{
    uint16_t caliVal, temp, maxVal = 0, minVal = 0xffff, i;
    uint32_t sum = 0;
    GPIOA_ModeCfg(GPIO_Pin_4, GPIO_ModeIN_Floating);
    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_0);
    caliVal = ADC_DataCalib_Rough();
    ADC_ChannelCfg(0);
    for(i = 0; i < 20; i++) {
        temp = ADC_ExcutSingleConver() + caliVal;
        if(temp > maxVal) maxVal = temp;
        if(temp < minVal) minVal = temp;
        sum += temp;
    }
    sum -= (maxVal + minVal);
    return NtcB3950_AdcToTempX10((uint16_t)(sum / 18U), temperatureX10);
}

void ADC_Init(void){
    adcValid = 0;
    sensorStatus = 0U;
    sht30Pending = 0U;
    humidityX10 = 0U;
    sht30Errors = 0U;
    i2c_sda_release();
    i2c_scl_release();
    Dev.errorCode.bit.ad = 0; /* Not yet sampled is not a confirmed hardware fault. */
}
uint8_t ADC_IsValid(void){ return adcValid; }
uint8_t ADC_GetSensorStatus(void){ return sensorStatus; }
uint16_t ADC_GetHumidityX10(void){ return humidityX10; }
uint16_t ADC_GetSht30Errors(void){ return sht30Errors; }

void ADC_Pro(void){
    static uint32_t lastSampleTick = 0;
    static uint8_t sampled = 0U;
    int16_t temperatureX10;

    if(sht30Pending) {
        if((uint32_t)(CurTick - sht30StartedMs) < SHT30_MEASURE_TIME_MS) return;
        sht30Pending = 0U;
        lastSampleTick = CurTick;
        sampled = 1U;
        if(sht30_read(&temperatureX10, &humidityX10)) {
            Dev.roomTempX10 = temperatureX10;
            adcValid = 1U;
            sensorStatus = TEMP_SENSOR_TEMP_VALID | TEMP_SENSOR_HUMIDITY_VALID | TEMP_SENSOR_SHT30_ACTIVE;
            Dev.errorCode.bit.ad = 0U;
            return;
        }
        if(sht30Errors != 0xffffU) sht30Errors++;
        if(ntc_sample(&temperatureX10)) {
            Dev.roomTempX10 = temperatureX10;
            adcValid = 1U;
            humidityX10 = 0U;
            sensorStatus = TEMP_SENSOR_TEMP_VALID | TEMP_SENSOR_NTC_FALLBACK | TEMP_SENSOR_SHT30_FAULT;
            Dev.errorCode.bit.ad = 0U;
        } else {
            adcValid = 0U;
            humidityX10 = 0U;
            sensorStatus = TEMP_SENSOR_SHT30_FAULT;
            Dev.errorCode.bit.ad = 1U;
        }
        return;
    }
    /* Sensor polling must continue even when the external calendar stops. */
    if(sampled && (uint32_t)(CurTick - lastSampleTick) < AD_INTERVAL * 1000U){
        return;
    }
    if(sht30_begin()) {
        sht30Pending = 1U;
        sht30StartedMs = CurTick;
        return;
    }

    lastSampleTick = CurTick;
    sampled = 1U;
    if(sht30Errors != 0xffffU) sht30Errors++;
    if(ntc_sample(&temperatureX10)) {
        Dev.roomTempX10 = temperatureX10;
        adcValid = 1U;
        humidityX10 = 0U;
        sensorStatus = TEMP_SENSOR_TEMP_VALID | TEMP_SENSOR_NTC_FALLBACK | TEMP_SENSOR_SHT30_FAULT;
        Dev.errorCode.bit.ad = 0U;
    } else {
        adcValid = 0U;
        humidityX10 = 0U;
        sensorStatus = TEMP_SENSOR_SHT30_FAULT;
        Dev.errorCode.bit.ad = 1U;
    }
}
