此项目是一个基于沁恒CH583的红外分体空调控制器，支持红外遥控、蓝牙遥控和Lora通信。

## 项目概述
- **MCU**: 沁恒 CH583 (RISC-V Qingke V4A 内核, 60MHz)
- **BLE**: 低功耗蓝牙 5.1 (服务 0xFFE0, 特征 0xFFE1/0xFFE2/0xFFE3)
- **红外**: 通过 UART3 与红外模块通信，支持红外匹配、学习(10通道)、发送
- **Lora**: SX1268 无线通信，支持多频道扫描注册
- **RTOS**: 支持 FreeRTOS / RT-Thread / HarmonyOS LiteOS-M 移植
- **OTA**: 支持 BLE 空中固件升级

## 核心功能
1. 蓝牙遥控空调 (开关/模式/温度/风速)
2. 红外码学习与匹配 (16M+ 组合空间)
3. Lora 组网通信 (5频道 × 10频点)
4. 本地规则引擎 (10条规则, 定时/温度/功率/运行时间触发)
5. 计量统计 (电量/运行时间/开关次数/故障次数)
6. OTA 固件升级

## 开发规范
- 所有涉及到与上位机蓝牙交换的操作及报文，更改后需同步到 `BLE/Peripheral/下位机蓝牙通讯.md` 文档中
- 协议定义以 `BLE/Peripheral/APP/include/peripheral.h` 中的枚举为准
- 设备结构体定义以 `BLE/Peripheral/APP/include/board.h` 中的 `t_dev` 为准
