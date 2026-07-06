# BLE 中继功能对接文档 — 上位机/微信小程序开发用

> 配合固件版本: v4.0 (LoRa 多跳中继方案)
> 文档版本: v1.0 | 日期: 2026-06-26

---

## 1. 概述

本次升级新增 **LoRa 多跳中继** 功能。微信小程序端需要新增 **中继配置页面**，用于将某个板子设为中继节点，或将子节点指定到某个中继下。

所有配置**完全通过 BLE 完成**，不需要网关配合，不需要服务器协议升级。

### 新增 PID

| PID | 名称 | 读/写 | 说明 |
|-----|------|-------|------|
| **0x18** | **PID_RELAY_CFG** | **R/W** | **中继配置（新增）** |

### 不影响现有功能

- 旧版固件升级后：`isRelay=0`, `hopCount=0`（默认值），行为完全等价于升级前
- 现有所有 PID（PID_SWITCH ~ PID_ALL_STATE）保持不变
- 升级后小程序如果没用到 PID_RELAY_CFG，照常运行

---

## 2. 数据结构

### 节点连接类型（统一字段）

上位机只需要操作一个字段 `linkRole`（uint8）即可定义节点角色：

| 节点类型 | linkRole 值 | 枚举名 | parentRelayId | 说明 |
|---------|------------|--------|---------------|------|
| **直连节点** | 0 | LINK_DIRECT | 0x0000 | 与网关直接通信 |
| **中继节点** | 1 | LINK_RELAY | 0x0000 | 管理自己的空调 + 转发子节点 |
| **中继子节点** | 2 | LINK_CHILD | 非 0（如 0xBB02） | 通过中继与网关通信 |

### 字段含义

```
linkRole (uint8, R/W):
  0x00 = LINK_DIRECT  直连节点
  0x01 = LINK_RELAY   中继节点
  0x02 = LINK_CHILD   中继子节点

parentRelayId (uint16, LE, R/W):
  0x0000     = 直连网关 (仅 LINK_DIRECT 和 LINK_RELAY 用)
  0xBB02    = 上一级中继节点ID (仅 LINK_CHILD 用)
```

**注意**：`linkRole` 和 `parentRelayId` **必须保持一致**：
- `linkRole = LINK_CHILD` 时，`parentRelayId != 0`
- `linkRole != LINK_CHILD` 时，`parentRelayId == 0`

如果设置不一致，固件按下表自动修正：

| 上位机写入 | linkRole | parentRelayId | 实际生效 |
|----------|----------|---------------|---------|
| role=0, parent=0 | 0 (LINK_DIRECT) | 0 | 直连节点 |
| role=1, parent=0 | 1 (LINK_RELAY) | 0 | 中继节点 |
| role=0, parent=非0 | 2 (LINK_CHILD) | parent | 子节点 |
| role=1, parent=非0 | 1 (LINK_RELAY) | 0 | 强制修正为中继 |
| role=2, parent=0 | 0 (LINK_DIRECT) | 0 | 强制修正为直连 |

### 中继下子节点状态（中继节点只读）

```
childCount (uint8):
  当前已注册子节点数量 (中继下)
  
childBitmap (uint16, LE):
  每个 bit 对应一个子节点槽位
  bit=1 在线, bit=0 离线/超时
  最多 8 个子节点 (高 8 位保留)
```

---

## 3. BLE 协议格式

### 帧结构（保持不变）

```
[LEN(1)][CMD(1)][PAYLOAD(N)][CRC(1)]
CRC = (sum(前 N+1 字节) + 0xEC) & 0xFF
```

### CMD 类型（保持不变）

| CMD | 方向 | 含义 |
|-----|------|------|
| 0x01 WRITE | 上位机 → 设备 | 写属性 |
| 0x02 READ | 上位机 → 设备 | 读属性 |
| 0x03 NOTIFY | 设备 → 上位机 | 属性通知/响应 |
| 0x04 ACK | 设备 → 上位机 | 操作成功 |
| 0x05 ERROR | 设备 → 上位机 | 错误响应 |
| 0x06 ACTION | 上位机 → 设备 | 执行动作 |

### WRITE 格式（上位机写配置到设备）

```
帧: [LEN][0x01][0x18][role][parentRelayId_LE_LO][parentRelayId_LE_HI][CRC]

字节:
  LEN      = 6 (1 + 1 + 1 + 1 + 2 + 1)
  CMD      = 0x01
  PAYLOAD[0] = 0x18 (PID_RELAY_CFG)
  PAYLOAD[1] = linkRole (0/1/2)
  PAYLOAD[2] = parentRelayId 低字节
  PAYLOAD[3] = parentRelayId 高字节
  CRC      = 校验字节

例: 设置为中继节点 (linkRole=1, parentRelayId=0)
  06 01 18 01 00 00 XX
  
例: 设置为中继子节点 (linkRole=2, parentRelayId=0xBB02)
  06 01 18 02 02 BB XX
  
例: 设置为直连节点 (linkRole=0, parentRelayId=0)
  06 01 18 00 00 00 XX

兼容旧版: 上位机写入 role=0 + parent=非0 时, 
         固件自动设为 linkRole=2 (LINK_CHILD)
```

**响应**:
- 成功：`[LEN][0x04][CRC]` (ACK)
- 失败：`[LEN][0x05][ERR_STR][CRC]`

### READ 格式（上位机读取当前中继配置）

```
帧: [LEN][0x02][0x18][CRC]

字节:
  LEN      = 4
  CMD      = 0x02
  PAYLOAD[0] = 0x18 (PID_RELAY_CFG)
  CRC

例: 读中继配置
  04 02 18 XX
```

**响应（NOTIFY）**:

```
帧: [LEN][0x03][PAYLOAD][CRC]

PAYLOAD:
  [0x18]                              // PID
  [linkRole]                          // 1 字节 (0/1/2)
  [hopCount]                          // 1 字节 (0=直连, 1=中继下, 与 linkRole 一致)
  [parentRelayId_LE_LO]               // 1 字节
  [parentRelayId_LE_HI]               // 1 字节
  [childCount]                        // 1 字节 (仅 linkRole=1 时有意义)
  [childBitmap_LE_LO]                 // 1 字节
  [childBitmap_LE_HI]                 // 1 字节

LEN = 1 + 1 + 8 = 10 字节 (含 PID 自身)

例: 直连节点返回
  0A 03 18 00 00 00 00 00 00 00 XX
  
例: 中继节点返回 (3 个子节点在线: 0x07)
  0A 03 18 01 00 00 00 03 07 00 XX
  
例: 中继子节点返回 (挂在 0xBB02 下)
  0A 03 18 02 01 02 BB 00 00 00 XX
```

---

## 4. 微信小程序 UI 建议

### 4.1 新增页面: 中继配置

入口：设备详情页 → "中继设置"

#### 页面布局

```
┌────────────────────────────────────┐
│  节点中继配置                      │
│                                    │
│  当前角色: [普通节点 ▼]            │
│                                    │
│  ┌─────────────────────────────┐  │
│  │  ○ 普通节点                  │  │
│  │  ○ 中继节点                  │  │
│  │  ○ 中继子节点 (挂在 R1 下)  │  │
│  └─────────────────────────────┘  │
│                                    │
│  上级中继节点 ID: [____]            │
│  (中继子节点时填写, 其他填 0)      │
│                                    │
│  [    读取当前配置    ]            │
│  [    保存配置        ]            │
└────────────────────────────────────┘
```

#### 交互逻辑

```js
// 读取当前配置
function readRelayConfig() {
  sendCmd('02 18');  // READ PID_RELAY_CFG
  // 等待 NOTIFY 响应, 解析 isRelay/hopCount/parentRelayId
}

// 保存配置
function saveRelayConfig() {
  let isRelay = 0;  // 0=普通, 1=中继
  let parentId = 0x0000;
  
  if (role === 'relay') {
    isRelay = 1;
    parentId = 0x0000;
  } else if (role === 'child') {
    isRelay = 0;
    parentId = parseInt(inputParentId);  // 用户输入
  }
  
  // 构造 BLE 帧: WRITE PID_RELAY_CFG
  sendCmd(`01 18 ${isRelay} ${parentId&0xFF} ${(parentId>>8)&0xFF}`);
  // 等待 ACK
  // 成功后提示: "配置已保存, 节点正在重新注册..."
}
```

#### 中继下子节点列表（中继节点专属页面）

```
┌────────────────────────────────────┐
│  中继节点: 0xBB02                   │
│  当前在线子节点: 3 / 8             │
│                                    │
│  子节点列表:                       │
│  ┌────────┬────────┬────────────┐ │
│  │ nodeId │ 状态   │ 上次心跳   │ │
│  ├────────┼────────┼────────────┤ │
│  │ 0xBB03 │ ●在线  │ 2s 前      │ │
│  │ 0xBB05 │ ●在线  │ 5s 前      │ │
│  │ 0xBB07 │ ●在线  │ 8s 前      │ │
│  │ 0xBB09 │ ○离线  │ 3min 前    │ │
│  └────────┴────────┴────────────┘ │
│                                    │
│  [    刷新    ]                    │
└────────────────────────────────────┘
```

### 4.2 部署流程

```
1. 安装工人在现场选定一个板子作为中继位置
2. 微信小程序连接该板子, 进入"中继配置"页面
3. 选择"中继节点", 保存
4. 设备自动重新注册, 注册成功
5. 工人连接中继覆盖范围内的其他板子
6. 对每个子节点: 选择"中继子节点", 输入上级中继 nodeId (0xBB02), 保存
7. 子节点自动通过中继注册, 注册成功

现场常见配置示例:
- 中继 ID: 0xBB02 (工地上统一的"主中继")
- 子节点: 挂在 0xBB02 下, isRelay=0, parentRelayId=0xBB02
```

### 4.3 故障排查

通过 `PID_RELAY_CFG` 读取可以诊断:

| 现象 | 解读 |
|------|------|
| `isRelay=0, hopCount=0, parentRelayId=0` | 直连节点, 正常工作 |
| `isRelay=1, childCount=0` | 中继节点, 但没有子节点注册上来 |
| `isRelay=1, childCount=5, childBitmap=0x0001` | 中继下只有 1 个子节点在线 |
| `isRelay=0, hopCount=1, parentRelayId=0xBB02` | 中继子节点, 正确状态 |
| `isRelay=0, hopCount=1, 但长时间无数据上报` | 中继子节点, 中继可能离线, 等待恢复 |

---

## 5. JavaScript 参考实现

### CRC 计算

```js
function calcCrc(buf) {
  let sum = 0;
  for (let i = 0; i < buf.length; i++) {
    sum += buf[i];
  }
  return (sum + 0xEC) & 0xFF;
}

// 构建 BLE 帧
function buildFrame(cmd, payload) {
  const len = 2 + payload.length + 1;  // CMD + PAYLOAD + CRC
  const buf = [len, cmd, ...payload];
  const crc = calcCrc(buf);
  buf.push(crc);
  return buf;
}
```

### 中继配置相关

```js
// === 中继配置 PID ===
const PID_RELAY_CFG = 0x18;

// 读取中继配置
function readRelayConfig() {
  const payload = [PID_RELAY_CFG];
  const frame = buildFrame(0x02, payload);  // READ
  return sendBleFrame(frame).then(notify => {
    // notify.payload = [0x18, isRelay, hopCount, parentLo, parentHi, childCount, bitmapLo, bitmapHi]
    return {
      pid:         notify.payload[0],
      isRelay:     notify.payload[1],
      hopCount:    notify.payload[2],
      parentRelayId: notify.payload[3] | (notify.payload[4] << 8),
      childCount:  notify.payload[5],
      childBitmap: notify.payload[6] | (notify.payload[7] << 8),
    };
  });
}

// 写入中继配置
function writeRelayConfig(isRelay, parentRelayId) {
  const payload = [
    PID_RELAY_CFG,
    isRelay & 0xFF,
    parentRelayId & 0xFF,
    (parentRelayId >> 8) & 0xFF
  ];
  const frame = buildFrame(0x01, payload);  // WRITE
  return sendBleFrame(frame);
}

// 解析中继角色描述
function describeRelayRole(cfg) {
  if (cfg.isRelay === 1) {
    return `中继节点 (下挂 ${cfg.childCount} 个子节点)`;
  }
  if (cfg.hopCount === 1) {
    return `中继子节点 (挂在 0x${cfg.parentRelayId.toString(16).toUpperCase().padStart(4, '0')} 下)`;
  }
  return '直连节点';
}
```

### 子节点列表展示

```js
// 根据 childBitmap 解析每个槽位状态
function parseChildList(childCount, childBitmap) {
  const list = [];
  for (let i = 0; i < Math.min(childCount, 8); i++) {
    const online = (childBitmap >> i) & 1;
    list.push({
      slot: i,
      // nodeId 需要从其他渠道获取 (如服务器同步)
      online: online === 1,
    });
  }
  return list;
}

// 渲染子节点状态
function renderChildStatus(cfg) {
  if (cfg.isRelay !== 1) return null;
  const list = parseChildList(cfg.childCount, cfg.childBitmap);
  return {
    total: cfg.childCount,
    online: list.filter(c => c.online).length,
    offline: list.filter(c => !c.online).length,
  };
}
```

---

## 6. 完整 PID 速查表 (新增后)

| PID | 名称 | 读/写 | 说明 | 长度 |
|-----|------|-------|------|------|
| 0x01 | PID_SWITCH | R/W | 开关 | 1B |
| 0x02 | PID_MODE | R/W | 模式 | 1B |
| 0x03 | PID_TEMP_SET | R/W | 设定温度 | 2B |
| 0x04 | PID_TEMP_ROOM | R | 环境温度 | 2B |
| 0x05 | PID_FAN_SPEED | R/W | 风速 | 1B |
| 0x06 | PID_LOCK | R/W | 锁定 | 1B |
| 0x07 | PID_ERROR | R | 故障码 | 2B |
| 0x10 | PID_LORA_CFG | R/W | LoRa 配置 (nodeId + channel) | 3B |
| 0x11 | PID_IR_CFG | R/W | 红外配置 | 4B |
| 0x12 | PID_DEV_INFO | R | 设备信息 | 4B |
| 0x13 | PID_SYS_PARAMS | R | 系统参数 | 14B |
| 0x14 | PID_SYS_CTRL | R/W | 系统控制 | 4B |
| 0x15 | PID_IR_MATCH | R | 红外匹配结果 | 4B |
| 0x16 | PID_IR_LEARN | R/W | 红外学习控制 | 1B |
| 0x17 | PID_IR_LEARN_LIST | R | 学习通道列表 | 10B |
| **0x18** | **PID_RELAY_CFG** | **R/W** | **中继配置 (新增)** | **4B/8B** |
| 0xF0 | PID_ALL_STATE | R | 全状态聚合 | 11B |

---

## 7. 测试用例

### 7.1 直连节点

```
1. 读 PID_RELAY_CFG
   预期响应: isRelay=0, hopCount=0, parentRelayId=0
```

### 7.2 设置为中继节点

```
1. 写 PID_RELAY_CFG: isRelay=1, parentRelayId=0
   预期: ACK 响应
2. 等待 30s (设备重新注册)
3. 读 PID_RELAY_CFG
   预期: isRelay=1, hopCount=0, parentRelayId=0, childCount=0
4. 中继板 LoraStatus 应能正常工作
```

### 7.3 设置为中继子节点

```
前提: 已有一个中继节点 ID = 0xBB02
1. 写 PID_RELAY_CFG: isRelay=0, parentRelayId=0xBB02
   预期: ACK 响应
2. 等待设备注册 (在中继覆盖范围内, 应 < 1 分钟)
3. 读 PID_RELAY_CFG
   预期: isRelay=0, hopCount=1, parentRelayId=0xBB02
```

### 7.4 验证中继子节点在线

```
1. 中继节点 + 多个中继子节点全部注册成功
2. 读中继节点的 PID_RELAY_CFG
   预期: childCount > 0, childBitmap 标记在线子节点
3. 中继子节点关闭电源
4. 等待 3 个 scanCycle (约 270s)
5. 再读中继节点的 PID_RELAY_CFG
   预期: 对应 bit 位从 1 变 0
```

---

## 8. 兼容性说明

### 升级路径

- **小程序升级**: 可在下次发布时集成 PID_RELAY_CFG 支持, 不需立即上线
- **设备升级**: 通过 OTA 推送新固件 (见 OTAprofile)
- **混合部署**: 旧固件 + 新版小程序 → 小程序忽略未识别的 PID; 新固件 + 旧版小程序 → 中继字段为默认值, 不影响

### 错误处理

```js
// WRITE 失败的可能原因
- 节点正在切换 Lora 状态 (ble 写入但状态机忙)
  → 重试即可
- 数据长度不对
  → 检查 payload 长度

// READ 失败的可能原因
- 节点未连接
  → 检查 BLE 连接
- 节点正在切换频率
  → 重试
```

---

## 9. 升级检查清单

微信小程序端:

- [ ] 添加 PID_RELAY_CFG = 0x18 常量
- [ ] 实现 `readRelayConfig()` 函数
- [ ] 实现 `writeRelayConfig(isRelay, parentId)` 函数
- [ ] 新增"中继配置"页面 UI
- [ ] 在设备详情页添加入口
- [ ] 添加角色描述显示
- [ ] 添加中继节点子节点列表显示
- [ ] 添加部署流程引导
- [ ] 错误处理和重试逻辑

固件端 (已完成):

- [x] `board.h` t_dev 新增 isRelay/hopCount/parentRelayId
- [x] `peripheral.h` PID_RELAY_CFG = 0x18
- [x] `util.c` Lora_Pro 中继转发状态机
- [x] `peripheral.c` PID_RELAY_CFG 读写处理
- [x] 三种节点 (直连/中继/中继子) 全部稳定运行