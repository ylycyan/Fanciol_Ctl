# ML307R MQTT 与 OTA

## 接线与产品边界

- CH583 UART1：PA9 TX → ML307R 主 AT 口 UART0_RXD，PA8 RX ← UART0_TXD，PB14 → RESET。
- 使用普通 MQTT/TCP 和 HTTP，不使用 TLS、证书、用户名、密码或 MCU 加密库。业务和 OTA 仍为二进制 Hex；仅低频设备登记使用短 JSON。
- MQTT 业务载荷直接复用固定 LoRa 报文的连续 Hex；LoRa 空口和网关协议不变。
- CRC16/CRC32 只校验传输损坏，不提供公网链路防篡改能力。

## MQTT

设备 UID 由产线按 `SAC + 年月 + 产线/版本 + 7 位全局流水号` 生成，例如 `SAC2608A0000001`，并写入 DataFlash。该 UID 同时作为 MQTT Client ID、主题设备段和平台设备主键；普通配置和恢复出厂不修改它。

| 主题 | QoS | 内容 |
|---|---:|---|
| `{prefix}/{uid}/u` | 可配置 | 固定状态/控制结果报文 Hex |
| `{prefix}/{uid}/d` | 0 | 固定控制报文 Hex |
| `{prefix}/{uid}/m/u` | 1 | OTA 结果 Hex |
| `{prefix}/{uid}/m/d` | 1 | OTA 指令 Hex |
| `{prefix}/{uid}/info` | 1 | MCU版本、硬件版本、IMEI、ICCID、运营商 JSON |

业务控制不缓存、不去重，每个节点 ID 和 CRC 有效的下行报文执行一次。管理页配置 APN、PDP 类型、Broker、端口、Client ID、主题前缀、保活、上报周期、QoS 和 Clean Session。Broker 未配置时模组仍完成基础初始化并保留 UART1 AT 调试能力。

上电时依次使用 `AT+CGSN=1` 和 `AT+MCCID` 读取 IMEI、ICCID，网络注册后使用 `AT+COPS?` 读取运营商。查询失败不会阻塞联网；信息保存在 RAM，不周期读取。每次 MQTT 订阅完成后使用 QoS 1 发布一次非 retained 登记，之后继续普通状态上报；服务端依靠 MySQL 保存登记信息，避免后端重连时 EMQX 重放所有设备信息。

失败恢复始终继续：5/10/20/40/60/120/240/300 秒退避，之后固定 300 秒。先重连 MQTT，再恢复网络，最后硬复位模组。管理页保留当前阶段、SIM/网络/MQTT 状态、信号、最近错误、重试时间和 UART 收发量。

## 远程 OTA 管理帧

管理主题载荷为：

`C7 | 01 | type | flags | transactionId:u16LE | payloadLength:u16LE | payload | CRC16-CCITT-FALSE:u16LE`

| type | 用途 | payload |
|---:|---|---|
| 4 | 操作结果 | `requestType + status` |
| 5 | OTA 提议 | `version:u32 + size:u32 + crc32:u32 + urlLength:u8 + httpUrl` |
| 6 | OTA 安装 | `version:u32` |
| 7 | OTA 取消 | 空 |
| 8 | OTA 状态 | 空查询或状态结果 |

远程 OTA URL 必须是 `http://` 且直接指向链接地址为 `0x1000` 的应用 `.bin`。ML307R 使用 8 KB 缓存 HTTP 模式，每次请求带 4 KB Range；收到响应后先用 `AT+MHTTPREAD=<id>,0,<len>` 读出并丢弃本次响应头，再用类型 `1` 读取正文，避免多个 Range 响应头在模组缓存中累积。CH583 使用固定 240 字节 RAM 缓冲接收原始二进制并写入暂存区，每 4 KB 保存一次断点，完整下载后计算 CRC32，校验完成后等待安装指令。

单个分片异常时销毁并重建 HTTP 会话，分别等待 2、5、10 秒重试三次；续传前只擦除尚未确认的当前 4 KB，不改动已确认分片。连续失败后任务暂停但 MQTT 和设备控制继续运行，再次下发完全相同的版本、大小、CRC32 和 URL 即从断点恢复；不同固件必须先取消。MCU 在下载或校验期间意外复位时，同样从最后完整的 4 KB 恢复或重新执行完整 CRC 校验。

## 固件分区与安装

Flash 地址不变：入口 `0x00000/4 KB`、运行应用 `0x01000/216 KB`、暂存区 `0x37000/216 KB`、Updater `0x6D000/12 KB`。应用上限为 208 KB。

应用始终从 `0x1000` 运行，`0x37000` 只保存待升级镜像。收到安装指令后 updater 校验暂存镜像、复制到运行区、再次校验并启动。复制阶段掉电时安装标记仍保留，重启后从完整暂存镜像重新复制；没有试运行、健康确认、双链接镜像或运行槽切换状态机。

## 构建与打包

```powershell
platformio run -e ch583 -e ch583_updater -e ch583_jump
python tools/splitac_production.py factory --output firmware-factory.hex
python tools/splitac_production.py ota `
  --app BLE/Peripheral/.pio/build/ch583/firmware.bin `
  --version 2.22.0 --output firmware-2.22.0.bin
```

- `firmware-factory.hex`：WCH-Link 首次烧录，包含入口、运行应用和 updater，不覆盖 DataFlash。
- `firmware-2.22.0.bin`：微信小程序 BLE OTA 与 HTTP 4G OTA 共用的原始应用镜像。

量产配置仍可使用 `splitac_production.py provision` 生成 DataFlash 镜像；不生成密码、凭据或二维码。
