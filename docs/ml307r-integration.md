# ML307R MQTT 与 OTA

## 接线与产品边界

- CH583 UART1：PA9 TX → ML307R 主 AT 口 UART0_RXD，PA8 RX ← UART0_TXD，PB14 → RESET。
- 使用普通 MQTT/TCP 和 HTTP，不使用 TLS、证书、用户名、密码、JSON 或 MCU 加密库。
- MQTT 业务载荷直接复用固定 LoRa 报文的连续 Hex；LoRa 空口和网关协议不变。
- CRC16/CRC32 只校验传输损坏，不提供公网链路防篡改能力。

## MQTT

设备 ID 由 CH583 的 6 字节 UID 派生为 `SACxxxxxxxxxxxx`。Client ID 留空时自动使用设备 ID，也可由内部人员在管理页覆盖。

| 主题 | QoS | 内容 |
|---|---:|---|
| `{prefix}/{uid}/u` | 可配置 | 固定状态/控制结果报文 Hex |
| `{prefix}/{uid}/d` | 0 | 固定控制报文 Hex |
| `{prefix}/{uid}/m/u` | 1 | OTA 结果 Hex |
| `{prefix}/{uid}/m/d` | 1 | OTA 指令 Hex |

业务控制不缓存、不去重，每个节点 ID 和 CRC 有效的下行报文执行一次。管理页配置 APN、PDP 类型、Broker、端口、Client ID、主题前缀、保活、上报周期、QoS 和 Clean Session。Broker 未配置时模组仍完成基础初始化并保留 UART1 AT 调试能力。

失败恢复始终继续：5/10/20/40/60/120/240/300 秒退避，之后固定 300 秒。先重连 MQTT，再恢复网络，最后硬复位模组。管理页保留当前阶段、SIM/网络/MQTT 状态、信号、最近错误、重试时间和 UART 收发量，不周期读取 IMEI、ICCID 等非运行必需信息。

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

远程 OTA URL 必须是 `http://` 且直接指向链接地址为 `0x1000` 的应用 `.bin`。ML307R 使用 HTTP Range 分块下载，CH583 每 4 KB 保存一次断点并在完整下载后计算 CRC32。校验完成后等待安装指令。

## 固件分区与安装

Flash 地址不变：入口 `0x00000/4 KB`、运行应用 `0x01000/216 KB`、暂存区 `0x37000/216 KB`、Updater `0x6D000/12 KB`。应用上限为 208 KB。

应用始终从 `0x1000` 运行，`0x37000` 只保存待升级镜像。收到安装指令后 updater 校验暂存镜像、复制到运行区、再次校验并启动。复制阶段掉电时安装标记仍保留，重启后从完整暂存镜像重新复制；没有试运行、健康确认、双链接镜像或运行槽切换状态机。

## 构建与打包

```powershell
platformio run -e ch583 -e ch583_updater -e ch583_jump
python tools/splitac_production.py factory --output firmware-factory.hex
python tools/splitac_production.py package `
  --app BLE/Peripheral/.pio/build/ch583/firmware.bin `
  --version 2.21.0 --output firmware.sacfw `
  --remote-output firmware.bin
```

- `firmware-factory.hex`：WCH-Link 首次烧录，包含入口、运行应用和 updater，不覆盖 DataFlash。
- `firmware.sacfw`：微信小程序 BLE OTA 选择的单镜像升级包。
- `firmware.bin`：放在 HTTP 服务器供 4G OTA 下载。

量产配置仍可使用 `splitac_production.py provision` 生成 DataFlash 镜像；不生成密码、凭据或二维码。
