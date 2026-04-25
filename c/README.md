# CSM MassData Parameter Support —— C 语言移植

本目录提供 CSM MassData Parameter Support 插件的 C 语言实现，
接口与 [`addons/MassData-Parameter`](../addons/MassData-Parameter)
中的 LabVIEW VI 一一对应，可与 LabVIEW 端无缝互通。

## 目录结构

```
c/
├── include/csm_massdata.h          # 公开 C 接口（中文 Doxygen 注释）
├── src/csm_massdata.c              # 跨平台实现（Win32 / POSIX）
├── _test/
│   └── vs/
│       ├── csm_massdata_test.sln          # Visual Studio 2026 解决方案
│       ├── csm_massdata_test.vcxproj      # 测试工程（按 C 编译）
│       ├── csm_massdata_test.vcxproj.filters
│       └── test_main.c                    # 独立测试程序
└── README.md                       # 本文件
```

## 接口对应关系

每个 C 函数都是对应 LabVIEW VI 的逐字翻译：名称相同、参数顺序相同、
语义相同。

| LabVIEW VI                                          | C 函数                                                |
| --------------------------------------------------- | ----------------------------------------------------- |
| `CSM - Config MassData Parameter Cache Size.vi`     | `CSM_ConfigMassDataParameterCacheSize`                |
| `CSM - Convert MassData to Argument.vim`            | `CSM_ConvertMassDataToArgument`                       |
| `CSM - Convert MassData to Argument With DataType.vim` | `CSM_ConvertMassDataToArgumentWithDataType`        |
| `CSM - Convert Argument to MassData.vim`            | `CSM_ConvertArgumentToMassData`                       |
| `CSM - MassData Data Type String.vi`                | `CSM_MassDataDataTypeString`                          |
| `CSM - MassData Parameter Status.vi`                | `CSM_MassDataParameterStatus`                         |

参考字符串格式与 LabVIEW 端完全相同：

```
<MassData>Start:<N>;Size:<N>[;DataType:<T>]
```

## 构建与运行测试

### Visual Studio 2026（推荐）

1. 用 Visual Studio 2026 打开 `c/_test/vs/csm_massdata_test.sln`。
2. 选择任意配置（例如 `Debug|x64`），按 <kbd>F5</kbd> 启动。
3. 控制台会逐条打印断言结果，并在最后输出 `N/N 个断言通过` 摘要。

工程使用 `v145` 平台工具集（Visual Studio 2026 默认）。

### 命令行（任意支持 C99 的编译器）

```bash
cd c
cc -std=c99 -Wall -Wextra -Iinclude src/csm_massdata.c _test/vs/test_main.c -o csm_test -lpthread
./csm_test
```

Windows 下可用 `cl /I include src\csm_massdata.c _test\vs\test_main.c`，
不需要 `-lpthread`（实现会自动改用 `CRITICAL_SECTION`）。

## 在自己的工程中链接

1. 将 `c/include` 加入工程的头文件搜索路径。
2. 编译并链接 `c/src/csm_massdata.c`（除标准 C 库与 POSIX
   平台上的 pthreads 之外没有其它依赖）。
3. `#include "csm_massdata.h"` 后即可调用头文件中文档化的所有函数。

典型的编码 / 解码往返如下：

```c
#include "csm_massdata.h"

double samples[1024] = { /* ... */ };
char   arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
double restored[1024];
size_t restored_size = 0;

CSM_ConfigMassDataParameterCacheSize(64u * 1024u * 1024u);                      /* 64 MiB 缓冲区 */
CSM_ConvertMassDataToArgumentWithDataType(samples, sizeof(samples),
                                          "1D DBL", arg, sizeof(arg));
/* ... 通过 CSM 总线传递 `arg` ... */
CSM_ConvertArgumentToMassData(arg, restored, sizeof(restored), &restored_size);
```
