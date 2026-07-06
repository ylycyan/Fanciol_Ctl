# LoRa 多跳中继方案 — 透传轮询设计

> 适用规模：≤60 个总节点 | 版本：v4.0 | 日期：2026-06-26

---

## 1. 方案概述

### 当前问题

```
[板子A] ──LoRa──→ [网关] ──→ 服务器
[板子B] ──LoRa──↗
[板子C] ──LoRa──↗
       ↑
   单跳覆盖（视距 1~3km）
```

所有板子必须在网关覆盖范围内，覆盖面积小，大规模部署需要大量网关。

### 解决思路

指定部分板子为**中继节点**，透明转发其他板子的数据和指令，扩展覆盖范围：

```
[板子A] ──LoRa──→ [中继R1] ──LoRa──→ [网关] ──→ 服务器
[板子B] ──LoRa──↗
```

### 设计约束

| 项目 | 要求 |
|------|------|
| 网关 | **零改动**，网关不知道中继的存在 |
| 硬件 | 不改，每块板子已具备 LoRa + BLE |
| 配置 | 全部通过 BLE（微信小程序）完成 |
| 中继角色 | 中继是透明的无线转发器，不解析应用层 |
| 可靠性 | 控制操作几乎不允许失败 |
| 故障策略 | 中继故障时子节点等待恢复，不降级直连 |

---

## 2. 网关轮询模型

网关是**单线程串行轮询**，一个周期内依次访问每个节点，过程中内联控制指令：

```
for each node in nodeList:
    发送请求cmd → 等待该节点响应 → 处理数据
    if 该节点有pending控制指令:
        发送控制指令 → 等待执行结果
    → 下一个节点
```

网关硬件特征：
- **LoRa #1**：始终监听注册频率（SF9/125kHz），随时可接收注册包
- **LoRa #2**：始终在工作频率（SF10/250kHz）轮询已注册节点

中继板只需在自己的轮询槽位内完成转发，不会和其他节点冲突。
半双工冲突在网关串行调度下天然不存在。

---

## 3. 已识别问题及对策

| # | 问题 | 严重度 | 对策 |
|---|------|--------|------|
| 1 | **半双工冲突** | 致命 | 网关串行轮询，中继只在自己槽位内转发 |
| 2 | **注册/工作 SF/BW 不匹配** | 致命 | 中继做频率转换：工作频率收→注册频率发→注册频率收回复→工作频率转发 |
| 3 | **控制指令无重试** | 严重 | 新增 ACK 包（cmd=0x0E），服务器侧统计并重试 |
| 4 | **中继单点故障** | 严重 | 子节点不降级，等待中继恢复；服务器告警人工介入 |
| 5 | **多跳时序挤压** | 中等 | hopCount>0 的子节点 scanCycle 强制 ≥90s |
| 6 | **弱 CRC 多跳累积** | 中等 | 暂不改，LoRa 自带 FEC，实测后决定 |

---

## 4. 网络拓扑

```
           ┌──[板子A]     ┌──[板子D]
[网关] ←──[中继R1]──┤         [中继R2]──┤
    ↑       ↑     └──[板子B]     ↑     └──[板子E]
    │       │     ┌──[板子C]     │
    │       └─────┘              │
    │                      └─────┘
    │
    └──[板子F]  (直连网关的普通节点)
```

| 角色 | 说明 | 配置方式 |
|------|------|---------|
| **中继节点** | 既管理自己的空调，又转发子节点的数据/指令 | BLE 写入 isRelay=1 |
| **子节点** | 通过中继转发与网关通信 | BLE 写入 parentRelayId=中继nodeId |
| **直连节点** | 直接与网关通信（现有行为不变） | 默认，不需配置 |

最大跳数：2 跳（网关→中继→子节点），不建议超过 2 跳。

---

## 5. 协议设计

### 5.1 注册包（节点侧扩展 +2 字节）

```
原: [cmd=0x05(1)][Tag(1)][nodeId(2)][CRC(1)]                         = 5 字节
新: [cmd=0x05(1)][Tag(1)][nodeId(2)][hopCount(1)][isRelay(1)][CRC(1)] = 7 字节

hopCount: BLE 配置写入。0=直连, 1=一跳中继下
isRelay:  BLE 配置写入。0=普通节点, 1=中继节点

网关处理: 读取前 5 字节，忽略 hopCount/isRelay。
节点 CRC: 覆盖全部 7 字节。
```

### 5.2 注册回复（网关不改动）

```
网关回复保持 19 字节不变:
  [cmd=0x07(1)][Tag(1)][Timestamp(4)][gatewayId(2)][nodeId(2)]
  [LoraPower(1)][nodeIdx(2)][DataCycle(2)][scanCycle(2)]
  [DevType(1)][DevTag(1)][CRC(1)]

节点侧额外处理:
  hopCount > 0 且 scanCycle < 90 → 强制 scanCycle = 90s
```

### 5.3 数据上报包（+1 字节）

```
原: [cmd=0x01(1)][Tag(1)][nodeId(2)][RSSI(1)][errorInfo(1)]...[CRC(1)] = 18 字节
新: 同上 CRC 前追加 [hopCount(1)] = 19 字节

网关: 正常解析前 18 字节，hopCount 追加在末尾不影响现有逻辑。
```

### 5.4 控制指令 ACK（节点侧新增）

```
ACK包: [cmd=0x0E(1)][Tag(1)][nodeId(2)][execResult(1)][CRC(1)] = 6 字节

execResult: 0x00=成功, 0x01=IR失败, 0x02=参数错误, 0x03=设备锁定

网关: 按正常数据处理，不解析 ACK 含义。
服务器: 解析 ACK 包统计控制成功率，未收到则通过应用层重新下发。
```

### 5.5 网关控制指令（不改动）

```
格式保持不变（17 字节）:
  [cmd=0x0D(1)][Tag(1)][nodeId(2)][Operate(1)][OTag(2)][OParam(4)][Token(4)][CRC(1)]
```

---

## 6. 状态机设计

### 6.1 普通节点 / 子节点

```
Status 0 (Uninit)
  │  LoadDevInfo()
  ▼
Status 1 (Logining)
  │  hopCount=0: 注册频率发注册包 (5~7字节, SF9/125k)
  │  hopCount=1: 工作频率发注册包 (7字节, SF10/250k)
  │  间隔: 每 (10 + nodeId%3)s 发一次
  ▼
Status 2 (CheckSend)
  │  等待 TX_DONE
  │  超时 1s → 回 Status 1
  ▼
Status 3 (RecvLogin)
  │  hopCount=0: 在注册频率 Listening，超时 2s
  │  hopCount=1: 在工作频率 Listening，超时 5s（中继转发需要时间）
  │  收到 cmdConfig + nodeId 匹配 → 进入 Status 4
  │  超时 → 回 Status 1 重试
  ▼
Status 4 (Connected)
  │  hopCount>0 且 scanCycle<90 → 强制 scanCycle=90
  │  工作频率 Listening (SF10/250k)
  │
  │  ├─ cmd=0x0D Tag=0 → 上报数据 (Status 5)
  │  ├─ cmd=0x0D Tag=1 → 执行控制，回复 ACK(0x0E)
  │  ├─ cmd=0x0B       → 更新 RTC
  │  └─ 不降级：hopCount>0 时，即使长时间无指令也保持等待
  ▼
Status 5 (CheckData)
  │  发送上报数据 (19字节，含 hopCount)
  │  TX_DONE/超时 → 回 Status 4
```

**与现有状态机完全兼容**：hopCount=0 时行为与当前固件完全一致。

### 6.2 中继节点

Status 0~3 与普通节点完全相同。差异仅在 Status 4：

```
Status 4 (Connected + Relay) ── 工作频率 Listening
  │
  ├─ 收到 cmd=0x05（子节点注册包，工作频率）
  │    │
  │    │  [频率转换转发 — 一次性事件，<1秒]
  │    │
  │    ├─ 1. 记录 pendingChildId
  │    ├─ 2. Lora_Init(注册频率, SF9, BW125k)  // 切到注册频率
  │    ├─ 3. Lora_Tx(注册包)                    // 在注册频率转发
  │    ├─ 4. Listening 等待网关回复（最多 5s）
  │    ├─ 5a. 收到 cmdConfig (nodeId = pendingChildId)
  │    │      → Lora_Init(工作频率, SF10, BW250k)  // 切回工作频率
  │    │      → Lora_Tx(cmdConfig)                   // 转发给子节点
  │    │      → 回到正常 Listening
  │    │
  │    └─ 5b. 5s 超时
  │           → Lora_Init(工作频率, SF10, BW250k)  // 切回工作频率
  │           → 回到正常 Listening（子节点会重发注册包）
  │
  ├─ 收到 cmd=0x0D（网关指令，工作频率，目标 ≠ 自己）
  │    │
  │    │  [同步阻塞转发 — 每次轮询都可能触发]
  │    │
  │    ├─ 1. Lora_Tx(原包)                      // 转发给子节点
  │    ├─ 2. Listening 等待子节点响应（最多 3s）
  │    ├─ 3. 收到子节点数据 → Lora_Tx(转发给网关)
  │    │       → 等待 TX_DONE → 回到 Listening
  │    └─ 4. 超时 → 向网关回 errorInfo=子节点离线
  │
  ├─ 收到 cmd=0x0D Tag=1（控制指令，目标 ≠ 自己）
  │    ├─ 1. 转发给子节点
  │    ├─ 2. 等待 ACK(cmd=0x0E)（最多 2s）
  │    └─ 3. 转发 ACK 给网关；超时回 errorInfo
  │
  ├─ 收到 cmd=0x0D（目标 = 自己）
  │    → 正常上报自己的数据（现有逻辑不变）
  │
  ├─ 收到 cmd=0x0B（群控对时）
  │    → 自己处理 + 转发给子节点
  │
  ├─ 收到子节点 cmd=0x0E（ACK，工作频率）
  │    → 转发给网关（中继不解析内容，纯转发）
  │
  └─ 转发期间收到非目标包 → 静默丢弃
```

**关键特性：同步阻塞式转发**——收到网关指令后，中继处理完（转发→等响应→转发回）才重新 Listening。匹配网关串行轮询模型，无半双工冲突。

### 6.3 中继频率切换说明

中继仅在收到子节点注册包时做一次频率切换，这是**唯一涉及频率切换的地方**：

```
正常工作: 始终在工作频率 (SF10/250k)

收到 cmd=0x05 时:
  工作频率 ──→ 注册频率 (SF9/125k) ──→ 工作频率
       收注册包    转发+等回复              回到正常

切换耗时: < 100ms (Lora_Init 硬件复位 + 参数配置)
持续时间: 最多 5s (等待网关回复)
发生频率: 每个子节点仅一次 (注册时)
```

切换期间（最多 5s），中继无法接收网关的轮询指令。
轮询周期 60~90s，丢失一次轮询无影响。

---

## 7. 时序详解

### 7.1 子节点注册时序（通过中继）

```
时间(ms)  子节点A(工作频率)    中继R1                  网关LoRa1(注册频率)
──────────────────────────────────────────────────────────────────────────
0         发注册包(7字节)
          cmd=0x05
          hopCount=1

50        Waiting...           收到cmd=0x05
                              识别→切注册频率
                              转发注册包 ────────────→ 收到cmd=0x05
                                                       nodeId=A
                                                       加入轮询列表
                                                       
800       Waiting...           Waiting...               回复cmdConfig(19B)
                              ←────────────────────────

850       Waiting...           收到cmdConfig
                              nodeId=A ✓
                              切回工作频率

900       收到cmdConfig ✓      转发cmdConfig
        注册完成
```

### 7.2 网关轮询中继子节点（正常工作）

```
时间(ms)  网关                     中继R1                  子节点A
──────────────────────────────────────────────────────────────────────
0         ──cmd→A(Tag=0请求上报)──  收到,目标=A,转发→A      Listening
200       等待响应                 等待A响应               收到cmd,处理
400       等待响应                 收到A数据,转发→网关      发送数据
600       收到A数据,处理完毕        Listening               Listening
650       ──cmd→A(Tag=1控制指令)──  收到,转发→A             Listening
                                   (若有pending控制)
850       等待ACK                  等待A的ACK             执行IR,发送ACK
1050      等待ACK                  收到ACK,转发→网关       发送ACK
1250      收到ACK,确认成功          Listening              Listening

→ 下一个节点的轮询槽位
```

中继在 200~600ms 期间 Listening 收子节点数据，网关此时不向其他节点发任何东西。
**半双工冲突不存在。**

### 7.3 时序预算

```
单节点轮询耗时:
  直连节点:                ~400ms (仅上报)
  直连节点+内联控制:       ~650ms (上报+控制)
  1跳中继子节点:           ~600ms (仅上报)
  1跳中继子节点+内联控制:  ~1250ms (上报+控制)
```

**容量计算**（scanCycle=90s，30% 节点有控制操作）：

| 节点总数 | 直连 | 中继子节点 | 上报耗时 | 控制增量 | 合计 | 余量 |
|---------|------|-----------|---------|---------|------|------|
| 20 | 10 | 10 | 12s | 4s | **16s** | 74s ✅ |
| 40 | 10 | 30 | 28s | 11s | **39s** | 51s ✅ |
| 60 | 10 | 50 | 44s | 17s | **61s** | 29s ⚠️ |

---

## 8. 频率规划

```
注册频率（不变）:
  频率 = channel × 0.3 + 420.05 MHz
  参数 = SF9 + BW125kHz
  用途: 子节点(hopCount=0)注册, 中继转发注册包, 网关LoRa1监听

工作频率（不变）:
  频率 = channel × 0.3 + 420.05 + 3.1375 MHz (channel≤22)
  参数 = SF10 + BW250kHz
  用途: 数据上报/控制指令/网关LoRa2轮询

中继转发注册时的频率使用:
  收包: 工作频率 (SF10/250k) ← 子节点在此频率发注册包
  发包: 注册频率 (SF9/125k)  ← 网关LoRa1在此频率监听
  收回复: 注册频率 (SF9/125k) ← 网关在此频率回复
  发给子节点: 工作频率 (SF10/250k) ← 子节点在此频率等待
```

不新增频率，不改变现有频率规划。

---

## 9. 故障处理

### 9.1 中继故障

```
中继掉电/异常:
  子节点在工作频率收不到任何指令
  子节点行为: 保持 Status 4 Listening，等待中继恢复
  不降级，不切换频率，不做任何自动恢复

服务器侧:
  连续 N 个周期子节点未上报 → 触发告警
  中继自身也未上报 → 触发中继离线告警
  人工介入修复中继，恢复后子节点自动恢复通信
```

### 9.2 中继板子节点状态上报

中继在上报自己的数据时，额外携带子节点在线状态：

```
中继上报扩展（CRC前追加 3 字节）:
  原字段... + [childCount(1)][childBitmap(2)][hopCount(1)][CRC]

  childCount:  当前在线子节点数
  childBitmap: 每个 bit 对应一个子节点槽位（1=在线, 0=超时）
```

网关正常解析前 18 字节，服务器可解析额外字段监控中继健康。

---

## 10. BLE 配置接口

### 新增 PID

```
PID_RELAY_CFG = 0x18

写入 (App → 设备):
  [PID_RELAY_CFG(1)][isRelay(1)][parentRelayId(2)]
    isRelay:        0=普通节点, 1=中继节点
    parentRelayId:  上级中继节点ID (0x0000=直连网关)
    hopCount:       自动计算 (parentRelayId=0→0, 否则→1)

写入后自动触发重新注册:
  Dev.loraStatus = 1; Timer_Lora = 30000;

读取 (设备 → App):
  [PID_RELAY_CFG(1)][isRelay(1)][hopCount(1)][parentRelayId(2)][childCount(1)][childBitmap(2)]
```

### 部署操作流程

```
安装:
  1. 选定中继位置，小程序将该节点设为 isRelay=1
  2. 对中继覆盖范围内的子节点，写入 parentRelayId=中继nodeId
  3. 子节点 hopCount 自动设为 1，触发重新注册
  4. 子节点在工作频率发注册包，中继转发至注册频率，网关收到并回复
  5. 注册完成，开始正常轮询

维护:
  - 中继故障: 服务器告警，人工修复中继，子节点自动恢复
  - 变更中继: 旧中继设 isRelay=0，新中继设 isRelay=1，子节点更新 parentRelayId
  - 移除中继: 中继设 isRelay=0，子节点 parentRelayId 设为 0，恢复直连
```

---

## 11. 代码改动清单

### 11.1 板子固件（唯一改动部分）

#### `board.h` — t_dev 新增 4 字节

```c
uint8_t  isRelay;          // 0=普通节点, 1=中继节点
uint8_t  hopCount;         // 0=直连, 1=一跳中继下
uint16_t parentRelayId;    // 上级中继节点ID (0=直连)
```

#### `util.c` — Lora_Pro() 改动

```
Status 1 (Logining):
  hopCount=1 → 用工作频率发注册包 (7字节)
  hopCount=0 → 用注册频率发注册包 (5~7字节, 现有逻辑)

Status 3 (RecvLogin):
  hopCount>0 → 工作频率 Listening，超时 5s
  hopCount=0 → 注册频率 Listening，超时 2s (不变)
  收到回复后: hopCount>0 且 scanCycle<90 → 强制 scanCycle=90

Status 4 (Connected):
  if (Dev.isRelay) {
      新增中继转发逻辑 (~60行):
        - cmd=0x05: 频率转换转发 (工作→注册→工作)
        - cmd=0x0D: 同步阻塞转发
        - cmd=0x0B: 群控对时同步
        - cmd=0x0E: ACK 转发
  }
  hopCount>0: 不做降级处理，保持等待

Status 5 (CheckData):
  上报包 18→19字节 (+hopCount)
  中继节点额外追加 childCount + childBitmap (+3字节)
```

#### `peripheral.c` — BLE 处理新增

```
BT_CMD_WRITE:
  case PID_RELAY_CFG:  // ~15行
    解析 isRelay + parentRelayId
    自动计算 hopCount
    保存到 Dev
    触发重新注册

BT_CMD_READ:
  case PID_RELAY_CFG:  // ~10行
    返回 isRelay/hopCount/parentRelayId/childCount/childBitmap
```

### 11.2 零改动模块

| 模块 | 改动量 | 说明 |
|------|--------|------|
| **网关固件** | **0** | 不知道中继存在 |
| `lora.c` | **0** | 底层驱动不变 |
| `flash.c` | **0** | t_dev 自动持久化 |
| `timer.c` | **0** | 定时逻辑不变 |
| `rule.c` | **0** | 规则引擎不变 |
| `adc.c` | **0** | 传感器不变 |
| `ir.c` | **0** | 红外模块不变 |
| `led.c` | **0** | LED 不变 |
| `Profile/` | **0** | GATT 服务不变 |

---

## 12. 兼容性

| 场景 | 说明 |
|------|------|
| 新固件 + 现有网关 | **完全兼容**，网关零改动 |
| 旧固件 + 现有网关 | 不受影响，hopCount/isRelay 默认 0 |
| isRelay=0 的节点 | 注册包多 2 字节，网关忽略，其余行为完全不变 |
| 新旧固件混合部署 | 可共存，旧节点直连，新节点可配中继 |

---

## 13. 风险与应对

| 风险 | 概率 | 影响 | 应对 |
|------|------|------|------|
| 中继板掉电 | 中 | 子节点离线 | 服务器告警，人工修复，子节点自动恢复 |
| 中继转发注册时被网关轮询 | 极低 | 中继本轮数据少报一次 | 切换 <5s，轮询周期 60~90s，碰撞概率 <1% |
| 中继下子节点过多 | 中 | 尾部子节点超时 | 限制 ≤8 子节点/中继 |
| LoRa 同频干扰 | 低 | 个别包丢失 | ACK 包使服务器可感知，应用层重试 |
| BLE 配置错误 | 低 | 子节点注册失败 | 5s 超时后自动重试，可通过 BLE 修正 |

---

## 14. 实施计划

### 阶段 1：协议与数据结构（1 天）

- [ ] `board.h` t_dev 新增 4 字段
- [ ] 注册包/上报包扩展
- [ ] ACK 包格式（cmd=0x0E）
- [ ] BLE PID_RELAY_CFG 读写
- [ ] 验证 hopCount=0 时行为完全不变

### 阶段 2：中继转发核心（2~3 天）

- [ ] 中继 Status 4 转发逻辑（cmd=0x0D 数据/控制）
- [ ] 中继频率转换转发（cmd=0x05 注册）
- [ ] ACK 转发（cmd=0x0E）
- [ ] 子节点注册频率选择（hopCount 决定）

### 阶段 3：完善与监控（1 天）

- [ ] 中继子节点状态上报（childCount + childBitmap）
- [ ] 子节点 scanCycle 强制下限
- [ ] 服务器侧中继健康监控

### 阶段 4：联调测试（3~5 天）

- [ ] 2 块板子 + 1 网关：单跳中继基本通信
- [ ] 5 块板子（1 中继 + 4 子节点）+ 1 网关：多节点轮询
- [ ] 中继掉电→恢复测试
- [ ] 控制指令压力测试：连续 100 次，统计成功率
- [ ] BLE 配置切换测试
- [ ] 全负荷测试（60 节点场景）

### 总工期：1~2 周

---

## 15. 附录

### A. 数据包格式速查

| 包类型 | 方向 | 频率 | 格式 | 字节数 | 网关感知 |
|--------|------|------|------|--------|---------|
| 注册请求 | 节点→网关 | 工作/注册 | [0x05][Tag][nodeId][hopCount][isRelay][CRC] | 7 | 只读前5字节 |
| 注册回复 | 网关→节点 | 注册 | [0x07][Tag][TS(4)][GWId][NId][Pwr][Idx][DC][SC][DT][DTg][CRC] | 19 | **不变** |
| 数据上报 | 节点→网关 | 工作 | [0x01][Tag][nodeId][RSSI][Err][data...][hopCount][CRC] | 19 | 前18字节不变 |
| 控制指令 | 网关→节点 | 工作 | [0x0D][Tag][nodeId][Operate][OTag][OParam(4)][Token(4)][CRC] | 17 | **不变** |
| 执行ACK | 节点→网关 | 工作 | [0x0E][Tag][nodeId][execResult][CRC] | 6 | 按数据处理 |
| 群控对时 | 网关→广播 | 工作 | [0x0B][Tag][GWId][Timestamp(4)][CRC] | 9 | **不变** |

### B. BLE PID 速查

| PID | 名称 | 读/写 | 说明 |
|-----|------|-------|------|
| 0x01 | PID_SWITCH | R/W | 开关 |
| 0x02 | PID_MODE | R/W | 运行模式 |
| 0x03 | PID_TEMP_SET | R/W | 设定温度 |
| 0x04 | PID_TEMP_ROOM | R | 环境温度 |
| 0x05 | PID_FAN_SPEED | R/W | 风速 |
| 0x10 | PID_LORA_CFG | R/W | LoRa 配置（nodeId+channel） |
| 0x11 | PID_IR_CFG | R/W | 红外配置 |
| 0x12 | PID_DEV_INFO | R | 设备信息 |
| 0x13 | PID_SYS_PARAMS | R | 系统参数 |
| 0x14 | PID_SYS_CTRL | R/W | 系统控制（nodeId+channel+mode） |
| **0x18** | **PID_RELAY_CFG** | **R/W** | **中继配置（新增）** |
| 0xF0 | PID_ALL_STATE | R | 全状态聚合 |

### C. 中继内存表结构

```c
#define MAX_CHILD_NODES 8

typedef struct {
    uint16_t nodeId;
    uint8_t  online;       // 1=本周期有响应, 0=超时
    uint8_t  lastRssi;
    uint32_t lastSeenTs;
} child_info_t;

static child_info_t childTable[MAX_CHILD_NODES];
static uint8_t childCount = 0;
```

子节点首次出现时加入表，超过 3 个周期未响应标记为 offline。
