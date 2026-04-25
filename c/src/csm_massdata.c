/**
 * @file    csm_massdata.c
 * @brief   CSM MassData Parameter Support C 接口的实现。
 *
 * MassData 缓存是进程内唯一的环形缓冲区，由互斥量保护。每次成功的
 * 编码会推进一个单调递增的 64 位写游标 `write_total`，并将数据
 * 复制到环形缓冲区中 `write_total % capacity` 的位置（必要时回绕）。
 * 该游标值会被写入返回字符串的 `Start:` 字段，从而让解码端能够
 * 判断引用的数据是否仍驻留在缓存中、还是已经被后续写入覆盖。
 */

#include "csm_massdata.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
typedef CRITICAL_SECTION csm_mutex_t;
#  define CSM_MUTEX_INIT(m)    InitializeCriticalSection(m)
#  define CSM_MUTEX_LOCK(m)    EnterCriticalSection(m)
#  define CSM_MUTEX_UNLOCK(m)  LeaveCriticalSection(m)
#else
#  include <pthread.h>
typedef pthread_mutex_t csm_mutex_t;
#  define CSM_MUTEX_INIT(m)    pthread_mutex_init((m), NULL)
#  define CSM_MUTEX_LOCK(m)    pthread_mutex_lock(m)
#  define CSM_MUTEX_UNLOCK(m)  pthread_mutex_unlock(m)
#endif

/* ------------------------------------------------------------------------- */
/*  内部状态                                                                 */
/* ------------------------------------------------------------------------- */

#define CSM_MASSDATA_PREFIX "<MassData>"

typedef struct csm_massdata_state_s {
    int                       initialized;
    csm_mutex_t               mutex;
    uint8_t                  *buffer;       /* 环形字节缓冲区。            */
    size_t                    capacity;     /* 缓冲区已分配的大小。        */
    uint64_t                  write_total;  /* 累计已写入的字节数。        */
    csm_massdata_operation_t  last_read;
    csm_massdata_operation_t  last_write;
} csm_massdata_state_t;

static csm_massdata_state_t g_state;

static void csm_massdata_lazy_init(void)
{
    if (g_state.initialized) {
        return;
    }
    /* 第一次调用本模块的 API 时负责分配默认缓冲区。
     * 一旦初始化完成，该标志将由互斥量保护；但首次分配本身
     * 在多线程程序中存在竞态，因此关心线程安全的调用方应当
     * 在程序启动时主动调用 CSM_ConfigMassDataParameterCacheSize()。 */
    g_state.buffer = (uint8_t *)malloc(CSM_MASSDATA_DEFAULT_CACHE_SIZE);
    g_state.capacity = (g_state.buffer != NULL) ? CSM_MASSDATA_DEFAULT_CACHE_SIZE : 0u;
    g_state.write_total = 0u;
    memset(&g_state.last_read,  0, sizeof(g_state.last_read));
    memset(&g_state.last_write, 0, sizeof(g_state.last_write));
    CSM_MUTEX_INIT(&g_state.mutex);
    g_state.initialized = 1;
}

/* ------------------------------------------------------------------------- */
/*  内部辅助函数                                                             */
/* ------------------------------------------------------------------------- */

static int csm_massdata_starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

/**
 * 解析形如 `<MassData>Start:<N>;Size:<N>[;DataType:<T>]` 的 MassData
 * 参数字符串。
 *
 * @param[out] data_type        可选的数据类型标签输出缓冲区。
 * @param[in]  data_type_cap    @p data_type 的容量。
 *
 * @return 成功返回 CSM_MASSDATA_OK；输入非法返回 CSM_MASSDATA_ERR_PARSE；
 *         数据类型标签放不下时返回 CSM_MASSDATA_ERR_BUFFER_TOO_SMALL。
 */
static csm_massdata_status_t csm_massdata_parse(const char *argument,
                                                uint64_t   *start_out,
                                                uint64_t   *size_out,
                                                char       *data_type,
                                                size_t      data_type_cap)
{
    const char *p;
    char       *end;
    unsigned long long tmp;

    if (argument == NULL || start_out == NULL || size_out == NULL) {
        return CSM_MASSDATA_ERR_INVALID_ARG;
    }
    if (!csm_massdata_starts_with(argument, CSM_MASSDATA_PREFIX)) {
        return CSM_MASSDATA_ERR_PARSE;
    }
    p = argument + (sizeof(CSM_MASSDATA_PREFIX) - 1);

    if (strncmp(p, "Start:", 6) != 0) {
        return CSM_MASSDATA_ERR_PARSE;
    }
    p += 6;
    tmp = strtoull(p, &end, 10);
    if (end == p || *end != ';') {
        return CSM_MASSDATA_ERR_PARSE;
    }
    *start_out = (uint64_t)tmp;
    p = end + 1;

    if (strncmp(p, "Size:", 5) != 0) {
        return CSM_MASSDATA_ERR_PARSE;
    }
    p += 5;
    tmp = strtoull(p, &end, 10);
    if (end == p) {
        return CSM_MASSDATA_ERR_PARSE;
    }
    *size_out = (uint64_t)tmp;
    p = end;

    /* 可选的 ;DataType:<T> 后缀。 */
    if (data_type != NULL && data_type_cap > 0u) {
        data_type[0] = '\0';
    }
    if (*p == '\0') {
        return CSM_MASSDATA_OK;
    }
    if (*p != ';') {
        return CSM_MASSDATA_ERR_PARSE;
    }
    p++;
    if (strncmp(p, "DataType:", 9) != 0) {
        return CSM_MASSDATA_ERR_PARSE;
    }
    p += 9;
    if (data_type != NULL && data_type_cap > 0u) {
        size_t len = strlen(p);
        if (len + 1u > data_type_cap) {
            return CSM_MASSDATA_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(data_type, p, len + 1u);
    }
    return CSM_MASSDATA_OK;
}

/**
 * 将 @p src 指向的 @p size 字节按照 @c g_state.write_total 暗示的位置
 * 写入环形缓冲区。调用者必须持有互斥量，并已确保 @p size 不大于
 * 缓冲区容量。
 */
static void csm_massdata_ring_write(const uint8_t *src, size_t size)
{
    size_t   offset    = (size_t)(g_state.write_total % (uint64_t)g_state.capacity);
    size_t   first_run = g_state.capacity - offset;

    if (size <= first_run) {
        memcpy(g_state.buffer + offset, src, size);
    } else {
        memcpy(g_state.buffer + offset, src, first_run);
        memcpy(g_state.buffer, src + first_run, size - first_run);
    }
}

/**
 * 从环形缓冲区中以绝对游标 @p start 起始位置读取 @p size 字节到
 * @p dst。调用者必须持有互斥量，并已确认所请求的范围仍然驻留。
 */
static void csm_massdata_ring_read(uint64_t start, uint8_t *dst, size_t size)
{
    size_t offset    = (size_t)(start % (uint64_t)g_state.capacity);
    size_t first_run = g_state.capacity - offset;

    if (size <= first_run) {
        memcpy(dst, g_state.buffer + offset, size);
    } else {
        memcpy(dst, g_state.buffer + offset, first_run);
        memcpy(dst + first_run, g_state.buffer, size - first_run);
    }
}

/* ------------------------------------------------------------------------- */
/*  公开 API                                                                 */
/* ------------------------------------------------------------------------- */

csm_massdata_status_t CSM_ConfigMassDataParameterCacheSize(size_t size)
{
    uint8_t *new_buf;

    if (size == 0u) {
        return CSM_MASSDATA_ERR_INVALID_ARG;
    }

    csm_massdata_lazy_init();

    new_buf = (uint8_t *)malloc(size);
    if (new_buf == NULL) {
        return CSM_MASSDATA_ERR_NO_MEMORY;
    }

    CSM_MUTEX_LOCK(&g_state.mutex);
    free(g_state.buffer);
    g_state.buffer       = new_buf;
    g_state.capacity     = size;
    g_state.write_total  = 0u;
    memset(&g_state.last_read,  0, sizeof(g_state.last_read));
    memset(&g_state.last_write, 0, sizeof(g_state.last_write));
    CSM_MUTEX_UNLOCK(&g_state.mutex);

    return CSM_MASSDATA_OK;
}

static csm_massdata_status_t csm_massdata_encode(const void *data,
                                                 size_t      data_size,
                                                 const char *data_type,
                                                 char       *argument,
                                                 size_t      argument_cap)
{
    uint64_t start_cursor;
    int      written;

    if (argument == NULL || argument_cap == 0u) {
        return CSM_MASSDATA_ERR_INVALID_ARG;
    }
    if (data_size > 0u && data == NULL) {
        return CSM_MASSDATA_ERR_INVALID_ARG;
    }
    if (data_type != NULL) {
        size_t dt_len = strlen(data_type);
        if (dt_len + 1u > CSM_MASSDATA_MAX_DATATYPE_LEN) {
            return CSM_MASSDATA_ERR_BUFFER_TOO_SMALL;
        }
        /* 拒绝可能破坏引用字符串语法的字符。 */
        if (strpbrk(data_type, ";<>") != NULL) {
            return CSM_MASSDATA_ERR_INVALID_ARG;
        }
    }

    csm_massdata_lazy_init();

    CSM_MUTEX_LOCK(&g_state.mutex);
    if (g_state.buffer == NULL) {
        CSM_MUTEX_UNLOCK(&g_state.mutex);
        return CSM_MASSDATA_ERR_NO_MEMORY;
    }
    if (data_size > g_state.capacity) {
        CSM_MUTEX_UNLOCK(&g_state.mutex);
        return CSM_MASSDATA_ERR_CACHE_TOO_SMALL;
    }

    start_cursor = g_state.write_total;
    if (data_size > 0u) {
        csm_massdata_ring_write((const uint8_t *)data, data_size);
        g_state.write_total += (uint64_t)data_size;
    }
    g_state.last_write.start = start_cursor;
    g_state.last_write.size  = (uint64_t)data_size;
    CSM_MUTEX_UNLOCK(&g_state.mutex);

    if (data_type != NULL) {
        written = snprintf(argument, argument_cap,
                           CSM_MASSDATA_PREFIX
                           "Start:%" PRIu64 ";Size:%" PRIu64 ";DataType:%s",
                           start_cursor, (uint64_t)data_size, data_type);
    } else {
        written = snprintf(argument, argument_cap,
                           CSM_MASSDATA_PREFIX
                           "Start:%" PRIu64 ";Size:%" PRIu64,
                           start_cursor, (uint64_t)data_size);
    }
    if (written < 0 || (size_t)written >= argument_cap) {
        return CSM_MASSDATA_ERR_BUFFER_TOO_SMALL;
    }
    return CSM_MASSDATA_OK;
}

csm_massdata_status_t CSM_ConvertMassDataToArgument(const void *data,
                                                    size_t      data_size,
                                                    char       *argument,
                                                    size_t      argument_cap)
{
    return csm_massdata_encode(data, data_size, NULL, argument, argument_cap);
}

csm_massdata_status_t CSM_ConvertMassDataToArgumentWithDataType(const void *data,
                                                                size_t      data_size,
                                                                const char *data_type,
                                                                char       *argument,
                                                                size_t      argument_cap)
{
    if (data_type == NULL) {
        return CSM_MASSDATA_ERR_INVALID_ARG;
    }
    return csm_massdata_encode(data, data_size, data_type, argument, argument_cap);
}

csm_massdata_status_t CSM_ConvertArgumentToMassData(const char *argument,
                                                    void       *data,
                                                    size_t      data_cap,
                                                    size_t     *data_size_out)
{
    csm_massdata_status_t status;
    uint64_t              start = 0u;
    uint64_t              size  = 0u;

    if (data_size_out == NULL) {
        return CSM_MASSDATA_ERR_INVALID_ARG;
    }

    status = csm_massdata_parse(argument, &start, &size, NULL, 0u);
    if (status != CSM_MASSDATA_OK) {
        return status;
    }

    csm_massdata_lazy_init();

    *data_size_out = (size_t)size;

    if (size > 0u && data == NULL) {
        return CSM_MASSDATA_ERR_INVALID_ARG;
    }
    if (size > (uint64_t)data_cap) {
        return CSM_MASSDATA_ERR_BUFFER_TOO_SMALL;
    }

    CSM_MUTEX_LOCK(&g_state.mutex);
    if (g_state.buffer == NULL) {
        CSM_MUTEX_UNLOCK(&g_state.mutex);
        return CSM_MASSDATA_ERR_NO_MEMORY;
    }
    if (size > (uint64_t)g_state.capacity) {
        CSM_MUTEX_UNLOCK(&g_state.mutex);
        return CSM_MASSDATA_ERR_OVERWRITTEN;
    }
    /* 当前驻留在环形缓冲区中的窗口为
     * [write_total - capacity, write_total)。任何末端落在该窗口
     * 之外的请求都视为已被覆盖。 */
    {
        uint64_t end = start + size;
        uint64_t oldest = (g_state.write_total > (uint64_t)g_state.capacity)
                          ? g_state.write_total - (uint64_t)g_state.capacity
                          : 0u;
        if (start < oldest || end > g_state.write_total) {
            CSM_MUTEX_UNLOCK(&g_state.mutex);
            return CSM_MASSDATA_ERR_OVERWRITTEN;
        }
        if (size > 0u) {
            csm_massdata_ring_read(start, (uint8_t *)data, (size_t)size);
        }
        g_state.last_read.start = start;
        g_state.last_read.size  = size;
    }
    CSM_MUTEX_UNLOCK(&g_state.mutex);

    return CSM_MASSDATA_OK;
}

csm_massdata_status_t CSM_MassDataDataTypeString(const char *argument,
                                                 char       *argument_dup,
                                                 size_t      argument_dup_cap,
                                                 char       *data_type,
                                                 size_t      data_type_cap)
{
    csm_massdata_status_t status;
    uint64_t              start = 0u;
    uint64_t              size  = 0u;

    if (argument == NULL || data_type == NULL || data_type_cap == 0u) {
        return CSM_MASSDATA_ERR_INVALID_ARG;
    }

    status = csm_massdata_parse(argument, &start, &size, data_type, data_type_cap);
    if (status != CSM_MASSDATA_OK) {
        return status;
    }

    if (argument_dup != NULL) {
        size_t len = strlen(argument);
        if (len + 1u > argument_dup_cap) {
            return CSM_MASSDATA_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(argument_dup, argument, len + 1u);
    }
    return CSM_MASSDATA_OK;
}

csm_massdata_status_t CSM_MassDataParameterStatus(csm_massdata_operation_t *active_read,
                                                  csm_massdata_operation_t *active_write,
                                                  size_t                   *cache_size)
{
    csm_massdata_lazy_init();
    CSM_MUTEX_LOCK(&g_state.mutex);
    if (active_read != NULL) {
        *active_read = g_state.last_read;
    }
    if (active_write != NULL) {
        *active_write = g_state.last_write;
    }
    if (cache_size != NULL) {
        *cache_size = g_state.capacity;
    }
    CSM_MUTEX_UNLOCK(&g_state.mutex);
    return CSM_MASSDATA_OK;
}
