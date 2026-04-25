/**
 * @file    csm_massdata.h
 * @brief   CSM MassData Parameter Support 插件的 C 语言移植版本。
 *
 * 本头文件公开的 C 接口与 LabVIEW 端
 * `addons/MassData-Parameter/CSM MassData Parameter Support.lvlib`
 * 中的 VI 在功能与命名上完全一致。
 *
 * 函数名称、参数顺序与语义均与对应的 LabVIEW VI 严格保持一致，
 * 因此使用 C 与 LabVIEW 两种语言编写的代码可以无需任何转换层
 * 直接互通 MassData 参数。
 *
 * @par MassData 参数格式
 * MassData 参数是一段仅包含 ASCII 字符、可读的引用字符串，指向
 * 进程内一个全局环形缓冲区中的实际数据。支持以下两种形式：
 *
 *   - 不带数据类型： `<MassData>Start:<N>;Size:<N>`
 *   - 带   数据类型： `<MassData>Start:<N>;Size:<N>;DataType:<T>`
 *
 * 其中 `<N>` 为非负十进制整数，`<T>` 为自由格式的数据类型标签
 * （例如 `1D I32`、`Waveform` 等），由 CSM Data Type String VI 定义。
 *
 * @par 数据生命周期
 * MassData 内部使用环形缓冲区。当缓冲区写满后，新写入的数据将
 * 从缓冲区起始位置覆盖最早的数据。被覆盖的数据无法恢复，
 * 后续对其引用进行解码时会返回错误。同一进程内的所有调用者
 * 共享同一份 MassData 缓冲区。
 *
 * @par 线程安全
 * 在内部状态完成初始化后，公开函数的并发调用会通过内部互斥量
 * 串行化执行。延迟初始化通过平台原语（Windows: @c InitOnceExecuteOnce，
 * POSIX: @c pthread_once）保证仅执行一次，即使多个线程并发首次调用
 * 也不会产生竞态。建议在进入多线程阶段前主动调用
 * CSM_ConfigMassDataParameterCacheSize() 完成初始化，以获得最佳性能。
 *
 * @copyright MIT 许可证 — 详见仓库根目录的 LICENSE 文件。
 */

#ifndef CSM_MASSDATA_H
#define CSM_MASSDATA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/*  常量与类型定义                                                           */
/* ------------------------------------------------------------------------- */

/** MassData 缓冲区的默认大小（字节），与 LabVIEW VI 保持一致：50 MiB。 */
#define CSM_MASSDATA_DEFAULT_CACHE_SIZE  ((size_t)(50u * 1024u * 1024u))

/** 编码函数返回的 MassData 参数字符串的最大长度（包含末尾 NUL）。 */
#define CSM_MASSDATA_MAX_ARGUMENT_LEN    256

/** 数据类型标签字符串的最大长度（包含末尾 NUL）。 */
#define CSM_MASSDATA_MAX_DATATYPE_LEN    128

/**
 * @brief MassData API 所有函数返回的状态码。
 */
typedef enum csm_massdata_status_e {
    CSM_MASSDATA_OK                   =  0, /**< 操作成功完成。                     */
    CSM_MASSDATA_ERR_INVALID_ARG      = -1, /**< 参数为 NULL 或无效。               */
    CSM_MASSDATA_ERR_BUFFER_TOO_SMALL = -2, /**< 调用方提供的输出缓冲区不足。       */
    CSM_MASSDATA_ERR_PARSE            = -3, /**< MassData 参数字符串无法解析。      */
    CSM_MASSDATA_ERR_OVERWRITTEN      = -4, /**< 引用的数据已被环形缓冲区覆盖。     */
    CSM_MASSDATA_ERR_CACHE_TOO_SMALL  = -5, /**< 待写入的数据大于缓冲区容量。       */
    CSM_MASSDATA_ERR_NO_MEMORY        = -6  /**< 内存分配失败。                     */
} csm_massdata_status_t;

/**
 * @brief 描述最近一次对 MassData 环形缓冲区的读或写操作。
 *
 * 等价于 `CSM - MassData Parameter Status.vi` 返回的
 * @c Active&nbsp;Read&nbsp;Operation / @c Active&nbsp;Write&nbsp;Operation 簇。
 */
typedef struct csm_massdata_operation_s {
    uint64_t start;  /**< 在缓冲区中的起始偏移量（字节）。 */
    uint64_t size;   /**< 该次操作的字节数。               */
} csm_massdata_operation_t;

/* ------------------------------------------------------------------------- */
/*  API 函数 —— 每个函数对应一个同名的 LabVIEW VI                            */
/* ------------------------------------------------------------------------- */

/**
 * @brief 配置 MassData 后台缓冲区大小。
 *
 * 对应 `CSM - Config MassData Parameter Cache Size.vi`。
 *
 * 将内部环形缓冲区重新分配为 @p size 字节。与 LabVIEW VI 一致，
 * 在程序运行过程中调用本函数会丢弃当前已缓存的数据；通常应在
 * 任何编码 / 解码调用之前、应用启动阶段调用一次。
 *
 * @param[in] size  新的缓冲区大小（字节）。若从未调用过本函数，
 *                  则默认值为 @ref CSM_MASSDATA_DEFAULT_CACHE_SIZE。
 *
 * @return 成功返回 @ref CSM_MASSDATA_OK；
 *         若 @p size 为 0 返回 @ref CSM_MASSDATA_ERR_INVALID_ARG；
 *         若分配失败返回 @ref CSM_MASSDATA_ERR_NO_MEMORY。
 */
csm_massdata_status_t CSM_ConfigMassDataParameterCacheSize(size_t size);

/**
 * @brief 将原始数据转换为 MassData 参数（不嵌入数据类型）。
 *
 * 对应 `CSM - Convert MassData to Argument.vim`。原始数据被复制到
 * 环形缓冲区，并向 @p argument 写入形如
 * `<MassData>Start:<N>;Size:<N>` 的引用字符串。
 *
 * @param[in]  data         指向待保存的原始字节。仅当 @p data_size
 *                          为 0 时允许为 @c NULL。
 * @param[in]  data_size    @p data 的字节长度。
 * @param[out] argument     调用方分配的输出缓冲区，用于接收以 NUL 结尾的
 *                          MassData 参数字符串。
 * @param[in]  argument_cap @p argument 的容量（字节），建议不小于
 *                          @ref CSM_MASSDATA_MAX_ARGUMENT_LEN。
 *
 * @return @ref CSM_MASSDATA_OK 或对应的错误码。
 */
csm_massdata_status_t CSM_ConvertMassDataToArgument(const void *data,
                                                    size_t      data_size,
                                                    char       *argument,
                                                    size_t      argument_cap);

/**
 * @brief 将原始数据转换为带数据类型标签的 MassData 参数。
 *
 * 对应 `CSM - Convert MassData to Argument With DataType.vim`。
 * 生成的参数形如 `<MassData>Start:<N>;Size:<N>;DataType:<data_type>`。
 *
 * @param[in]  data         指向待保存的原始字节。
 * @param[in]  data_size    @p data 的字节长度。
 * @param[in]  data_type    以 NUL 结尾的数据类型字符串（例如 `"1D I32"`）。
 *                          字符串中不允许出现 `';'` 或 `'<'` / `'>'`。
 * @param[out] argument     调用方分配的输出缓冲区，用于接收结果。
 * @param[in]  argument_cap @p argument 的容量（字节）。
 *
 * @return @ref CSM_MASSDATA_OK 或对应的错误码。
 */
csm_massdata_status_t CSM_ConvertMassDataToArgumentWithDataType(const void *data,
                                                                size_t      data_size,
                                                                const char *data_type,
                                                                char       *argument,
                                                                size_t      argument_cap);

/**
 * @brief 将 MassData 参数还原为原始数据。
 *
 * 对应 `CSM - Convert Argument to MassData.vim`。本函数解析
 * @p argument 中的引用字符串，并将对应的数据复制到 @p data。
 * LabVIEW VI 中可选的 `Type` 输入在此处刻意省略：返回的就是
 * 此前写入的原始字节，不受嵌入的类型标签影响。
 *
 * @param[in]  argument        以 NUL 结尾的 MassData 参数字符串。
 * @param[out] data            调用方分配的接收缓冲区。
 * @param[in]  data_cap        @p data 的容量（字节）。
 * @param[out] data_size_out   返回实际写入 @p data 的字节数，
 *                             不允许为 @c NULL。
 *
 * @return 成功返回 @ref CSM_MASSDATA_OK；
 *         @p argument 不合法时返回 @ref CSM_MASSDATA_ERR_PARSE；
 *         缓存中的数据已被覆盖时返回 @ref CSM_MASSDATA_ERR_OVERWRITTEN；
 *         @p data_cap 小于实际数据大小时返回
 *         @ref CSM_MASSDATA_ERR_BUFFER_TOO_SMALL（此时
 *         @p data_size_out 仍会写入所需的字节数）。
 */
csm_massdata_status_t CSM_ConvertArgumentToMassData(const char *argument,
                                                    void       *data,
                                                    size_t      data_cap,
                                                    size_t     *data_size_out);

/**
 * @brief 从 MassData 参数中解析出数据类型字符串。
 *
 * 对应 `CSM - MassData Data Type String.vi`。本函数不会消费输入：
 * 当 @p argument_dup 非空时，会写入 @p argument 的副本，以模仿
 * LabVIEW VI 中“返回输入副本”的数据流行为。
 *
 * @param[in]  argument          以 NUL 结尾的 MassData 参数字符串。
 * @param[out] argument_dup      可选。若非空，则接收 @p argument 的副本。
 * @param[in]  argument_dup_cap  @p argument_dup 的容量（当 @p argument_dup
 *                               为空时忽略）。
 * @param[out] data_type         接收以 NUL 结尾的数据类型标签；
 *                               若不存在则为空字符串。
 * @param[in]  data_type_cap     @p data_type 的容量（字节）。
 *
 * @return @ref CSM_MASSDATA_OK 或对应的错误码。
 */
csm_massdata_status_t CSM_MassDataDataTypeString(const char *argument,
                                                 char       *argument_dup,
                                                 size_t      argument_dup_cap,
                                                 char       *data_type,
                                                 size_t      data_type_cap);

/**
 * @brief 读取 MassData 后台缓冲区的状态信息。
 *
 * 对应 `CSM - MassData Parameter Status.vi`。
 *
 * @param[out] active_read   接收最近一次的读操作信息，可为 @c NULL。
 * @param[out] active_write  接收最近一次的写操作信息，可为 @c NULL。
 * @param[out] cache_size    接收当前配置的缓冲区大小（字节），可为 @c NULL。
 *
 * @return 始终返回 @ref CSM_MASSDATA_OK。
 */
csm_massdata_status_t CSM_MassDataParameterStatus(csm_massdata_operation_t *active_read,
                                                  csm_massdata_operation_t *active_write,
                                                  size_t                   *cache_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSM_MASSDATA_H */
