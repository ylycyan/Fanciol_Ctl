/**
 * @file hlw8110.c
 * @brief HLW8110 单相电能计量芯片驱动（非阻塞状态机）
 *
 * 通过 UART0 (PB4:RX, PB7:TX) 与 HLW8110 通信，9600 8E1。
 * 主循环每 20ms 调用 HLW8110_Poll() 推进状态机：
 *   - 上电/故障后先拉低 RX 引脚复位芯片（手册要求 >9.15ms）
 *   - 依次读取并回写 SYSCON/EMUCON/EMUCON2 配置并回读校验
 *   - 校准系数（RMS_IAC / RMS_UC / POWER_PAC）后进入周期采样
 *   - 每 2 秒采样 功率/电流/电压 一组，全部通过范围校验后才对外发布
 * 通信异常自动整芯片复位重连；连续失败达到阈值后置位功率故障码。
 */
#include "CH58x_common.h"
#include "board.h"
#include "hlw8110.h"
#include "hlw8110_math.h"
#include <string.h>

#define HLW_UART_BAUD                 9600UL
#define HLW_FRAME_HEAD                0xA5U
#define HLW_SPECIAL_CMD               0xEAU
#define HLW_WRITE_ENABLE              0xE5U
#define HLW_WRITE_PROTECT             0xDCU
#define HLW_SELECT_CHANNEL_A          0x5AU

#define HLW_REG_SYSCON                0x00U
#define HLW_REG_EMUCON                0x01U
#define HLW_REG_EMUCON2               0x13U
#define HLW_REG_RMS_IA                0x24U
#define HLW_REG_RMS_U                 0x26U
#define HLW_REG_POWER_PA              0x2CU
#define HLW_REG_RMS_IAC               0x70U
#define HLW_REG_RMS_UC                0x72U
#define HLW_REG_POWER_PAC             0x73U

#define HLW_SYSCON_ADC1ON             (1U << 9)
#define HLW_SYSCON_ADC2ON             (1U << 10)
#define HLW_SYSCON_ADC3ON             (1U << 11)
#define HLW_SYSCON_PGAIA_MASK         0x0007U
#define HLW_SYSCON_PGAIA_X16          0x0004U
#define HLW_SYSCON_PGAU_MASK          0x0038U
#define HLW_EMUCON_HPFU_OFF           (1U << 4)
#define HLW_EMUCON_HPFIA_OFF          (1U << 5)
#define HLW_EMUCON_DC_MODE            (1U << 9)
#define HLW_EMUCON2_VREF_SEL          (1U << 0)
#define HLW_EMUCON2_PEAK_EN           (1U << 1)
#define HLW_EMUCON2_WAVE_EN           (1U << 5)
#define HLW_EMUCON2_CHS_IB            (1U << 7)

#define HLW_RESPONSE_TIMEOUT_MS       80UL
#define HLW_BOOT_WAIT_MS              100UL
#define HLW_TX_SETTLE_MS              7UL
#define HLW_CONFIG_SETTLE_MS          10UL
#define HLW_RECOVERY_LOW_MS           12UL
#define HLW_SAMPLE_INTERVAL_MS        2000UL
#define HLW_FAULT_THRESHOLD           3U
typedef enum {
    HLW_STATE_RESET_LOW = 0,
    HLW_STATE_BOOT_WAIT,
    HLW_STATE_CONFIG_READ_SYSCON,
    HLW_STATE_CONFIG_READ_EMUCON,
    HLW_STATE_CONFIG_READ_EMUCON2,
    HLW_STATE_CONFIG_UNLOCK,
    HLW_STATE_CONFIG_WRITE_SYSCON,
    HLW_STATE_CONFIG_WRITE_EMUCON,
    HLW_STATE_CONFIG_WRITE_EMUCON2,
    HLW_STATE_CONFIG_SELECT_A,
    HLW_STATE_CONFIG_LOCK,
    HLW_STATE_CONFIG_SETTLE,
    HLW_STATE_CONFIG_VERIFY_SYSCON,
    HLW_STATE_CONFIG_VERIFY_EMUCON,
    HLW_STATE_CONFIG_VERIFY_EMUCON2,
    HLW_STATE_CAL_RMS_IAC,
    HLW_STATE_CAL_RMS_UC,
    HLW_STATE_CAL_POWER_PAC,
    HLW_STATE_SELECT_A,
    HLW_STATE_IDLE,
    HLW_STATE_SAMPLE_POWER,
    HLW_STATE_SAMPLE_CURRENT,
    HLW_STATE_SAMPLE_VOLTAGE
} hlw_state_t;

typedef enum {
    HLW_RX_WAITING = 0,
    HLW_RX_OK,
    HLW_RX_FAILED
} hlw_rx_result_t;

static HLW8110_Status_t meter_status;
static hlw_state_t meter_state;
static uint32_t state_started_ms;
static uint32_t next_sample_ms;
static uint16_t rms_iac;
static uint16_t rms_uc;
static uint16_t power_pac;
static uint16_t config_syscon;
static uint16_t config_emucon;
static uint16_t config_emucon2;
static uint16_t desired_syscon;
static uint16_t desired_emucon;
static uint16_t desired_emucon2;
static uint8_t response[5];
static uint8_t response_count;
static uint8_t response_expected;
static uint8_t pending_command;
static uint8_t request_active;
static uint8_t rx_failure_reason;
static uint32_t rx_failure_detail;
/* A sample is published only after power/current/voltage all pass validation. */
static uint16_t sample_power_w_x10;
static uint16_t sample_current_ma;
static uint16_t sample_voltage_dv;

static uint8_t elapsed(uint32_t now, uint32_t since, uint32_t interval)
{
    return (uint32_t)(now - since) >= interval;
}

static uint16_t parse_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t parse_be24(const uint8_t *data)
{
    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
}

static uint32_t parse_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

/**
 * @brief UART0 引脚与波特率配置（HLW8110 专用，8E1）
 */
static void uart_configure(void)
{
    GPIOPinRemap(DISABLE, RB_PIN_UART0);
    GPIOB_SetBits(GPIO_Pin_7);
    GPIOB_ModeCfg(GPIO_Pin_4, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_7, GPIO_ModeOut_PP_5mA);
    UART0_BaudRateCfg(HLW_UART_BAUD);
    R8_UART0_FCR = RB_FCR_TX_FIFO_CLR | RB_FCR_RX_FIFO_CLR | RB_FCR_FIFO_EN;
    R8_UART0_LCR = RB_LCR_WORD_SZ | RB_LCR_PAR_EN | (1U << 4); /* 8E1 */
    R8_UART0_IER = RB_IER_TXD_EN;
    R8_UART0_DIV = 1;
}

/**
 * @brief 记录一次通信失败，连续达到阈值时置位功率故障码
 * @param reason 失败原因（HLW8110_ERROR_*）
 * @param detail 附加详情（寄存器值等），用于现场诊断
 */
static void record_failure(uint8_t reason, uint32_t detail)
{
    meter_status.last_error_reason = reason;
    meter_status.last_error_state = (uint8_t)meter_state;
    meter_status.last_error_register = pending_command;
    if(meter_status.communication_errors != 0xFFFFU) meter_status.communication_errors++;
    if(meter_status.consecutive_errors != 0xFFU) meter_status.consecutive_errors++;
    if(meter_status.consecutive_errors == HLW_FAULT_THRESHOLD) {
        meter_status.valid = 0;
        /* 连续失败后才撤销实时值，避免一次瞬时干扰造成主页数据闪烁。 */
        Dev.loadPower = 0;
        Dev.errorCode.bit.power = 1;
        if(Dev.meter.fault_count != 0xFFFFU) Dev.meter.fault_count++;
        PRINT("HLW8110 offline: reason=%u state=%u reg=%02x detail=%08lx\r\n",
              reason, (uint8_t)meter_state, pending_command,
              (unsigned long)detail);
    }
}

/**
 * @brief 进入整芯片复位恢复流程
 *
 * 按手册要求将 RX 保持低电平超过 9.15ms，随后重新走完整配置流程。
 */
static void begin_recovery(uint32_t now, uint8_t reason, uint32_t detail)
{
    record_failure(reason, detail);
    request_active = 0;
    response_count = 0;

    /* 手册建议通信异常时将 HLW8110 RX 保持低电平超过 9.15 ms。 */
    UART0_Reset();
    GPIOPinRemap(DISABLE, RB_PIN_UART0);
    GPIOB_ModeCfg(GPIO_Pin_7, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(GPIO_Pin_7);
    meter_state = HLW_STATE_RESET_LOW;
    state_started_ms = now;
}

/**
 * @brief 拒绝本次采样（数据越界），不做昂贵的整芯片复位
 *
 * 与 begin_recovery 的区别：UART/芯片本身正常，只是数值超出合理范围，
 * 直接丢弃本轮采样并等待下一个采样周期。
 */
static void reject_sample(uint32_t now, uint8_t reason, uint32_t detail)
{
    /* 数据越界不代表 UART 或芯片失去响应，不做昂贵的整芯片复位。 */
    record_failure(reason, detail);
    request_active = 0;
    response_count = 0;
    meter_state = HLW_STATE_IDLE;
    next_sample_ms = now + HLW_SAMPLE_INTERVAL_MS;
    state_started_ms = now;
}

/**
 * @brief 发送结果检查：失败或超时则进入恢复流程
 */
static uint8_t tx_progress_or_recover(uint8_t sent, uint32_t now)
{
    if(sent) return 1U;
    if(elapsed(now, state_started_ms, HLW_RESPONSE_TIMEOUT_MS))
        begin_recovery(now, HLW8110_ERROR_TX_BUSY, R8_UART0_TFC);
    return 0U;
}

/**
 * @brief 发起一次寄存器读请求
 * @param reg 寄存器地址（bit7 必须为 0）
 * @param data_length 期望数据长度（1~4 字节，不含校验和）
 */
static uint8_t start_read(uint8_t reg, uint8_t data_length, uint32_t now)
{
    if(data_length == 0U || data_length > 4U || R8_UART0_TFC > (UART_FIFO_SIZE - 2U)) return 0;
    UART0_CLR_RXFIFO();
    UART0_SendByte(HLW_FRAME_HEAD);
    UART0_SendByte(reg & 0x7FU);
    pending_command = reg & 0x7FU;
    response_expected = (uint8_t)(data_length + 1U);
    response_count = 0;
    request_active = 1;
    state_started_ms = now;
    return 1;
}

/**
 * @brief 轮询等待读响应：校验帧长、全 FF、校验和，超时判定
 * @retval HLW_RX_WAITING 等待中；HLW_RX_OK 完整有效；HLW_RX_FAILED 出错
 */
static hlw_rx_result_t poll_response(uint32_t now)
{
    uint8_t line_status = UART0_GetLinSTA();
    if(line_status & (STA_ERR_BREAK | STA_ERR_FRAME | STA_ERR_PAR | STA_ERR_FIFOOV)) {
        rx_failure_reason = HLW8110_ERROR_UART_LINE;
        rx_failure_detail = line_status;
        UART0_CLR_RXFIFO();
        return HLW_RX_FAILED;
    }

    while(R8_UART0_RFC != 0U) {
        uint8_t value = UART0_RecvByte();
        if(response_count >= response_expected) {
            rx_failure_reason = HLW8110_ERROR_RX_LENGTH;
            rx_failure_detail = ((uint32_t)response_count << 8) | response_expected;
            UART0_CLR_RXFIFO();
            return HLW_RX_FAILED;
        }
        response[response_count++] = value;
    }

    if(response_count == response_expected) {
        uint8_t i;
        uint8_t all_ff = 1;
        for(i = 0; i < (uint8_t)(response_expected - 1U); ++i) {
            if(response[i] != 0xFFU) all_ff = 0;
        }
        request_active = 0;
        if(all_ff) {
            rx_failure_reason = HLW8110_ERROR_ALL_FF;
            rx_failure_detail = response_expected;
            return HLW_RX_FAILED;
        }
        {
            uint8_t expected_checksum = HLW8110_UartChecksum(
                pending_command, response, (uint8_t)(response_expected - 1U));
            if(response[response_expected - 1U] != expected_checksum) {
                rx_failure_reason = HLW8110_ERROR_CHECKSUM;
                rx_failure_detail = ((uint32_t)response[response_expected - 1U] << 8) |
                                    expected_checksum;
                return HLW_RX_FAILED;
            }
        }
        return HLW_RX_OK;
    }

    if(elapsed(now, state_started_ms, HLW_RESPONSE_TIMEOUT_MS)) {
        request_active = 0;
        rx_failure_reason = HLW8110_ERROR_TIMEOUT;
        rx_failure_detail = ((uint32_t)response_count << 8) | response_expected;
        return HLW_RX_FAILED;
    }
    return HLW_RX_WAITING;
}

/**
 * @brief 发送特殊命令帧（写使能/写保护/通道选择）
 */
static uint8_t send_special(uint8_t special_command)
{
    uint8_t sum;
    if(R8_UART0_TFC > (UART_FIFO_SIZE - 4U)) return 0;
    sum = HLW8110_UartChecksum(HLW_SPECIAL_CMD, &special_command, 1U);
    UART0_CLR_RXFIFO();
    UART0_SendByte(HLW_FRAME_HEAD);
    UART0_SendByte(HLW_SPECIAL_CMD);
    UART0_SendByte(special_command);
    UART0_SendByte(sum);
    return 1;
}

/**
 * @brief 写 16 位寄存器（bit7 置 1 表示写）
 */
static uint8_t write_register_u16(uint8_t reg, uint16_t value)
{
    uint8_t command = (uint8_t)(reg | 0x80U);
    uint8_t data[2];
    uint8_t sum;
    if(R8_UART0_TFC > (UART_FIFO_SIZE - 5U)) return 0;
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
    sum = HLW8110_UartChecksum(command, data, 2U);
    UART0_CLR_RXFIFO();
    UART0_SendByte(HLW_FRAME_HEAD);
    UART0_SendByte(command);
    UART0_SendByte(data[0]);
    UART0_SendByte(data[1]);
    UART0_SendByte(sum);
    return 1;
}

/**
 * @brief 处理"读寄存器"类状态：发起请求→轮询响应→按状态推进
 *
 * 配置读回阶段（VERIFY_*）校验写入是否生效；校准阶段（CAL_*）保存
 * 功率/电流/电压系数；采样阶段（SAMPLE_*）把原始值换算为实际物理量。
 * 任何一步失败都进入整芯片恢复流程。
 */
static void service_read_state(uint8_t reg, uint8_t bytes, uint32_t now)
{
    hlw_rx_result_t result;
    uint32_t raw;

    if(!request_active) {
        if(!start_read(reg, bytes, now) &&
           elapsed(now, state_started_ms, HLW_RESPONSE_TIMEOUT_MS))
            begin_recovery(now, HLW8110_ERROR_TX_BUSY, R8_UART0_TFC);
        return;
    }
    result = poll_response(now);
    if(result == HLW_RX_WAITING) return;
    if(result == HLW_RX_FAILED) {
        begin_recovery(now, rx_failure_reason, rx_failure_detail);
        return;
    }

    switch(meter_state) {
    case HLW_STATE_CONFIG_READ_SYSCON:
        /* 读出当前 SYSCON，合入目标位（开 ADC1/ADC3、电流 PGA×16），其余位保持 */
        config_syscon = parse_be16(response);
        desired_syscon = (uint16_t)(config_syscon &
                                    (uint16_t)~(HLW_SYSCON_ADC1ON | HLW_SYSCON_ADC2ON |
                                                HLW_SYSCON_ADC3ON | HLW_SYSCON_PGAIA_MASK |
                                                HLW_SYSCON_PGAU_MASK));
        desired_syscon = (uint16_t)(desired_syscon | HLW_SYSCON_ADC1ON |
                                    HLW_SYSCON_ADC3ON | HLW_SYSCON_PGAIA_X16);
        meter_state = HLW_STATE_CONFIG_READ_EMUCON;
        break;
    case HLW_STATE_CONFIG_READ_EMUCON:
        /* EMUCON：关闭直流模式与高低通滤波器旁路，恢复交流测量 */
        config_emucon = parse_be16(response);
        desired_emucon = (uint16_t)(config_emucon &
                                    (uint16_t)~(HLW_EMUCON_DC_MODE | HLW_EMUCON_HPFIA_OFF |
                                                HLW_EMUCON_HPFU_OFF));
        meter_state = HLW_STATE_CONFIG_READ_EMUCON2;
        break;
    case HLW_STATE_CONFIG_READ_EMUCON2:
        /* EMUCON2：选内部基准，关闭波形/峰值捕获与 B 通道 */
        config_emucon2 = parse_be16(response);
        desired_emucon2 = (uint16_t)((config_emucon2 &
                                      (uint16_t)~(HLW_EMUCON2_CHS_IB | HLW_EMUCON2_WAVE_EN |
                                                  HLW_EMUCON2_PEAK_EN)) |
                                     HLW_EMUCON2_VREF_SEL);
        meter_state = HLW_STATE_CONFIG_UNLOCK;
        break;
    case HLW_STATE_CONFIG_VERIFY_SYSCON:
        raw = parse_be16(response);
        if(raw != desired_syscon) { begin_recovery(now, HLW8110_ERROR_CONFIG_VERIFY, (raw << 16) | desired_syscon); return; }
        meter_state = HLW_STATE_CONFIG_VERIFY_EMUCON;
        break;
    case HLW_STATE_CONFIG_VERIFY_EMUCON:
        raw = parse_be16(response);
        if(raw != desired_emucon) { begin_recovery(now, HLW8110_ERROR_CONFIG_VERIFY, (raw << 16) | desired_emucon); return; }
        meter_state = HLW_STATE_CONFIG_VERIFY_EMUCON2;
        break;
    case HLW_STATE_CONFIG_VERIFY_EMUCON2:
        raw = parse_be16(response);
        if(raw != desired_emucon2) { begin_recovery(now, HLW8110_ERROR_CONFIG_VERIFY, (raw << 16) | desired_emucon2); return; }
        meter_state = HLW_STATE_CAL_RMS_IAC;
        break;
    case HLW_STATE_CAL_RMS_IAC:
        /* 校准系数：电流有效值（0 或全 FF 视为无效） */
        rms_iac = parse_be16(response);
        if(rms_iac == 0U || rms_iac == 0xFFFFU) { begin_recovery(now, HLW8110_ERROR_COEFFICIENT, rms_iac); return; }
        meter_state = HLW_STATE_CAL_RMS_UC;
        break;
    case HLW_STATE_CAL_RMS_UC:
        /* 校准系数：电压有效值 */
        rms_uc = parse_be16(response);
        if(rms_uc == 0U || rms_uc == 0xFFFFU) { begin_recovery(now, HLW8110_ERROR_COEFFICIENT, rms_uc); return; }
        meter_state = HLW_STATE_CAL_POWER_PAC;
        break;
    case HLW_STATE_CAL_POWER_PAC:
        /* 校准系数：功率 */
        power_pac = parse_be16(response);
        if(power_pac == 0U || power_pac == 0xFFFFU) { begin_recovery(now, HLW8110_ERROR_COEFFICIENT, power_pac); return; }
        meter_state = HLW_STATE_SELECT_A;
        break;
    case HLW_STATE_SAMPLE_POWER:
        /* 采样功率：32 位原始值 → W×10 */
        sample_power_w_x10 = HLW8110_CalcPowerX10(parse_be32(response), power_pac);
        meter_state = HLW_STATE_SAMPLE_CURRENT;
        break;
    case HLW_STATE_SAMPLE_CURRENT:
        raw = parse_be24(response);
        if(!HLW8110_CalcCurrentMa(raw, rms_iac, &sample_current_ma)) {
            reject_sample(now, HLW8110_ERROR_CURRENT_RANGE, raw);
            return;
        }
        meter_state = HLW_STATE_SAMPLE_VOLTAGE;
        break;
    case HLW_STATE_SAMPLE_VOLTAGE:
        raw = parse_be24(response);
        if(!HLW8110_CalcVoltageDv(raw, rms_uc, &sample_voltage_dv)) {
            reject_sample(now, HLW8110_ERROR_VOLTAGE_RANGE, raw);
            return;
        }
        /* 三个量全部通过校验后才发布本轮采样 */
        meter_status.power_w_x10 = sample_power_w_x10;
        meter_status.current_ma = sample_current_ma;
        meter_status.voltage_dv = sample_voltage_dv;
        if(!meter_status.valid) {
            PRINT("HLW8110 online: U=%u.%uV I=%umA P=%u.%uW errors=%u\r\n",
                  meter_status.voltage_dv / 10U, meter_status.voltage_dv % 10U,
                  meter_status.current_ma,
                  meter_status.power_w_x10 / 10U, meter_status.power_w_x10 % 10U,
                  meter_status.communication_errors);
        }
        meter_status.valid = 1;
        meter_status.consecutive_errors = 0;
        meter_status.last_sample_ms = now;
        Dev.loadPower = meter_status.power_w_x10;
        Dev.errorCode.bit.power = 0;
        meter_state = HLW_STATE_IDLE;
        next_sample_ms = now + HLW_SAMPLE_INTERVAL_MS;
        break;
    default:
        begin_recovery(now, HLW8110_ERROR_STATE, meter_state);
        break;
    }
    state_started_ms = now;
}

/**
 * @brief 初始化计量模块：复位引脚拉低，进入整芯片复位流程
 */
void HLW8110_Init(void)
{
    memset(&meter_status, 0, sizeof(meter_status));
    rms_iac = 0;
    rms_uc = 0;
    power_pac = 0;
    sample_power_w_x10 = 0;
    sample_current_ma = 0;
    sample_voltage_dv = 0;
    request_active = 0;
    GPIOPinRemap(DISABLE, RB_PIN_UART0);
    GPIOB_ModeCfg(GPIO_Pin_7, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(GPIO_Pin_7);
    meter_state = HLW_STATE_RESET_LOW;
    state_started_ms = CurTick;
}

/**
 * @brief 计量状态机主轮询（每 20ms 由 Period_20ms 调用，非阻塞）
 *
 * 各状态通过 CurTick 时间戳推进，不忙等：
 *   RESET_LOW(12ms) → BOOT_WAIT(100ms) → 配置读/写/校验 → 校准 → IDLE
 *   IDLE 每 2 秒进入一轮 SAMPLE_* 采样
 *   异常统一走 begin_recovery 或 reject_sample
 */
void HLW8110_Poll(void)
{
    uint32_t now = CurTick;

    switch(meter_state) {
    case HLW_STATE_RESET_LOW:
        /* 拉低 RX 复位引脚并保持 12ms（手册要求 >9.15ms） */
        if(!elapsed(now, state_started_ms, HLW_RECOVERY_LOW_MS)) return;
        GPIOB_SetBits(GPIO_Pin_7);
        uart_configure();
        meter_state = HLW_STATE_BOOT_WAIT;
        state_started_ms = now;
        return;
    case HLW_STATE_BOOT_WAIT:
        /* 芯片上电后等待 100ms 再开始配置 */
        if(!elapsed(now, state_started_ms, HLW_BOOT_WAIT_MS)) return;
        meter_state = HLW_STATE_CONFIG_READ_SYSCON;
        state_started_ms = now;
        return;
    case HLW_STATE_CONFIG_READ_SYSCON:
        service_read_state(HLW_REG_SYSCON, 2U, now);
        return;
    case HLW_STATE_CONFIG_READ_EMUCON:
        service_read_state(HLW_REG_EMUCON, 2U, now);
        return;
    case HLW_STATE_CONFIG_READ_EMUCON2:
        service_read_state(HLW_REG_EMUCON2, 2U, now);
        return;
    case HLW_STATE_CONFIG_UNLOCK:
        /* 写使能（解除寄存器写保护） */
        if(!tx_progress_or_recover(send_special(HLW_WRITE_ENABLE), now)) return;
        meter_state = HLW_STATE_CONFIG_WRITE_SYSCON;
        state_started_ms = now;
        return;
    case HLW_STATE_CONFIG_WRITE_SYSCON:
        /* 等 TX 静默后回写配置；与当前值相同则跳过写操作 */
        if(!elapsed(now, state_started_ms, HLW_TX_SETTLE_MS)) return;
        if(desired_syscon != config_syscon &&
           !tx_progress_or_recover(write_register_u16(HLW_REG_SYSCON, desired_syscon), now)) return;
        meter_state = HLW_STATE_CONFIG_WRITE_EMUCON;
        state_started_ms = now;
        return;
    case HLW_STATE_CONFIG_WRITE_EMUCON:
        if(!elapsed(now, state_started_ms, HLW_TX_SETTLE_MS)) return;
        if(desired_emucon != config_emucon &&
           !tx_progress_or_recover(write_register_u16(HLW_REG_EMUCON, desired_emucon), now)) return;
        meter_state = HLW_STATE_CONFIG_WRITE_EMUCON2;
        state_started_ms = now;
        return;
    case HLW_STATE_CONFIG_WRITE_EMUCON2:
        if(!elapsed(now, state_started_ms, HLW_TX_SETTLE_MS)) return;
        if(desired_emucon2 != config_emucon2 &&
           !tx_progress_or_recover(write_register_u16(HLW_REG_EMUCON2, desired_emucon2), now)) return;
        meter_state = HLW_STATE_CONFIG_SELECT_A;
        state_started_ms = now;
        return;
    case HLW_STATE_CONFIG_SELECT_A:
        /* 选择通道 A（电流采样通道） */
        if(!elapsed(now, state_started_ms, HLW_TX_SETTLE_MS)) return;
        if(!tx_progress_or_recover(send_special(HLW_SELECT_CHANNEL_A), now)) return;
        meter_state = HLW_STATE_CONFIG_LOCK;
        state_started_ms = now;
        return;
    case HLW_STATE_CONFIG_LOCK:
        /* 重新写保护 */
        if(!elapsed(now, state_started_ms, HLW_TX_SETTLE_MS)) return;
        if(!tx_progress_or_recover(send_special(HLW_WRITE_PROTECT), now)) return;
        meter_state = HLW_STATE_CONFIG_SETTLE;
        state_started_ms = now;
        return;
    case HLW_STATE_CONFIG_SETTLE:
        /* 配置生效等待 */
        if(!elapsed(now, state_started_ms, HLW_CONFIG_SETTLE_MS)) return;
        meter_state = HLW_STATE_CONFIG_VERIFY_SYSCON;
        state_started_ms = now;
        return;
    case HLW_STATE_CONFIG_VERIFY_SYSCON:
        service_read_state(HLW_REG_SYSCON, 2U, now);
        return;
    case HLW_STATE_CONFIG_VERIFY_EMUCON:
        service_read_state(HLW_REG_EMUCON, 2U, now);
        return;
    case HLW_STATE_CONFIG_VERIFY_EMUCON2:
        service_read_state(HLW_REG_EMUCON2, 2U, now);
        return;
    case HLW_STATE_CAL_RMS_IAC:
        service_read_state(HLW_REG_RMS_IAC, 2U, now);
        return;
    case HLW_STATE_CAL_RMS_UC:
        service_read_state(HLW_REG_RMS_UC, 2U, now);
        return;
    case HLW_STATE_CAL_POWER_PAC:
        service_read_state(HLW_REG_POWER_PAC, 2U, now);
        return;
    case HLW_STATE_SELECT_A:
        /* 校准完成后再次选通道 A，进入空闲采样 */
        if(!tx_progress_or_recover(send_special(HLW_SELECT_CHANNEL_A), now)) return;
        meter_state = HLW_STATE_IDLE;
        next_sample_ms = now;
        state_started_ms = now;
        return;
    case HLW_STATE_IDLE:
        /* 空闲等待采样间隔（2 秒） */
        if((int32_t)(now - next_sample_ms) < 0) return;
        meter_state = HLW_STATE_SAMPLE_POWER;
        state_started_ms = now;
        return;
    case HLW_STATE_SAMPLE_POWER:
        service_read_state(HLW_REG_POWER_PA, 4U, now);
        return;
    case HLW_STATE_SAMPLE_CURRENT:
        service_read_state(HLW_REG_RMS_IA, 3U, now);
        return;
    case HLW_STATE_SAMPLE_VOLTAGE:
        service_read_state(HLW_REG_RMS_U, 3U, now);
        return;
    default:
        begin_recovery(now, HLW8110_ERROR_STATE, meter_state);
        return;
    }
}

/**
 * @brief 获取计量状态快照（供上报与诊断）
 */
const HLW8110_Status_t *HLW8110_GetStatus(void)
{
    return &meter_status;
}
