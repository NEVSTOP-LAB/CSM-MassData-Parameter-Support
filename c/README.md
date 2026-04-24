# CSM MassData Parameter Support — C language port

This folder contains a C-language implementation of the CSM MassData
Parameter Support add-on. It mirrors the LabVIEW API one-for-one so that C
applications can produce and consume MassData arguments that are
indistinguishable from those produced by the LabVIEW VIs in
[`addons/MassData-Parameter`](../addons/MassData-Parameter).

> [English] | [中文](#中文)

## Layout

```
c/
├── include/csm_massdata.h          # Public C API (Doxygen comments)
├── src/csm_massdata.c              # Portable implementation (Win32 / POSIX)
├── vs_test/
│   ├── csm_massdata_test.sln       # Visual Studio 2022 solution
│   ├── csm_massdata_test.vcxproj   # Test project (compiles as C)
│   ├── csm_massdata_test.vcxproj.filters
│   └── test_main.c                 # Self-contained test harness
└── README.md                       # This file
```

## API mapping

Every C entry point is the verbatim translation of a LabVIEW VI: same name,
same parameters (in the same order), same semantics.

| LabVIEW VI                                          | C function                                            |
| --------------------------------------------------- | ----------------------------------------------------- |
| `CSM - Config MassData Parameter Cache Size.vi`     | `CSM_ConfigMassDataParameterCacheSize`                |
| `CSM - Convert MassData to Argument.vim`            | `CSM_ConvertMassDataToArgument`                       |
| `CSM - Convert MassData to Argument With DataType.vim` | `CSM_ConvertMassDataToArgumentWithDataType`        |
| `CSM - Convert Argument to MassData.vim`            | `CSM_ConvertArgumentToMassData`                       |
| `CSM - MassData Data Type String.vi`                | `CSM_MassDataDataTypeString`                          |
| `CSM - MassData Parameter Status.vi`                | `CSM_MassDataParameterStatus`                         |

The on-the-wire MassData argument format is identical:

```
<MassData>Start:<N>;Size:<N>[;DataType:<T>]
```

## Building & running the tests

### Visual Studio (recommended on Windows)

1. Open `c/vs_test/csm_massdata_test.sln` in Visual Studio 2022.
2. Pick a configuration (`Debug|x64` is fine) and press <kbd>F5</kbd>.
3. The console window prints one line per assertion and the final
   `N/N assertions passed` summary.

### Command line (any platform with a C99 compiler)

```bash
cd c
cc -std=c99 -Wall -Wextra -Iinclude src/csm_massdata.c vs_test/test_main.c -o csm_test -lpthread
./csm_test
```

On Windows, replace `-lpthread` with nothing (the implementation falls back
to `CRITICAL_SECTION`) and use `cl /I include src\csm_massdata.c vs_test\test_main.c`.

## Linking the library into your own project

1. Add `c/include` to your project's include path.
2. Compile and link `c/src/csm_massdata.c` (it has no external dependencies
   beyond the C standard library and the platform's pthreads on POSIX).
3. `#include "csm_massdata.h"` and call any of the functions documented in
   the header.

A typical encode/decode round-trip looks like:

```c
#include "csm_massdata.h"

double samples[1024] = { /* ... */ };
char   arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
double restored[1024];
size_t restored_size = 0;

CSM_ConfigMassDataParameterCacheSize(64u * 1024u * 1024u);                      /* 64 MiB cache */
CSM_ConvertMassDataToArgumentWithDataType(samples, sizeof(samples),
                                          "1D DBL", arg, sizeof(arg));
/* ... ship `arg` over a CSM bus ... */
CSM_ConvertArgumentToMassData(arg, restored, sizeof(restored), &restored_size);
```

---

## 中文

本目录提供 CSM MassData Parameter Support 的 **C 语言移植**，接口名称、参数、
语义与 [`addons/MassData-Parameter`](../addons/MassData-Parameter) 中的 LabVIEW
VI **完全一致**，可与 LabVIEW 端无缝互通。

| LabVIEW VI                                          | C 函数                                                |
| --------------------------------------------------- | ----------------------------------------------------- |
| `CSM - Config MassData Parameter Cache Size.vi`     | `CSM_ConfigMassDataParameterCacheSize`                |
| `CSM - Convert MassData to Argument.vim`            | `CSM_ConvertMassDataToArgument`                       |
| `CSM - Convert MassData to Argument With DataType.vim` | `CSM_ConvertMassDataToArgumentWithDataType`        |
| `CSM - Convert Argument to MassData.vim`            | `CSM_ConvertArgumentToMassData`                       |
| `CSM - MassData Data Type String.vi`                | `CSM_MassDataDataTypeString`                          |
| `CSM - MassData Parameter Status.vi`                | `CSM_MassDataParameterStatus`                         |

参考字符串格式与 LabVIEW 端完全相同：
`<MassData>Start:<N>;Size:<N>[;DataType:<T>]`。

测试工程位于 `c/vs_test/`，使用 Visual Studio 2022 打开
`csm_massdata_test.sln` 即可编译并运行。也可以使用任意支持 C99 的编译器在
命令行直接构建（参见上文 “Command line” 章节）。
