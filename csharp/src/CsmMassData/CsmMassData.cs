// <copyright file="CsmMassData.cs">
// MIT 许可证 —— 详见仓库根目录的 LICENSE 文件。
// </copyright>

using System.Globalization;

namespace Csm.MassData;

/// <summary>
/// CSM MassData Parameter Support 插件的 C# 移植版本。
/// </summary>
/// <remarks>
/// <para>
/// 本类公开的方法与 LabVIEW 端
/// <c>addons/MassData-Parameter/CSM MassData Parameter Support.lvlib</c>
/// 中的 VI 在功能与命名上完全一致；同时与 <c>c/include/csm_massdata.h</c>
/// 中暴露的 C API 保持等价语义，便于跨语言互通：使用 C# 编写的代码与
/// 使用 LabVIEW / C 编写的代码可以无缝传递 MassData 参数字符串。
/// </para>
///
/// <para>
/// <b>MassData 参数格式：</b>MassData 参数是一段仅包含 ASCII 字符、
/// 可读的引用字符串，指向进程内一个全局环形缓冲区中的实际数据。
/// 支持以下两种形式：
/// </para>
/// <list type="bullet">
///   <item><description>不带数据类型：<c>&lt;MassData&gt;Start:&lt;N&gt;;Size:&lt;N&gt;</c></description></item>
///   <item><description>带数据类型：  <c>&lt;MassData&gt;Start:&lt;N&gt;;Size:&lt;N&gt;;DataType:&lt;T&gt;</c></description></item>
/// </list>
/// <para>
/// 其中 <c>&lt;N&gt;</c> 为非负十进制整数，<c>&lt;T&gt;</c> 为自由格式的
/// 数据类型标签（例如 <c>1D I32</c>、<c>Waveform</c> 等）。
/// </para>
///
/// <para>
/// <b>数据生命周期：</b>MassData 内部使用环形缓冲区。当缓冲区写满后，
/// 新写入的数据将从缓冲区起始位置覆盖最早的数据。被覆盖的数据
/// 无法恢复，后续对其引用进行解码时会抛出
/// <see cref="CsmMassDataException"/>（<see cref="CsmMassDataStatus.Overwritten"/>）。
/// 同一 AppDomain 内的所有调用者共享同一份 MassData 缓冲区。
/// </para>
///
/// <para>
/// <b>线程安全：</b>所有公开方法都是线程安全的。内部状态由静态构造函数
/// 完成一次性初始化（CLR 保证其只执行一次且对其它线程可见），后续访问
/// 通过同一个 <c>lock</c> 对象串行化。建议在进入多线程阶段前主动调用
/// <see cref="ConfigMassDataParameterCacheSize(int)"/> 完成初始化与
/// 缓冲区配置，以获得最佳性能。
/// </para>
/// </remarks>
public static class CsmMassData
{
    /// <summary>MassData 缓冲区的默认大小（字节），与 LabVIEW VI 一致：50 MiB。</summary>
    public const int DefaultCacheSize = 50 * 1024 * 1024;

    /// <summary>编码方法返回的 MassData 参数字符串的最大长度（字符数）。</summary>
    public const int MaxArgumentLength = 256;

    /// <summary>数据类型标签字符串的最大长度（字符数）。</summary>
    public const int MaxDataTypeLength = 128;

    private const string Prefix = "<MassData>";

    private static readonly object SyncRoot = new();

    private static byte[] s_buffer = AllocateInitialBuffer();
    private static int s_capacity = s_buffer.Length;
    private static ulong s_writeTotal;
    private static CsmMassDataOperation s_lastRead;
    private static CsmMassDataOperation s_lastWrite;

    /// <summary>
    /// 配置 MassData 后台缓冲区大小。
    /// </summary>
    /// <remarks>
    /// 对应 <c>CSM - Config MassData Parameter Cache Size.vi</c> 与
    /// <c>CSM_ConfigMassDataParameterCacheSize</c>。
    /// 与 LabVIEW VI 一致，调用本方法会丢弃当前缓存中的所有数据；
    /// 通常应在任何编码 / 解码调用之前、应用启动阶段调用一次。
    /// </remarks>
    /// <param name="size">新的缓冲区大小（字节），必须为正整数。</param>
    /// <exception cref="CsmMassDataException">
    /// 当 <paramref name="size"/> 不为正数（<see cref="CsmMassDataStatus.InvalidArgument"/>）
    /// 或内存分配失败（<see cref="CsmMassDataStatus.NoMemory"/>）时抛出。
    /// </exception>
    public static void ConfigMassDataParameterCacheSize(int size)
    {
        if (size <= 0)
        {
            throw new CsmMassDataException(
                CsmMassDataStatus.InvalidArgument,
                $"缓冲区大小必须为正数，但收到 {size}。");
        }

        byte[] newBuffer;
        try
        {
            newBuffer = new byte[size];
        }
        catch (OutOfMemoryException ex)
        {
            throw new CsmMassDataException(
                CsmMassDataStatus.NoMemory,
                $"无法分配 {size} 字节的 MassData 缓冲区。",
                ex);
        }

        lock (SyncRoot)
        {
            s_buffer = newBuffer;
            s_capacity = size;
            s_writeTotal = 0u;
            s_lastRead = default;
            s_lastWrite = default;
        }
    }

    /// <summary>
    /// 将原始数据转换为 MassData 参数（不嵌入数据类型）。
    /// </summary>
    /// <remarks>
    /// 对应 <c>CSM - Convert MassData to Argument.vim</c> 与
    /// <c>CSM_ConvertMassDataToArgument</c>。原始数据被复制到环形缓冲区，
    /// 并返回形如 <c>&lt;MassData&gt;Start:&lt;N&gt;;Size:&lt;N&gt;</c>
    /// 的引用字符串。
    /// </remarks>
    /// <param name="data">待保存的原始字节，<c>null</c> 视为空数据。</param>
    /// <returns>MassData 参数字符串。</returns>
    /// <exception cref="CsmMassDataException">编码过程中检测到错误时抛出。</exception>
    public static string ConvertMassDataToArgument(ReadOnlySpan<byte> data)
        => Encode(data, dataType: null);

    /// <summary>
    /// 将原始数据转换为 MassData 参数（不嵌入数据类型）。
    /// </summary>
    /// <param name="data">待保存的原始字节。<c>null</c> 视为空数据。</param>
    /// <returns>MassData 参数字符串。</returns>
    /// <inheritdoc cref="ConvertMassDataToArgument(ReadOnlySpan{byte})"/>
    public static string ConvertMassDataToArgument(byte[]? data)
        => Encode(data is null ? ReadOnlySpan<byte>.Empty : data.AsSpan(), dataType: null);

    /// <summary>
    /// 将原始数据转换为带数据类型标签的 MassData 参数。
    /// </summary>
    /// <remarks>
    /// 对应 <c>CSM - Convert MassData to Argument With DataType.vim</c> 与
    /// <c>CSM_ConvertMassDataToArgumentWithDataType</c>，生成的参数形如
    /// <c>&lt;MassData&gt;Start:&lt;N&gt;;Size:&lt;N&gt;;DataType:&lt;dataType&gt;</c>。
    /// </remarks>
    /// <param name="data">待保存的原始字节。</param>
    /// <param name="dataType">
    /// 非空的数据类型字符串（例如 <c>"1D I32"</c>）；不允许包含
    /// <c>';'</c>、<c>'&lt;'</c> 或 <c>'&gt;'</c>。
    /// </param>
    /// <returns>MassData 参数字符串。</returns>
    /// <exception cref="CsmMassDataException">参数非法或编码失败时抛出。</exception>
    public static string ConvertMassDataToArgumentWithDataType(ReadOnlySpan<byte> data, string dataType)
    {
        if (dataType is null)
        {
            throw new CsmMassDataException(
                CsmMassDataStatus.InvalidArgument,
                "dataType 不能为 null。");
        }

        return Encode(data, dataType);
    }

    /// <summary>
    /// 将原始数据转换为带数据类型标签的 MassData 参数。
    /// </summary>
    /// <param name="data">待保存的原始字节。<c>null</c> 视为空数据。</param>
    /// <param name="dataType">非空的数据类型字符串。</param>
    /// <returns>MassData 参数字符串。</returns>
    /// <inheritdoc cref="ConvertMassDataToArgumentWithDataType(ReadOnlySpan{byte}, string)"/>
    public static string ConvertMassDataToArgumentWithDataType(byte[]? data, string dataType)
        => ConvertMassDataToArgumentWithDataType(
            data is null ? ReadOnlySpan<byte>.Empty : data.AsSpan(),
            dataType);

    /// <summary>
    /// 将 MassData 参数还原为原始数据。
    /// </summary>
    /// <remarks>
    /// 对应 <c>CSM - Convert Argument to MassData.vim</c> 与
    /// <c>CSM_ConvertArgumentToMassData</c>。返回的就是此前写入的原始字节，
    /// 不受嵌入的数据类型标签影响（如需读取标签请使用
    /// <see cref="MassDataDataTypeString(string)"/>）。
    /// </remarks>
    /// <param name="argument">以前由 <c>ConvertMassDataToArgument*</c> 返回的引用字符串。</param>
    /// <returns>解析得到的原始字节，长度恰好为参数中 <c>Size</c> 字段的值。</returns>
    /// <exception cref="CsmMassDataException">
    /// 参数非法（<see cref="CsmMassDataStatus.ParseError"/> /
    /// <see cref="CsmMassDataStatus.InvalidArgument"/>）或数据已被覆盖
    /// （<see cref="CsmMassDataStatus.Overwritten"/>）时抛出。
    /// </exception>
    public static byte[] ConvertArgumentToMassData(string argument)
    {
        Parse(argument, out ulong start, out ulong size, dataType: out _, parseDataType: false);

        // 在 64 位 CLR 上，数组最大长度仍受 int.MaxValue 约束。
        if (size > (ulong)Array.MaxLength)
        {
            throw new CsmMassDataException(
                CsmMassDataStatus.ParseError,
                $"参数 Size 字段为 {size}，超出当前运行时支持的最大数组长度。");
        }

        byte[] result = new byte[(int)size];

        lock (SyncRoot)
        {
            CheckResidencyAndRead(start, size, result);
            s_lastRead = new CsmMassDataOperation(start, size);
        }

        return result;
    }

    /// <summary>
    /// 从 MassData 参数中解析出数据类型字符串。
    /// </summary>
    /// <remarks>
    /// 对应 <c>CSM - MassData Data Type String.vi</c> 与
    /// <c>CSM_MassDataDataTypeString</c>。本方法不会消费输入，也不会
    /// 触碰环形缓冲区中的数据。
    /// </remarks>
    /// <param name="argument">MassData 参数字符串。</param>
    /// <returns>数据类型标签；若参数中未携带类型字段则返回空字符串。</returns>
    /// <exception cref="CsmMassDataException">参数非法时抛出。</exception>
    public static string MassDataDataTypeString(string argument)
    {
        Parse(argument, out _, out _, out string? dataType, parseDataType: true);
        return dataType ?? string.Empty;
    }

    /// <summary>
    /// 从 MassData 参数中解析出数据类型字符串，并返回输入字符串的副本。
    /// </summary>
    /// <remarks>
    /// 对应 LabVIEW VI 中将 <c>argument</c> 同时透传到下游连线的数据流行为。
    /// </remarks>
    /// <param name="argument">MassData 参数字符串。</param>
    /// <param name="argumentDup">输出的输入副本，与 <paramref name="argument"/> 内容相同。</param>
    /// <returns>数据类型标签；若参数中未携带类型字段则返回空字符串。</returns>
    /// <exception cref="CsmMassDataException">参数非法时抛出。</exception>
    public static string MassDataDataTypeString(string argument, out string argumentDup)
    {
        string result = MassDataDataTypeString(argument);
        argumentDup = argument;
        return result;
    }

    /// <summary>
    /// 读取 MassData 后台缓冲区的状态信息。
    /// </summary>
    /// <remarks>对应 <c>CSM - MassData Parameter Status.vi</c>。</remarks>
    /// <param name="activeRead">输出最近一次的读操作信息。</param>
    /// <param name="activeWrite">输出最近一次的写操作信息。</param>
    /// <param name="cacheSize">输出当前配置的缓冲区大小（字节）。</param>
    public static void MassDataParameterStatus(
        out CsmMassDataOperation activeRead,
        out CsmMassDataOperation activeWrite,
        out int cacheSize)
    {
        lock (SyncRoot)
        {
            activeRead = s_lastRead;
            activeWrite = s_lastWrite;
            cacheSize = s_capacity;
        }
    }

    private static byte[] AllocateInitialBuffer()
    {
        // 静态字段初始化由 CLR 保证仅执行一次且线程安全；首个失败的尝试
        // 不应阻止后续 ConfigMassDataParameterCacheSize 重新分配。
        try
        {
            return new byte[DefaultCacheSize];
        }
        catch (OutOfMemoryException)
        {
            return Array.Empty<byte>();
        }
    }

    private static string Encode(ReadOnlySpan<byte> data, string? dataType)
    {
        if (dataType is not null)
        {
            // 数据类型不能包含会破坏引用字符串语法的字符。
            if (dataType.IndexOfAny(new[] { ';', '<', '>' }) >= 0)
            {
                throw new CsmMassDataException(
                    CsmMassDataStatus.InvalidArgument,
                    "dataType 中不允许出现 ';'、'<' 或 '>' 字符。");
            }

            // 与 C 端及 LabVIEW 端保持一致的最大长度（含末尾 NUL）。
            if (dataType.Length + 1 > MaxDataTypeLength)
            {
                throw new CsmMassDataException(
                    CsmMassDataStatus.BufferTooSmall,
                    $"dataType 长度 {dataType.Length} 超过 {MaxDataTypeLength - 1}。");
            }
        }

        ulong startCursor;
        int dataSize = data.Length;

        lock (SyncRoot)
        {
            if (s_buffer.Length == 0 || s_capacity == 0)
            {
                throw new CsmMassDataException(
                    CsmMassDataStatus.NoMemory,
                    "MassData 缓冲区尚未分配，请先调用 ConfigMassDataParameterCacheSize。");
            }

            if (dataSize > s_capacity)
            {
                throw new CsmMassDataException(
                    CsmMassDataStatus.CacheTooSmall,
                    $"待写入的数据 {dataSize} 字节超过缓冲区容量 {s_capacity}。");
            }

            startCursor = s_writeTotal;
            if (dataSize > 0)
            {
                RingWrite(data);
                s_writeTotal += (ulong)dataSize;
            }

            s_lastWrite = new CsmMassDataOperation(startCursor, (ulong)dataSize);
        }

        string argument = dataType is null
            ? string.Format(
                CultureInfo.InvariantCulture,
                "{0}Start:{1};Size:{2}",
                Prefix,
                startCursor,
                (ulong)dataSize)
            : string.Format(
                CultureInfo.InvariantCulture,
                "{0}Start:{1};Size:{2};DataType:{3}",
                Prefix,
                startCursor,
                (ulong)dataSize,
                dataType);

        if (argument.Length + 1 > MaxArgumentLength)
        {
            throw new CsmMassDataException(
                CsmMassDataStatus.BufferTooSmall,
                $"生成的参数长度 {argument.Length} 超过 {MaxArgumentLength - 1}。");
        }

        return argument;
    }

    private static void Parse(
        string argument,
        out ulong start,
        out ulong size,
        out string? dataType,
        bool parseDataType)
    {
        start = 0u;
        size = 0u;
        dataType = parseDataType ? string.Empty : null;

        if (argument is null)
        {
            throw new CsmMassDataException(
                CsmMassDataStatus.InvalidArgument,
                "argument 不能为 null。");
        }

        if (!argument.StartsWith(Prefix, StringComparison.Ordinal))
        {
            ThrowParse(argument);
        }

        int pos = Prefix.Length;

        if (!TryConsume(argument, ref pos, "Start:")
            || !TryParseUInt64(argument, ref pos, out start)
            || pos >= argument.Length || argument[pos] != ';')
        {
            ThrowParse(argument);
        }

        pos++; // 跳过 ';'

        if (!TryConsume(argument, ref pos, "Size:")
            || !TryParseUInt64(argument, ref pos, out size))
        {
            ThrowParse(argument);
        }

        // 可选的 ;DataType:<T> 后缀。
        if (pos == argument.Length)
        {
            return;
        }

        if (argument[pos] != ';')
        {
            ThrowParse(argument);
        }

        pos++;
        if (!TryConsume(argument, ref pos, "DataType:"))
        {
            ThrowParse(argument);
        }

        // 验证 DataType 值仅包含合法字符（不能含 ';' / '<' / '>'）。
        for (int i = pos; i < argument.Length; i++)
        {
            char c = argument[i];
            if (c == ';' || c == '<' || c == '>')
            {
                ThrowParse(argument);
            }
        }

        if (parseDataType)
        {
            dataType = argument.Substring(pos);
        }
    }

    private static bool TryConsume(string s, ref int pos, string token)
    {
        if (pos + token.Length > s.Length)
        {
            return false;
        }

        for (int i = 0; i < token.Length; i++)
        {
            if (s[pos + i] != token[i])
            {
                return false;
            }
        }

        pos += token.Length;
        return true;
    }

    private static bool TryParseUInt64(string s, ref int pos, out ulong value)
    {
        value = 0u;
        int start = pos;
        while (pos < s.Length && s[pos] >= '0' && s[pos] <= '9')
        {
            pos++;
        }

        if (pos == start)
        {
            return false;
        }

        // 使用 InvariantCulture，且检查溢出，与 C 端 strtoull + errno 检查等价。
        return ulong.TryParse(
            s.AsSpan(start, pos - start),
            NumberStyles.None,
            CultureInfo.InvariantCulture,
            out value);
    }

    private static void RingWrite(ReadOnlySpan<byte> src)
    {
        int offset = (int)(s_writeTotal % (ulong)s_capacity);
        int firstRun = s_capacity - offset;
        if (src.Length <= firstRun)
        {
            src.CopyTo(s_buffer.AsSpan(offset));
        }
        else
        {
            src.Slice(0, firstRun).CopyTo(s_buffer.AsSpan(offset));
            src.Slice(firstRun).CopyTo(s_buffer.AsSpan(0));
        }
    }

    private static void RingRead(ulong start, Span<byte> dst)
    {
        int offset = (int)(start % (ulong)s_capacity);
        int firstRun = s_capacity - offset;
        if (dst.Length <= firstRun)
        {
            s_buffer.AsSpan(offset, dst.Length).CopyTo(dst);
        }
        else
        {
            s_buffer.AsSpan(offset, firstRun).CopyTo(dst);
            s_buffer.AsSpan(0, dst.Length - firstRun).CopyTo(dst.Slice(firstRun));
        }
    }

    private static void CheckResidencyAndRead(ulong start, ulong size, byte[] dst)
    {
        if (s_buffer.Length == 0 || s_capacity == 0)
        {
            throw new CsmMassDataException(
                CsmMassDataStatus.NoMemory,
                "MassData 缓冲区尚未分配，请先调用 ConfigMassDataParameterCacheSize。");
        }

        if (size > (ulong)s_capacity)
        {
            throw new CsmMassDataException(
                CsmMassDataStatus.Overwritten,
                $"请求的 {size} 字节超过当前缓冲区容量 {s_capacity}，对应数据已不可恢复。");
        }

        // 当前驻留窗口为 [writeTotal - capacity, writeTotal)；任何末端落在
        // 该窗口之外的请求都视为已被覆盖。
        ulong oldest = s_writeTotal > (ulong)s_capacity
            ? s_writeTotal - (ulong)s_capacity
            : 0u;

        // 在加法前先检测 ulong 溢出。
        if (size > 0u && start > ulong.MaxValue - size)
        {
            throw new CsmMassDataException(
                CsmMassDataStatus.Overwritten,
                "Start + Size 在 64 位空间内溢出，引用已无效。");
        }

        if (start < oldest || start > s_writeTotal)
        {
            throw new CsmMassDataException(
                CsmMassDataStatus.Overwritten,
                "引用的数据已被环形缓冲区后续写入覆盖。");
        }

        ulong end = start + size;
        if (end > s_writeTotal)
        {
            throw new CsmMassDataException(
                CsmMassDataStatus.Overwritten,
                "引用的数据末端尚未写入或已被覆盖。");
        }

        if (size > 0u)
        {
            RingRead(start, dst.AsSpan(0, (int)size));
        }
    }

    private static void ThrowParse(string argument)
    {
        // 截断过长的原始输入，避免异常消息无限制膨胀。
        const int maxEcho = 64;
        string echo = argument.Length > maxEcho
            ? argument.Substring(0, maxEcho) + "..."
            : argument;
        throw new CsmMassDataException(
            CsmMassDataStatus.ParseError,
            $"无法将字符串解析为 MassData 参数：\"{echo}\"。");
    }
}
