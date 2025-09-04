# CSM-Addon-MassData-Parameter-Support

[English](./README.md) | [中文](./README(zh-cn).md)

[![Image](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/badge.svg?metric=installs)](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/)
[![Image](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/badge.svg?metric=stars)](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/)
[![GitHub all releases](https://img.shields.io/github/downloads/NEVSTOP-LAB/CSM-MassData-Parameter-Support/total)](https://github.com/NEVSTOP-LAB/CSM-MassData-Parameter-Support/releases)

## 概述

CSM-MassData-Parameter-Support 是 Communicable State Machine (CSM) 框架的一个扩展插件，用于实现 CSM 模块间大数据集的高效传输。它通过使用内存高效的引用机制而非直接编码大型数据结构，解决了 API String 在传输大数据时的局限性。

## 为什么需要 MassData Support？

在 LabVIEW 测试测量应用中，处理大型数据类型（如波形、一维/二维数组）是常见需求，特别是在高采样率和多通道系统中。使用传统的 API String 方法传输此类大型数据会效率低下，原因包括：

- 明文编码导致内存开销增加
- 编码/解码大型数据的性能问题
- 调试日志中因过多文本导致可读性降低

## 工作原理

MassData Support 基于一个简单但有效的原理运行：

1. **编码**：将大型数据转换为引用字符串（"地址"），而不是直接编码数据本身
2. **传输**：通过 CSM 的"隐形总线"发送这个紧凑的引用字符串
3. **解码**：接收端的 CSM 模块使用引用字符串检索原始大型数据

编码结果是一个包含三部分的字符串："标志"、"起始位置"和"大小"，它们作为"门牌号"来定位存储在专用内存空间中的实际数据。

## 主要优势

1. **高效传输**：仅传输紧凑的引用字符串而非整个数据集，避免内存复制
2. **内存优化**：无论接收方数量多少，大型数据都只存储在一个位置
3. **提高可读性**：紧凑的引用字符串更容易在 CSM Log 控件中显示，不会占用过多空间

## 数据生命周期

- MassData Support 内部使用循环缓冲区机制
- 当缓冲区满时，新数据将从开始位置覆盖旧数据
- 一旦被覆盖，原始数据将无法恢复，解码将失败
- 同一应用程序内的所有 CSM 模块共享相同的 MassData 缓冲区空间

## 最佳实践

1. **避免无限生命周期数据**：不要使用 MassData 存储需要无限期持久化的数据
2. **配置适当的缓存大小**：使用 "Config MassData Parameter Cache Size.vi" 设置最佳缓冲区大小
   - 不要太大（避免浪费内存）
   - 不要太小（防止频繁覆盖）
3. **使用调试工具**：利用提供的调试工具监控缓存使用情况，确定最佳配置

## 安装

通过 VIPM（VI Package Manager）安装该插件。安装后，您可以在 CSM 插件选板中找到它。

## 使用方法

1. 使用编码 VI 将大型数据转换为 MassData 参数
2. 通过 CSM 的参数传递机制在 CSM 模块之间传输这些参数
3. 在接收端使用解码 VI 检索原始大型数据

## 示例

在示例文件夹中查看以下演示：
1. MassData 参数格式
2. 在前面板上显示 MassData 缓存状态
3. 在非 CSM 框架中使用 MassData
4. 将 MassData 与 CSM 集成

## 开发环境

LabVIEW 2017 或更高版本

## 许可证

本项目基于 MIT 许可证 - 详情请参阅 LICENSE 文件