/**
 * @file    csm_massdata.c
 * @brief   Implementation of the CSM MassData Parameter Support C API.
 *
 * The MassData cache is a single, process-wide circular buffer guarded by a
 * mutex. Each successful encode advances a monotonically increasing 64-bit
 * write cursor (`write_total`) and copies the payload into the ring at
 * `write_total % capacity`, possibly wrapping around. The cursor value is
 * embedded in the returned `Start:` field so that decoders can detect whether
 * the requested payload is still resident or has already been overwritten by
 * later writes.
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
/*  Internal state                                                           */
/* ------------------------------------------------------------------------- */

#define CSM_MASSDATA_PREFIX "<MassData>"

typedef struct csm_massdata_state_s {
    int                       initialized;
    csm_mutex_t               mutex;
    uint8_t                  *buffer;       /* Circular byte buffer.       */
    size_t                    capacity;     /* Allocated buffer size.      */
    uint64_t                  write_total;  /* Bytes ever written.         */
    csm_massdata_operation_t  last_read;
    csm_massdata_operation_t  last_write;
} csm_massdata_state_t;

static csm_massdata_state_t g_state;

static void csm_massdata_lazy_init(void)
{
    if (g_state.initialized) {
        return;
    }
    /* The first call into the API is responsible for allocating the default
     * cache. The init flag itself is protected by the mutex once it exists,
     * but the first-time allocation is racy in a multi-threaded program, so
     * callers that care should invoke
     * CSM_ConfigMassDataParameterCacheSize() at startup. */
    g_state.buffer = (uint8_t *)malloc(CSM_MASSDATA_DEFAULT_CACHE_SIZE);
    g_state.capacity = (g_state.buffer != NULL) ? CSM_MASSDATA_DEFAULT_CACHE_SIZE : 0u;
    g_state.write_total = 0u;
    memset(&g_state.last_read,  0, sizeof(g_state.last_read));
    memset(&g_state.last_write, 0, sizeof(g_state.last_write));
    CSM_MUTEX_INIT(&g_state.mutex);
    g_state.initialized = 1;
}

/* ------------------------------------------------------------------------- */
/*  Helpers                                                                  */
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
 * Parse a MassData argument string of the form
 * `<MassData>Start:<N>;Size:<N>[;DataType:<T>]`.
 *
 * @param[out] data_type        Optional output buffer for the data-type tag.
 * @param[in]  data_type_cap    Capacity of @p data_type.
 *
 * @return CSM_MASSDATA_OK on success, CSM_MASSDATA_ERR_PARSE on malformed
 *         input, or CSM_MASSDATA_ERR_BUFFER_TOO_SMALL if the data-type tag
 *         does not fit in @p data_type.
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

    /* Optional ;DataType:<T> trailer. */
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
 * Copy @p size bytes from @p src into the circular buffer at the position
 * implied by the current value of @c g_state.write_total. The caller must
 * hold the mutex and must have validated that @p size <= capacity.
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
 * Copy @p size bytes from the circular buffer (starting at the absolute
 * cursor @p start) into @p dst. The caller must hold the mutex and must
 * have validated that the requested range is still resident.
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
/*  Public API                                                               */
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
        /* Reject characters that would break the reference-string grammar. */
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
    /* The window currently resident in the ring is
     * [write_total - capacity, write_total). Reject any payload whose end
     * lies outside this window. */
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
