# 上下位机联调诊断报告

> 配合固件 v4.0 + 上位机 SplitAC_Host 最新版
> 报告日期: 2026-06-26

## 一、项目结构

```
F:\code\
├── Fanciol_Ctl\                  # 下位机固件 (CH583)
│   └── BLE\Peripheral\APP\
│       ├── peripheral.c        # BLE 协议处理
│       ├── util.c               # LoRa 协议处理
│       └── include\board.h      # t_dev 数据结构
└── SplitAC_Host\                # 上位机微信小程序
    ├── utils\
    │   ├── protocol.js          # 协议构造/解析
    │   └── bluetooth_helper.js  # BLE 通信封装
    └── pages\
        ├── sysparam\           # 系统参数页
        ├── control\            # 设备控制页
        └── ... 其他页面
```

## 二、当前协议对齐状态

### 2.1 PID 枚举对齐

| PID 名称 | 下位机 (peripheral.h) | 上位机 (protocol.js) | 文档 (上位机蓝牙协议实现文档.md) | 状态 |
|---------|---------------------|---------------------|---------------------------------|------|
| SWITCH | 0x01 | 0x01 | 0x01 | ✅ |
| MODE | 0x02 | 0x02 | 0x02 | ✅ |
| TEMP_SET | 0x03 | 0x03 | 0x03 | ✅ |
| TEMP_ROOM | 0x04 | 0x04 | 0x04 | ✅ |
| FAN_SPEED | 0x05 | 0x05 | 0x05 | ✅ |
| LOCK | 0x06 | 0x06 | 0x06 | ✅ |
| ERROR | 0x07 | 0x07 | 0x07 | ✅ |
| LORA_CFG | 0x10 | 0x10 | 0x10 | ✅ |
| IR_CFG | 0x11 | 0x11 | 0x11 | ✅ |
| DEV_INFO | 0x12 | 0x12 | 0x12 | ✅ |
| SYS_PARAMS | 0x13 | 0x13 | **0x14** | ⚠️ 文档不一致 |
| SYS_CTRL | 0x14 | 0x14 | **0x15** | ⚠️ 文档不一致 |
| IR_MATCH | 0x15 | 0x15 | - | ⚠️ 文档缺失 |
| IR_LEARN | 0x16 | 0x16 | - | ⚠️ 文档缺失 |
| IR_LEARN_LIST | 0x17 | 0x17 | - | ⚠️ 文档缺失 |
| **RELAY_CFG** | **0x18** | ❌ **未实现** | ❌ 未文档化 | ❌ **新增** |

**问题**：上位机 protocol.js 的 PID 枚举不包含 PID_RELAY_CFG = 0x18，新功能无法被上位机识别。

### 2.2 协议文档与代码差异

文档 `上位机蓝牙协议实现文档.md` 中:
- 第 71-72 行：`SYS_PARAMS = 0x13`, `SYS_CTRL = 0x15`
- 但实际代码 `protocol.js` 中：`SYS_PARAMS = 0x13`, `SYS_CTRL = 0x14`
- 实际下位机 `peripheral.h`：`SYS_PARAMS = 0x13`, `SYS_CTRL = 0x14`

**结论**：上位机代码与下位机一致，**文档写错了**。建议修复文档。

### 2.3 SYS_PARAMS 字段顺序对齐

**下位机 peripheral.c 第 893-913 行** (READ 返回顺序):
```
nodeId(2) + channel(1) + loraStatus(1) + scanCycle(1) + irType(2) + 
irIdx(1) + irActType(1) + mode(1) + errorCode(2) + tempRoom(2) + 
runTime(2) + loadPower(2)
```

**上位机 protocol.js parseSysParams**:
```
nodeId(2) + loraChannel(1) + loraStatus(1) + scanCycle(1) + irType(2) + 
irIdx(1) + irActType(1) + workMode(1) + errorCode(2) + tempRoom(2) + 
runTime(2) + loadPower(2)
```

✅ **完全一致** (irIdx 与 modelIndex 是同一字段的不同命名)

### 2.4 SYS_CTRL 字段顺序对齐

**下位机 peripheral.c 第 810-820 行**:
```
nodeId(2) + channel(1) + mode(1)  // 总4字节
```

**上位机 sysparam.js saveSysParams**:
```
nodeId(2) + loraChannel(1) + workMode(1)  // 总4字节
```

✅ **完全一致**

### 2.5 协议.js 中"不存在的"PID

上位机 `protocol.js` buildPayload/parseValue 中包含了 `PID.NODE_ID`, `PID.LORA_CHANNEL`, `PID.SCAN_CYCLE`, `PID.DEV_TYPE`, `PID.DEV_VER`, `PID.WORK_MODE`, `PID.IR_BRAND`, `PID.IR_TYPE`, `PID.IR_CHANNEL`, `PID.IR_MATCH`, `PID.IR_LEARN`, `PID.LORA_ENABLE`, `PID.OTA_ENABLE`, `PID.TIMESTAMP` 等 PID 定义, 但下位机 peripheral.h 中**没有这些 PID 枚举值**。

**实际使用情况**：
- `protocol.js` 第 229 行开始的 PID 枚举只定义到 0x0F (LOCK/ERROR) 和 0x10-0x17 + 0xF0
- 但在 buildPayload/parseValue 中引用了 `PID.NODE_ID`, `PID.LORA_CHANNEL` 等

**问题**：这些 PID 是历史遗留的死代码，实际构建/解析逻辑都不依赖它们。实际写入用 `SYS_CTRL` PID 统一处理。

### 2.6 BLE 字节序

下位机和上位机均使用**小端模式** (Little Endian) ✅

### 2.7 CRC 算法对齐

下位机 `util.c` AddCrc/ChkCrc 与上位机 `protocol.js` calcCrc 完全一致：
```js
// 双方都是 sum + 0xEC & 0xFF
```
✅

### 2.8 ACK 帧格式

**下位机 peripheral.c 第 825 行**: WRITE 成功后发送 `[LEN=3][0x04][CRC]`，3字节空 ACK
**上位机 protocol.js parseResponse**: 检测 `frame.cmd === CMD.ACK` 即认为成功

✅ 一致

## 三、上位机缺失的新功能支持

下位机固件已经实现了以下功能，但上位机还未对应：

### 3.1 PID_RELAY_CFG = 0x18 (中继配置) - **本次新增**

| 项目 | 状态 |
|------|------|
| 下位机固件 | ✅ 已实现 (peripheral.c PID_RELAY_CFG R/W) |
| 下位机文档 | ✅ docs/lora-relay-design-v2.md |
| 上位机 protocol.js | ❌ 未实现 |
| 上位机 UI 页面 | ❌ 缺失 |
| 上位机开发文档 | ✅ docs/ble-relay-host-integration.md |

### 3.2 PID_IR_LEARN = 0x16 (红外学习)

下位机 peripheral.c 第 962-977 行已实现红外学习启动，上位机需要添加：
- `action ACT_IR_LEARN` 触发
- 监听 NOTIFY 返回的学习结果

### 3.3 PID_IR_LEARN_LIST = 0x17 (学习通道列表)

下位机 peripheral.c 第 914-918 行实现读取 10 字节 enable 状态，上位机需添加 READ 命令。

### 3.4 cmd=0x0E ACK 包 (中继子节点响应)

下位机固件保留 cmd=0x0E 协议扩展空间，但当前未主动发送 ACK（IR 控制部分仍是占位实现）。

## 四、上位机需要做的修改

### 4.1 必须修改

**修改文件**: `F:\code\SplitAC_Host\utils\protocol.js`

1. 在 PID 枚举中添加:
```js
RELAY_CFG: 0x18,  // 中继配置 (新增)
```

2. 在 buildPayload 中添加:
```js
case PID.RELAY_CFG: {
  // value: { linkRole: 0|1|2, parentRelayId: uint16 }
  const linkRole = value.linkRole & 0xFF;
  const parentId = value.parentRelayId & 0xFFFF;
  payload.push(linkRole, parentId & 0xFF, (parentId >> 8) & 0xFF);
  break;
}
```

3. 在 parseNotifyData 中添加解析逻辑:
```js
if (data[0] === PID.RELAY_CFG && data.length >= 8) {
  return parseRelayConfig(data.slice(1));
}

function parseRelayConfig(data) {
  return {
    linkRole: data[0],
    hopCount: data[1],
    parentRelayId: data[2] | (data[3] << 8),
    childCount: data[4],
    childBitmap: data[5] | (data[6] << 8)
  };
}
```

4. 修复文档 `上位机蓝牙协议实现文档.md`：
- PID 表中 `SYS_PARAMS = 0x13`, `SYS_CTRL = 0x14` (不是 0x13/0x15)
- 添加 `PID_RELAY_CFG = 0x18` 行
- 添加 IR_LEARN / IR_LEARN_LIST / IR_MATCH 详细说明

### 4.2 建议修改 (新增页面)

**新建文件**: `pages/relay/relay.{js,json,wxml,wxss}`

参考 `docs/ble-relay-host-integration.md` 第 4 节"微信小程序 UI 建议"。

入口: 在 `sysparam.wxml` 添加"中继配置"入口按钮。

## 五、协议差异速查表

| 协议特性 | 下位机 | 上位机 | 差异 |
|---------|--------|--------|------|
| 帧格式 | [LEN][CMD][PAYLOAD][CRC] | 同 | ✅ |
| 端序 | 小端 | 小端 | ✅ |
| CRC 算法 | sum+0xEC | sum+0xEC | ✅ |
| CMD 类型 | 6 种 (WRITE/READ/NOTIFY/ACK/ERROR/ACTION) | 同 | ✅ |
| Action ID | 6 种 (RESET/IR_MATCH/IR_LEARN/IR_SEND/SAVE_PARAMS/IR_LEARN_SEND) | 6 种 (RESET/IR_MATCH/IR_LEARN/IR_SEND/SAVE_PARAMS/IR_LEARN_SEND) | ✅ |
| PID 已实现 | 0x01-0x18 (16个) | 0x01-0x17 + 0xF0 (16个) | ❌ 缺 0x18 |
| 中继功能 | ✅ 已实现 | ❌ 缺失 | ❌ |

## 六、修改建议优先级

### 高优先级 (P0) - 立即做
- [ ] 上位机 protocol.js 添加 PID.RELAY_CFG 枚举和解析
- [ ] 新增 relay 配置页面 (relay.js/wxml)
- [ ] sysparam 页面添加"中继配置"入口
- [ ] 修复文档 SYS_PARAMS/SYS_CTRL 编号错误

### 中优先级 (P1) - 后续完善
- [ ] IR_LEARN 学习的 UI 支持
- [ ] IR_LEARN_LIST 显示学习通道状态
- [ ] 中继子节点列表显示

### 低优先级 (P2) - 后续优化
- [ ] 清理 protocol.js 中的死代码 (PID.NODE_ID 等)
- [ ] protocol.js 与 docs 下文档字段对齐

## 七、调试日志参考

```
典型上位机发送 → 下位机接收:
上位机: 04 02 13 06    // READ SYS_PARAMS (PID=0x13)
下位机: 12 03 13 [17字节数据] [CRC]    // NOTIFY 响应

典型 WRITE:
上位机: 07 01 14 01 00 05 00 [CRC]   // WRITE SYS_CTRL, nodeId=0x0001, channel=5, mode=0
下位机: 03 04 [CRC]                  // ACK 响应
```

## 八、本次中继方案需要上位机配合的部分

| 改动 | 文档位置 | 必做/选做 |
|------|---------|----------|
| protocol.js 添加 PID.RELAY_CFG | 4.1 | 必做 |
| 新增 relay 配置页面 | 4.2 | 必做 |
| 修复协议文档 SYS_PARAMS/SYS_CTRL 编号 | 2.1 | 必做 |
| IR_LEARN UI 支持 | - | 选做 |
| IR_LEARN_LIST 显示 | - | 选做 |

完整对接文档: `F:\code\Fanciol_Ctl\docs\ble-relay-host-integration.md`