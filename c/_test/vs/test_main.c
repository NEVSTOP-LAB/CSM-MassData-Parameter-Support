/**
 * @file    test_main.c
 * @brief   CSM MassData C 接口的独立测试程序。
 *
 * 覆盖 `csm_massdata.h` 中公开的所有 API：
 *   - 缓冲区配置与状态查询
 *   - 不带数据类型的编码 / 解码往返
 *   - 带数据类型的编码 / 解码往返
 *   - 数据类型解析（CSM - MassData Data Type String）
 *   - 解析错误的处理
 *   - 环形缓冲区覆盖检测
 *
 * 任意断言失败时程序以非零状态退出，方便在交互模式与 CI 流水线中
 * 同时使用。
 */

#include "csm_massdata.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static int g_total    = 0;

#define CSM_TEST(cond)                                                        \
    do {                                                                      \
        ++g_total;                                                            \
        if (!(cond)) {                                                        \
            ++g_failures;                                                     \
            fprintf(stderr, "[FAIL] %s:%d  %s\n",                             \
                    __FILE__, __LINE__, #cond);                               \
        } else {                                                              \
            printf("[ OK ] %s\n", #cond);                                     \
        }                                                                     \
    } while (0)

/* 测试：缓冲区配置与状态查询 */
static void test_config_and_status(void)
{
    csm_massdata_operation_t r, w;
    size_t cache = 0;

    CSM_TEST(CSM_ConfigMassDataParameterCacheSize(0) == CSM_MASSDATA_ERR_INVALID_ARG);
    CSM_TEST(CSM_ConfigMassDataParameterCacheSize(1024) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_MassDataParameterStatus(&r, &w, &cache) == CSM_MASSDATA_OK);
    CSM_TEST(cache == 1024);
    CSM_TEST(r.size == 0 && w.size == 0);
}

/* 测试：不带数据类型的编码 / 解码往返 */
static void test_roundtrip_plain(void)
{
    const int32_t source[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    int32_t       restored[8];
    char          arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    size_t        out_size = 0;

    CSM_TEST(CSM_ConfigMassDataParameterCacheSize(4096) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_ConvertMassDataToArgument(source, sizeof(source),
                                           arg, sizeof(arg)) == CSM_MASSDATA_OK);
    CSM_TEST(strncmp(arg, "<MassData>Start:", 16) == 0);

    memset(restored, 0, sizeof(restored));
    CSM_TEST(CSM_ConvertArgumentToMassData(arg, restored, sizeof(restored),
                                           &out_size) == CSM_MASSDATA_OK);
    CSM_TEST(out_size == sizeof(source));
    CSM_TEST(memcmp(source, restored, sizeof(source)) == 0);
}

/* 测试：带数据类型的编码 / 解码往返 */
static void test_roundtrip_with_type(void)
{
    const double  source[4] = { 1.5, -2.5, 3.5, -4.5 };
    double        restored[4];
    char          arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    char          dup[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    char          type[CSM_MASSDATA_MAX_DATATYPE_LEN];
    size_t        out_size = 0;

    CSM_TEST(CSM_ConfigMassDataParameterCacheSize(4096) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_ConvertMassDataToArgumentWithDataType(source, sizeof(source),
                                                       "1D DBL", arg,
                                                       sizeof(arg))
             == CSM_MASSDATA_OK);
    CSM_TEST(strstr(arg, ";DataType:1D DBL") != NULL);

    CSM_TEST(CSM_MassDataDataTypeString(arg, dup, sizeof(dup),
                                        type, sizeof(type)) == CSM_MASSDATA_OK);
    CSM_TEST(strcmp(type, "1D DBL") == 0);
    CSM_TEST(strcmp(dup,  arg)      == 0);

    memset(restored, 0, sizeof(restored));
    CSM_TEST(CSM_ConvertArgumentToMassData(arg, restored, sizeof(restored),
                                           &out_size) == CSM_MASSDATA_OK);
    CSM_TEST(out_size == sizeof(source));
    CSM_TEST(memcmp(source, restored, sizeof(source)) == 0);
}

/* 测试：参数中没有数据类型字段时，解析结果应为空字符串 */
static void test_datatype_absent(void)
{
    char arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    char type[CSM_MASSDATA_MAX_DATATYPE_LEN];
    const uint8_t payload[3] = { 0xAA, 0xBB, 0xCC };

    CSM_TEST(CSM_ConfigMassDataParameterCacheSize(4096) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_ConvertMassDataToArgument(payload, sizeof(payload),
                                           arg, sizeof(arg)) == CSM_MASSDATA_OK);
    type[0] = 'x';
    CSM_TEST(CSM_MassDataDataTypeString(arg, NULL, 0, type, sizeof(type))
             == CSM_MASSDATA_OK);
    CSM_TEST(type[0] == '\0');
}

/* 测试：解析非法字符串时返回 PARSE 错误 */
static void test_parse_errors(void)
{
    size_t out = 0;
    uint8_t buf[16];

    CSM_TEST(CSM_ConvertArgumentToMassData("garbage", buf, sizeof(buf), &out)
             == CSM_MASSDATA_ERR_PARSE);
    CSM_TEST(CSM_ConvertArgumentToMassData("<MassData>Start:abc;Size:1",
                                           buf, sizeof(buf), &out)
             == CSM_MASSDATA_ERR_PARSE);
    CSM_TEST(CSM_ConvertArgumentToMassData("<MassData>Start:0;Size:1;wrong",
                                           buf, sizeof(buf), &out)
             == CSM_MASSDATA_ERR_PARSE);
}

/* 测试：旧数据被环形缓冲区覆盖后，应报告 OVERWRITTEN */
static void test_overwrite_detection(void)
{
    char     first_arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    char     filler_arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    uint8_t  small[8]  = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t  filler[32];
    uint8_t  restored[8];
    size_t   out = 0;
    int      i;

    /* 缓冲区故意设得很小，后续写入会把第一份数据挤掉。 */
    CSM_TEST(CSM_ConfigMassDataParameterCacheSize(32) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_ConvertMassDataToArgument(small, sizeof(small),
                                           first_arg, sizeof(first_arg))
             == CSM_MASSDATA_OK);
    for (i = 0; i < (int)sizeof(filler); ++i) filler[i] = (uint8_t)i;
    CSM_TEST(CSM_ConvertMassDataToArgument(filler, sizeof(filler),
                                           filler_arg, sizeof(filler_arg))
             == CSM_MASSDATA_OK);
    CSM_TEST(CSM_ConvertArgumentToMassData(first_arg, restored,
                                           sizeof(restored), &out)
             == CSM_MASSDATA_ERR_OVERWRITTEN);
}

/* 测试：输出缓冲区太小时返回 BUFFER_TOO_SMALL，并报告所需大小 */
static void test_buffer_too_small(void)
{
    const uint8_t payload[10] = { 0 };
    char arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    uint8_t small_out[2];
    size_t out = 0;

    CSM_TEST(CSM_ConfigMassDataParameterCacheSize(4096) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_ConvertMassDataToArgument(payload, sizeof(payload),
                                           arg, sizeof(arg)) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_ConvertArgumentToMassData(arg, small_out, sizeof(small_out),
                                           &out) == CSM_MASSDATA_ERR_BUFFER_TOO_SMALL);
    CSM_TEST(out == sizeof(payload));
}

/* 测试：写入数据大于缓冲区容量时返回 CACHE_TOO_SMALL */
static void test_cache_too_small(void)
{
    char arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    uint8_t big[64] = { 0 };

    CSM_TEST(CSM_ConfigMassDataParameterCacheSize(16) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_ConvertMassDataToArgument(big, sizeof(big), arg, sizeof(arg))
             == CSM_MASSDATA_ERR_CACHE_TOO_SMALL);
}

/* 测试：状态查询应当反映最近一次的读 / 写操作 */
static void test_status_reflects_last_ops(void)
{
    csm_massdata_operation_t r, w;
    size_t cache = 0;
    char arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    uint8_t payload[5] = { 9, 8, 7, 6, 5 };
    uint8_t restored[5];
    size_t out = 0;

    CSM_TEST(CSM_ConfigMassDataParameterCacheSize(1024) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_ConvertMassDataToArgument(payload, sizeof(payload),
                                           arg, sizeof(arg)) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_ConvertArgumentToMassData(arg, restored, sizeof(restored),
                                           &out) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_MassDataParameterStatus(&r, &w, &cache) == CSM_MASSDATA_OK);
    CSM_TEST(w.size == sizeof(payload));
    CSM_TEST(r.size == sizeof(payload));
    CSM_TEST(cache == 1024);
}

int main(void)
{
    printf("CSM MassData C API 测试套件\n");
    printf("===========================\n");

    test_config_and_status();
    test_roundtrip_plain();
    test_roundtrip_with_type();
    test_datatype_absent();
    test_parse_errors();
    test_overwrite_detection();
    test_buffer_too_small();
    test_cache_too_small();
    test_status_reflects_last_ops();

    printf("\n%d/%d 个断言通过，%d 个失败。\n",
           g_total - g_failures, g_total, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
