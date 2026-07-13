# CSM MassData Parameter Support —— Python 语言移植

本目录提供 CSM MassData Parameter Support 插件的 Python 实现，
接口与 [`addons/MassData-Parameter`](../addons/MassData-Parameter)
中的 LabVIEW VI 一一对应，同时与 [`c/`](../c) 目录下的 C 实现共享相同的
参考字符串格式与语义，可与 LabVIEW / C 端无缝互通。

## 目录结构

```
python/
├── csm_massdata.py                 # 公开 Python 接口（中文 docstring 注释）
├── _test/
│   └── test_csm_massdata.py        # 基于 unittest 的独立测试程序
└── README.md                       # 本文件
```

## 接口对应关系

每个 Python 函数都是对应 LabVIEW VI 的逐字翻译：名称相同、参数顺序相同、
语义相同；并与 [`c/include/csm_massdata.h`](../c/include/csm_massdata.h)
中的 C 函数一一对应。

| LabVIEW VI                                             | Python 函数                                            |
| ------------------------------------------------------ | ------------------------------------------------------ |
| `CSM - Config MassData Parameter Cache Size.vi`        | `CSM_ConfigMassDataParameterCacheSize`                 |
| `CSM - Convert MassData to Argument.vim`               | `CSM_ConvertMassDataToArgument`                        |
| `CSM - Convert MassData to Argument With DataType.vim` | `CSM_ConvertMassDataToArgumentWithDataType`            |
| `CSM - Convert Argument to MassData.vim`               | `CSM_ConvertArgumentToMassData`                        |
| `CSM - MassData Data Type String.vi`                   | `CSM_MassDataDataTypeString`                           |
| `CSM - MassData Parameter Status.vi`                   | `CSM_MassDataParameterStatus`                          |

参考字符串格式与 LabVIEW / C 端完全相同：

```
<MassData>Start:<N>;Size:<N>[;DataType:<T>]
```

状态码（`CsmMassDataStatus`）的整数值与 C 端
`csm_massdata_status_t` 一一对应，便于跨语言对照。

## 运行环境

- Python 3.8 及以上版本
- 仅依赖 Python 标准库（`enum`、`threading`、`typing`），无第三方依赖

## 运行测试

在仓库根目录或 `python/` 目录下，任选其一：

```bash
# 方式 1：直接以脚本运行
python python/_test/test_csm_massdata.py

# 方式 2：使用 unittest 模块发现并运行
cd python
python -m unittest _test.test_csm_massdata -v
```

测试覆盖：

- 缓冲区配置与状态查询
- 不带 / 带数据类型的编码 / 解码往返
- 数据类型字段的解析（含缺省与非法字符）
- 解析错误（前缀错误、数字非法、超出 64 位无符号整数范围、`DataType:`
  后缀含非法字符等）
- 环形缓冲区覆盖检测
- 写入数据超过缓冲容量
- 多线程并发编码 / 解码下的线程安全

## 在自己的工程中使用

将 `python/csm_massdata.py` 复制到工程目录，或将 `python/` 加入
`PYTHONPATH`，即可：

```python
from csm_massdata import (
    CSM_ConfigMassDataParameterCacheSize,
    CSM_ConvertMassDataToArgumentWithDataType,
    CSM_ConvertArgumentToMassData,
    CsmMassDataStatus,
)

# 应用启动阶段在单线程上下文中初始化（可选；不调用则使用 50 MiB 默认值）
CSM_ConfigMassDataParameterCacheSize(64 * 1024 * 1024)

samples = b"\x01\x02\x03\x04" * 256
status, arg = CSM_ConvertMassDataToArgumentWithDataType(samples, "1D U8")
assert status == CsmMassDataStatus.OK
# ... 通过 CSM 总线传递 arg ...
status, restored = CSM_ConvertArgumentToMassData(arg)
assert status == CsmMassDataStatus.OK
assert restored == samples
```

## 线程安全

内部环形缓冲区与游标在模块导入（`import`）时即一次性完成初始化，
避免 C 端历史上出现过的延迟初始化竞态。所有公开函数对内部状态的
访问均通过同一把 `threading.Lock` 串行化，可在多线程环境下安全调用。
