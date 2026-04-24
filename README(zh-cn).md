# CSM-Addon-MassData-Parameter-Support

[English](./README.md) | [中文](./README(zh-cn).md)

[![Image](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/badge.svg?metric=installs)](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/)
[![Image](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/badge.svg?metric=stars)](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/)
[![GitHub all releases](https://img.shields.io/github/downloads/NEVSTOP-LAB/CSM-MassData-Parameter-Support/total)](https://github.com/NEVSTOP-LAB/CSM-MassData-Parameter-Support/releases)

## 概述

CSM-MassData-Parameter-Support 是 Communicable State Machine (CSM) 框架的插件，用于在 CSM 模块间高效传递大型数据。它通过引用机制传递数据，而非直接将大型数据编码到 API 字符串中，从而突破了 API 字符串在大数据传输上的限制。

## 为什么需要 MassData Support？

在 LabVIEW 测试测量应用中，处理波形、一维/二维数组等大型数据类型十分常见，尤其是在高采样率和多通道系统中。使用传统 API 字符串方式传输此类数据存在以下问题：

- 纯文本编码带来额外的内存开销
- 大型数据的编解码性能开销较大
- 调试日志中充斥大量文本，可读性下降

## 工作原理

MassData Support 基于以下简单而有效的原理：

1. **编码**：将大型数据转换为紧凑的引用字符串（"地址"），而非直接编码数据本身
2. **传输**：通过 CSM 隐形总线传递该引用字符串
3. **解码**：接收端 CSM 模块根据引用字符串从共享内存中还原原始数据

引用字符串包含三个字段：`标志`、`起始位置` 和 `大小`，共同定位存储在共享内存缓冲区中的实际数据。

## 主要优势

1. **高效传输**：仅传递紧凑的引用字符串，无需复制完整数据
2. **节省内存**：大型数据只存储一份，无论有多少接收方
3. **日志简洁**：紧凑的引用字符串使 CSM 日志输出更简洁易读

## 数据生命周期

- MassData 内部使用循环缓冲区
- 缓冲区满后，新数据从头部开始覆盖旧数据
- 被覆盖的数据无法恢复，解码将失败
- 同一应用内的所有 CSM 模块共享同一 MassData 缓冲区

## 最佳实践

1. **避免长期持有数据**：不要使用 MassData 存储需要长期保留的数据
2. **合理配置缓存大小**：使用 `Config MassData Parameter Cache Size.vi` 设置缓冲区大小
   - 不要过大（避免浪费内存）
   - 不要过小（防止频繁覆盖）
3. **监控缓存使用情况**：使用内置调试工具观察缓存占用，调整最佳配置

## 安装

通过 VIPM（VI Package Manager）安装。安装后，可在 CSM 插件选板中找到。

## 使用方法

1. 使用编码 VI 将大型数据转换为 MassData 参数
2. 通过 CSM 参数传递机制在模块间传递这些参数
3. 在接收端使用解码 VI 还原原始数据

## 示例

在示例文件夹中可找到以下演示：

1. MassData 参数格式
2. 在前面板显示 MassData 缓存状态
3. 在非 CSM 框架中使用 MassData
4. 在 CSM 框架中使用 MassData

## 开发环境

LabVIEW 2017 或更高版本

## 第三方语言绑定

[`c/`](./c) 目录提供 MassData API 的 **C 语言移植**，函数名称、参数及参考
字符串格式与 LabVIEW VI 完全一致，并附带 Visual Studio 2022 测试工程
[`c/vs_test`](./c/vs_test)。

## 许可证

本项目基于 MIT 许可证 — 详见 LICENSE 文件
