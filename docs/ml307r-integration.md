# ML307R MQTT 与 OTA

## 接线与产品边界

- CH583 UART1：PA9 TX → ML307R 主 AT 口 UART0_RXD，PA8 RX ← UART0_TXD，PB5 控制 4G 电源（高电平开机），不再连接 RESET。
- 使用普通 MQTT/TCP 和 HTTP，不使用 TLS、证书或 MCU 加密库。MQTT 使用平台提供的普通用户名/密码；业务、设备登记和 OTA 均直接发送原始二进制帧。
- MQTT 业务载荷直接复用固定 LoRa 报文，不再转换成 Hex 字符串；LoRa 空口和网关协议不变。
- CRC16/CRC32 只校验传输损坏，不提供公网链路防篡改能力。

## MQTT

产线默认按 `A + 年月(YYMM) + nodeId(4 位大写十六进制)` 生成设备 ID，例如 `A26091001`。管理页可将前缀改为自定义的 0–5 位十六进制字符，后四位始终由 nodeId 生成，例如前缀 `BAF`、节点 `0x1421` 得到 `BAF1421`。现场修改 nodeId 后，完整 BLE 广播名称、MQTT Client ID 和主题设备段同步变化；修改前缀通过现有通信配置双槽保存，不改变 DataFlash 格式。订阅主题校验与重连抖动使用实际 ID 长度。

| 主题 | QoS | 内容 |
|---|---:|---|
| `pub/ac/{uid}` | 配置值/1 | 状态、控制结果、设备登记和 OTA 状态原始二进制帧 |
| `sub/ac/{uid}` | 发布端决定 | 业务控制 QoS 0；OTA 指令 QoS 1 |

默认 Broker 为 `118.178.128.26:1883`，用户名 `ecac`，密码 `ecac2026`。管理页可修改 Broker、端口、用户名、密码、发布/订阅主题模板和网络参数；每个主题模板必须恰好包含一个 `{uid}`。Client ID 始终使用设备 UID，不单独配置。业务控制不缓存、不去重，每个节点 ID 和校验有效的下行报文执行一次。

上电时依次使用 `AT+CGSN=1` 和 `AT+MCCID` 读取 IMEI、ICCID，网络注册后使用 `AT+COPS?` 读取运营商。查询失败不会阻塞联网；信息保存在 RAM，不周期读取。设备只订阅自己的下行主题。每次 MQTT 订阅完成后，在上行主题使用 QoS 1 发布一次非 retained 紧凑登记帧，之后继续普通状态上报；服务端依靠 MySQL 保存登记信息，避免后端重连时 Broker 重放所有设备信息。

失败恢复始终继续：5/10/20/40/60/120/240/300 秒退避，之后固定 300 秒。先重连 MQTT，再恢复网络，最后硬复位模组。管理页保留当前阶段、SIM/网络/MQTT 状态、信号、最近错误、重试时间和 UART 收发量。

时间戳在 BLE、LoRa 和外部 DS1302 内部统一为 Unix UTC 秒；小程序显示和本地智控的时段、日期、星期统一为北京时间（UTC+8）。现场 ML307R 的 `CCLK` 日期字段已是 UTC，直接转为 Unix 秒；即使带 `+32` 网络时区后缀，也不能再减 8 小时。解析同时支持无时区后缀的 UTC 返回。自动对时只初始化无效 RTC 或修正有效 RTC 的 3–300 秒误差；超过 5 分钟的网络偏差不覆盖有效的电池保持时钟，避免模组/网关时区错误使本地策略错时执行。手动手机同步不受此偏差限制；已存在错误时间的设备升级后需手动同步一次。

## 远程 OTA 管理帧

上行和下行主题中以 `C7` 开头的载荷为管理帧：

`C7 | 01 | type | flags | transactionId:u16LE | payloadLength:u16LE | payload | CRC16-CCITT-FALSE:u16LE`

| type | 用途 | payload |
|---:|---|---|
| 1 | 设备登记 | `firmware:u32 + hardware/IMEI/ICCID/operator（各为length:u8 + ASCII）` |
| 4 | 操作结果 | `requestType + status` |
| 5 | OTA 提议 | `version:u32 + size:u32 + crc32:u32 + urlLength:u8 + httpUrl` |
| 6 | OTA 安装 | `version:u32` |
| 7 | OTA 取消 | 空 |
| 8 | OTA 状态 | 空查询或状态结果 |

远程 OTA URL 必须是 `http://` 且直接指向链接地址为 `0x1000` 的应用 `.bin`。ML307R 使用 8 KB 缓存 HTTP 模式，每次请求带 4 KB Range；收到响应后先用 `AT+MHTTPREAD=<id>,0,<len>` 读出并丢弃本次响应头，再用类型 `1` 读取正文，避免多个 Range 响应头在模组缓存中累积。CH583 使用固定 240 字节 RAM 缓冲接收原始二进制并写入暂存区，每累计 16 KB 保存断点并主动上报进度，完整下载后计算 CRC32，校验完成后等待安装指令。

单个分片异常时销毁并重建 HTTP 会话，分别等待 2、5、10 秒重试三次；续传前只擦除尚未确认的当前 4 KB，不改动已确认分片。连续失败后任务暂停但 MQTT 和设备控制继续运行，再次下发完全相同的版本、大小、CRC32 和 URL 即从最近 16 KB 断点恢复；不同固件必须先取消。MCU 在下载或校验期间意外复位时，同样从最近断点恢复或重新执行完整 CRC 校验。

## 固件分区与安装

Flash 地址不变：入口 `0x00000/4 KB`、运行应用 `0x01000/216 KB`、暂存区 `0x37000/216 KB`、Updater `0x6D000/12 KB`。应用上限为 208 KB。

应用始终从 `0x1000` 运行，`0x37000` 只保存待升级镜像。收到安装指令后 updater 校验暂存镜像、复制到运行区、再次校验并启动。复制阶段掉电时安装标记仍保留，重启后从完整暂存镜像重新复制；没有试运行、健康确认、双链接镜像或运行槽切换状态机。

安装器末尾不足四字节时补 `FF` 再按 DWORD 写入，CRC 只覆盖真实镜像长度。2026-10-10 修复版首次部署须烧录完整 `splitac-burn.hex` 更新安装器，保留 DataFlash；只升级应用 BIN 不会更新旧 Updater。

管理请求需串行发送并等待事务结果。设备只保留一个不可变在途快照和一个待发响应，断网时保留并重发；待发槽满时不执行后续管理请求，由请求端超时重试。OTA 激活只在对应成功结果发布完成后重启，不由其他状态发布完成触发。业务下行仍不去重。

## 构建与打包

```powershell
platformio run -e ch583 -e ch583_updater -e ch583_jump
python tools/splitac_production.py package --output-dir production-release
```

- `splitac-burn.hex`：唯一烧写固件，包含入口、运行应用和 updater，不覆盖 DataFlash。
- `splitac-update.bin`：唯一更新固件，供微信小程序 BLE OTA 与 HTTP 4G OTA 共用；支持升级、回退和同版本重刷。
- Jump、应用和 Updater 只是构建过程中的内部组件，不作为产品固件交付。
- 量产不保存一机一份的 DataFlash 文件。烧写脚本根据设备编号从统一 CSV 临时生成，写入、读回校验后立即删除。

`production.csv` 是量产版本的唯一来源，固定列为：

```csv
device_id,communication_mode,lora_channel,firmware_version,hardware_version
A26091001,lora,9,2.22.19,HW1.0
```

- `device_id` 同时作为完整 BLE 广播名称和 MQTT client ID；末四位就是 nodeId，工具直接解析，不再在 CSV 中重复维护 nodeId。
- `communication_mode` 仅允许 `lora`、`4g`、`both`，分别对应 LoRa、4G 或调试双链路。
- `lora_channel` 范围为 `0~32`；纯 4G 设备可填写 `0`。
- `firmware_version` 同时用于固件编译、BLE 设备信息、MQTT 登记和 OTA 当前版本。
- `hardware_version` 同时用于固件编译和 MQTT 登记，最长 12 个 ASCII 字符。
- 同一份 CSV 对应同一批构建，所有行的软件和硬件版本必须一致；工具不再提供写死的版本默认值。
- 不再生成重复的 `platform-import.csv`。MQTT client ID、主题、DataFlash 文件名和地址都由工具或固件派生，量产只维护这一份五列 CSV。

量产工位只需要输入或扫码得到 `device_id`：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_production.ps1 `
  -DeviceId A26091001
```

出厂默认写入上述平台参数；需要其他平台时可使用生成工具的 `--broker`、`--port`、`--username`、`--password`、`--publish-topic` 和 `--subscribe-topic` 参数，或在小程序管理页修改。烧写脚本会校验 CSV、写入并校验 `splitac-burn.hex`、临时生成并写入 DataFlash、读回校验、复位设备，最后清除临时文件。设备须先进入 USB Boot ISP 模式；`wchisp` 在 Windows 下使用 WinUSB 驱动。

不接硬件时可先验证工单和 CSV：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_production.ps1 `
  -DeviceId A26091001 -PrepareOnly
```
