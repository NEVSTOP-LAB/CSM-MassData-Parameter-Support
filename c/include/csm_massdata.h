/**
 * @file    csm_massdata.h
 * @brief   C-language port of the CSM MassData Parameter Support add-on.
 *
 * This header exposes a C API that is functionally and nominally identical
 * to the LabVIEW VIs shipped under
 * `addons/MassData-Parameter/CSM MassData Parameter Support.lvlib`.
 *
 * The function names, parameter order and semantics intentionally mirror the
 * corresponding LabVIEW VIs so that user code written in either language can
 * exchange MassData arguments without any conversion layer.
 *
 * @par MassData Argument Format
 * A MassData argument is a printable, ASCII-only reference string that points
 * to a payload kept in a process-wide circular buffer. Two forms are supported:
 *
 *   - Without data type: `<MassData>Start:<N>;Size:<N>`
 *   - With    data type: `<MassData>Start:<N>;Size:<N>;DataType:<T>`
 *
 * where `<N>` is a non-negative decimal integer and `<T>` is a free-form
 * data-type tag (e.g. `1D I32`, `Waveform`, ...) defined by the CSM Data Type
 * String VI.
 *
 * @par Data Lifecycle
 * MassData uses an internally-managed circular buffer. When the buffer is
 * full, new writes overwrite the oldest data starting from the beginning.
 * Overwritten payloads cannot be recovered: a subsequent decode of such a
 * reference returns an error. All callers within the same process share the
 * same MassData buffer.
 *
 * @par Thread Safety
 * All public functions in this header are thread-safe. Concurrent calls from
 * multiple threads are serialised through an internal mutex.
 *
 * @copyright MIT License - see the LICENSE file at the repository root.
 */

#ifndef CSM_MASSDATA_H
#define CSM_MASSDATA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/*  Constants & types                                                        */
/* ------------------------------------------------------------------------- */

/** Default MassData cache size in bytes (50 MiB), matching the LabVIEW VI. */
#define CSM_MASSDATA_DEFAULT_CACHE_SIZE  ((size_t)(50u * 1024u * 1024u))

/** Maximum length (including the terminating NUL) of an encoded MassData
 *  argument string returned by the encoding functions. */
#define CSM_MASSDATA_MAX_ARGUMENT_LEN    256

/** Maximum length (including the terminating NUL) of a data-type tag. */
#define CSM_MASSDATA_MAX_DATATYPE_LEN    128

/**
 * @brief Status codes returned by every MassData API function.
 */
typedef enum csm_massdata_status_e {
    CSM_MASSDATA_OK                   =  0, /**< Operation completed successfully. */
    CSM_MASSDATA_ERR_INVALID_ARG      = -1, /**< NULL pointer or otherwise invalid argument. */
    CSM_MASSDATA_ERR_BUFFER_TOO_SMALL = -2, /**< User-supplied output buffer is too small. */
    CSM_MASSDATA_ERR_PARSE            = -3, /**< MassData argument string could not be parsed. */
    CSM_MASSDATA_ERR_OVERWRITTEN      = -4, /**< Referenced data has already been overwritten. */
    CSM_MASSDATA_ERR_CACHE_TOO_SMALL  = -5, /**< Payload is larger than the configured cache. */
    CSM_MASSDATA_ERR_NO_MEMORY        = -6  /**< Memory allocation failed. */
} csm_massdata_status_t;

/**
 * @brief Description of the most recent read or write performed against the
 *        MassData circular buffer.
 *
 * Equivalent to the @c Active&nbsp;Read&nbsp;Operation /
 * @c Active&nbsp;Write&nbsp;Operation cluster returned by
 * `CSM - MassData Parameter Status.vi`.
 */
typedef struct csm_massdata_operation_s {
    uint64_t start;  /**< Start offset (bytes) inside the cache. */
    uint64_t size;   /**< Length of the operation in bytes.       */
} csm_massdata_operation_t;

/* ------------------------------------------------------------------------- */
/*  API functions - each function mirrors the corresponding LabVIEW VI       */
/* ------------------------------------------------------------------------- */

/**
 * @brief Configure the MassData background cache size.
 *
 * Wraps `CSM - Config MassData Parameter Cache Size.vi`.
 *
 * Reallocates the internal circular buffer to @p size bytes. Calling this
 * function while the application is running discards any currently cached
 * data, exactly like the LabVIEW VI; it should normally be invoked once,
 * before any encode/decode call.
 *
 * @param[in] size  New cache size in bytes. The default (when this function
 *                  is never called) is @ref CSM_MASSDATA_DEFAULT_CACHE_SIZE.
 *
 * @return @ref CSM_MASSDATA_OK on success,
 *         @ref CSM_MASSDATA_ERR_INVALID_ARG if @p size is zero,
 *         @ref CSM_MASSDATA_ERR_NO_MEMORY if allocation failed.
 */
csm_massdata_status_t CSM_ConfigMassDataParameterCacheSize(size_t size);

/**
 * @brief Convert raw data into a MassData argument (no embedded data type).
 *
 * Wraps `CSM - Convert MassData to Argument.vim`. The raw payload is copied
 * into the circular buffer and a reference string of the form
 * `<MassData>Start:<N>;Size:<N>` is written into @p argument.
 *
 * @param[in]  data         Pointer to the raw bytes to store. May be @c NULL
 *                          only when @p data_size is zero.
 * @param[in]  data_size    Length of @p data in bytes.
 * @param[out] argument     Caller-allocated buffer that receives the
 *                          NUL-terminated MassData argument string.
 * @param[in]  argument_cap Capacity of @p argument in bytes (recommended:
 *                          @ref CSM_MASSDATA_MAX_ARGUMENT_LEN).
 *
 * @return @ref CSM_MASSDATA_OK or an error code.
 */
csm_massdata_status_t CSM_ConvertMassDataToArgument(const void *data,
                                                    size_t      data_size,
                                                    char       *argument,
                                                    size_t      argument_cap);

/**
 * @brief Convert raw data into a MassData argument that embeds a data-type
 *        tag.
 *
 * Wraps `CSM - Convert MassData to Argument With DataType.vim`. The produced
 * argument is `<MassData>Start:<N>;Size:<N>;DataType:<data_type>`.
 *
 * @param[in]  data         Pointer to the raw bytes to store.
 * @param[in]  data_size    Length of @p data in bytes.
 * @param[in]  data_type    NUL-terminated data-type string (e.g. `"1D I32"`).
 *                          Must contain neither `';'` nor `'<'`/`'>'`.
 * @param[out] argument     Caller-allocated buffer receiving the result.
 * @param[in]  argument_cap Capacity of @p argument in bytes.
 *
 * @return @ref CSM_MASSDATA_OK or an error code.
 */
csm_massdata_status_t CSM_ConvertMassDataToArgumentWithDataType(const void *data,
                                                                size_t      data_size,
                                                                const char *data_type,
                                                                char       *argument,
                                                                size_t      argument_cap);

/**
 * @brief Convert a MassData argument back into the original raw data.
 *
 * Wraps `CSM - Convert Argument to MassData.vim`. The reference string in
 * @p argument is parsed and the corresponding payload is copied into
 * @p data. The optional `Type` input of the LabVIEW VI is intentionally
 * omitted: the returned bytes are the verbatim payload that was previously
 * stored, regardless of any embedded type tag.
 *
 * @param[in]  argument        NUL-terminated MassData argument string.
 * @param[out] data            Caller-allocated buffer that receives the data.
 * @param[in]  data_cap        Capacity of @p data in bytes.
 * @param[out] data_size_out   Receives the actual number of bytes written
 *                             into @p data. Must not be @c NULL.
 *
 * @return @ref CSM_MASSDATA_OK on success.
 *         @ref CSM_MASSDATA_ERR_PARSE if @p argument is malformed.
 *         @ref CSM_MASSDATA_ERR_OVERWRITTEN if the payload is no longer
 *              available in the cache.
 *         @ref CSM_MASSDATA_ERR_BUFFER_TOO_SMALL if @p data_cap is smaller
 *              than the stored payload (in which case @p data_size_out is
 *              still populated with the required size).
 */
csm_massdata_status_t CSM_ConvertArgumentToMassData(const char *argument,
                                                    void       *data,
                                                    size_t      data_cap,
                                                    size_t     *data_size_out);

/**
 * @brief Extract the data-type string from a MassData argument.
 *
 * Wraps `CSM - MassData Data Type String.vi`. The function does not consume
 * the argument: a verbatim copy is written to @p argument_dup so callers can
 * mimic the LabVIEW dataflow that returns a duplicate of its input.
 *
 * @param[in]  argument          NUL-terminated MassData argument string.
 * @param[out] argument_dup      Optional. If non-NULL, receives a copy of
 *                               @p argument.
 * @param[in]  argument_dup_cap  Capacity of @p argument_dup in bytes
 *                               (ignored when @p argument_dup is NULL).
 * @param[out] data_type         Receives the NUL-terminated data-type tag.
 *                               Empty string when no tag is present.
 * @param[in]  data_type_cap     Capacity of @p data_type in bytes.
 *
 * @return @ref CSM_MASSDATA_OK or an error code.
 */
csm_massdata_status_t CSM_MassDataDataTypeString(const char *argument,
                                                 char       *argument_dup,
                                                 size_t      argument_dup_cap,
                                                 char       *data_type,
                                                 size_t      data_type_cap);

/**
 * @brief Read the status of the MassData background cache.
 *
 * Wraps `CSM - MassData Parameter Status.vi`.
 *
 * @param[out] active_read   Receives the most recent read operation. May be
 *                           @c NULL if not needed.
 * @param[out] active_write  Receives the most recent write operation. May be
 *                           @c NULL if not needed.
 * @param[out] cache_size    Receives the configured cache size in bytes.
 *                           May be @c NULL if not needed.
 *
 * @return Always @ref CSM_MASSDATA_OK.
 */
csm_massdata_status_t CSM_MassDataParameterStatus(csm_massdata_operation_t *active_read,
                                                  csm_massdata_operation_t *active_write,
                                                  size_t                   *cache_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSM_MASSDATA_H */
