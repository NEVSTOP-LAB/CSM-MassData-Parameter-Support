# CSM MassData Parameter Support —— C# 移植

本目录提供 CSM MassData Parameter Support 插件的 C# 实现，
接口与 [`addons/MassData-Parameter`](../addons/MassData-Parameter)
中的 LabVIEW VI 以及 [`c/`](../c) 目录下的 C 移植版本保持等价语义，
可与 LabVIEW / C 端无缝互通同一份 MassData 参数字符串。

## 目录结构

```
csharp/
├── src/CsmMassData/                       # .NET 类库（API 实现）
│   ├── CsmMassData.cs                     # 公开 API（中文 XML 文档注释）
│   ├── CsmMassDataException.cs            # 错误异常类
│   ├── CsmMassDataOperation.cs            # 读 / 写操作描述结构体
│   ├── CsmMassDataStatus.cs               # 与 C / LabVIEW 端对齐的状态码枚举
│   └── CsmMassData.csproj                 # 类库工程
├── _test/
│   └── vs/
│       ├── CsmMassData.Tests.sln          # Visual Studio 2026 解决方案
│       └── CsmMassData.Tests/             # MSTest 测试工程
│           ├── CsmMassDataTests.cs        # 端到端断言
│           ├── MSTestSettings.cs          # 全局禁用并行执行
│           └── CsmMassData.Tests.csproj
└── README.md                              # 本文件
```

## 接口对应关系

每个 C# 静态方法都是对应 LabVIEW VI 与 C 函数的逐字翻译：
名称相同、参数顺序一致、语义完全等价。

| LabVIEW VI                                          | C# 方法 (`Csm.MassData.CsmMassData`)                      |
| --------------------------------------------------- | --------------------------------------------------------- |
| `CSM - Config MassData Parameter Cache Size.vi`     | `ConfigMassDataParameterCacheSize`                        |
| `CSM - Convert MassData to Argument.vim`            | `ConvertMassDataToArgument`                               |
| `CSM - Convert MassData to Argument With DataType.vim` | `ConvertMassDataToArgumentWithDataType`                |
| `CSM - Convert Argument to MassData.vim`            | `ConvertArgumentToMassData`                               |
| `CSM - MassData Data Type String.vi`                | `MassDataDataTypeString`                                  |
| `CSM - MassData Parameter Status.vi`                | `MassDataParameterStatus`                                 |

参考字符串格式与 LabVIEW / C 端完全一致：

```
<MassData>Start:<N>;Size:<N>[;DataType:<T>]
```

## 错误处理

所有方法都遵循 .NET 的惯用做法：成功时返回结果值，失败时抛出
`CsmMassDataException`。该异常通过 `Status` 属性暴露与 C 端
`csm_massdata_status_t` 数值一致的 `CsmMassDataStatus` 枚举，
方便在 C# / C / LabVIEW 三种语言之间共享错误码语义。

## 线程安全

所有公开 API 都是线程安全的：

- 静态字段的初始化由 CLR 保证“仅一次且对其它线程可见”。
- 后续访问通过 `lock` 串行化。
- 建议在进入多线程阶段前主动调用
  `CsmMassData.ConfigMassDataParameterCacheSize(...)` 完成缓冲区配置，
  以获得最佳性能。

## 构建与运行测试

### Visual Studio 2026（推荐）

1. 用 Visual Studio 2026 打开 `csharp/_test/vs/CsmMassData.Tests.sln`。
2. 在“测试资源管理器”中运行 `CsmMassData.Tests` 中的全部用例，
   或直接按 <kbd>Ctrl+R</kbd>，<kbd>A</kbd> 运行所有测试。

### 命令行（任意 .NET 8 SDK）

```bash
cd csharp/_test/vs
dotnet test
```

## 在自己的工程中链接

```bash
dotnet add reference path/to/csharp/src/CsmMassData/CsmMassData.csproj
```

典型的编码 / 解码往返如下：

```csharp
using Csm.MassData;

byte[] samples = new byte[8 * 1024];

CsmMassData.ConfigMassDataParameterCacheSize(64 * 1024 * 1024); // 64 MiB 缓冲区
string arg = CsmMassData.ConvertMassDataToArgumentWithDataType(samples, "1D DBL");

// ... 通过 CSM 总线传递 arg ...

byte[] restored = CsmMassData.ConvertArgumentToMassData(arg);
```
