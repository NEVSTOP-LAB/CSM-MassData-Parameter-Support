/**
 * @file    test_main.c
 * @brief   Self-contained test harness for the CSM MassData C API.
 *
 * Exercises the public API documented in `csm_massdata.h`:
 *   - cache configuration and status reporting
 *   - encode / decode round-trip without data type
 *   - encode / decode round-trip with data type
 *   - data-type extraction (CSM - MassData Data Type String)
 *   - parse error handling
 *   - circular-buffer overwrite detection
 *
 * The harness exits with a non-zero status if any assertion fails so it can
 * be used both interactively and inside CI pipelines.
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

static void test_overwrite_detection(void)
{
    char     first_arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    char     filler_arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    uint8_t  small[8]  = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t  filler[32];
    uint8_t  restored[8];
    size_t   out = 0;
    int      i;

    /* Tiny cache so a follow-up write evicts the first payload. */
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

static void test_cache_too_small(void)
{
    char arg[CSM_MASSDATA_MAX_ARGUMENT_LEN];
    uint8_t big[64] = { 0 };

    CSM_TEST(CSM_ConfigMassDataParameterCacheSize(16) == CSM_MASSDATA_OK);
    CSM_TEST(CSM_ConvertMassDataToArgument(big, sizeof(big), arg, sizeof(arg))
             == CSM_MASSDATA_ERR_CACHE_TOO_SMALL);
}

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
    printf("CSM MassData C API test suite\n");
    printf("=============================\n");

    test_config_and_status();
    test_roundtrip_plain();
    test_roundtrip_with_type();
    test_datatype_absent();
    test_parse_errors();
    test_overwrite_detection();
    test_buffer_too_small();
    test_cache_too_small();
    test_status_reflects_last_ops();

    printf("\n%d/%d assertions passed, %d failed.\n",
           g_total - g_failures, g_total, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
