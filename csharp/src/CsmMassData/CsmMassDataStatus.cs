// <copyright file="CsmMassDataStatus.cs">
// MIT 许可证 —— 详见仓库根目录的 LICENSE 文件。
// </copyright>

namespace Csm.MassData;

/// <summary>
/// CSM MassData 接口在出错时返回的状态码。
/// </summary>
/// <remarks>
/// 数值与 C 语言移植版本（<c>csm_massdata_status_t</c>）以及
/// LabVIEW 端的错误约定保持一致，方便跨语言调试。
/// </remarks>
public enum CsmMassDataStatus
{
    /// <summary>操作成功完成。</summary>
    Ok = 0,

    /// <summary>参数为 <c>null</c> 或语义上无效。</summary>
    InvalidArgument = -1,

    /// <summary>调用方提供的输出缓冲区不足以容纳结果。</summary>
    BufferTooSmall = -2,

    /// <summary>MassData 参数字符串无法解析。</summary>
    ParseError = -3,

    /// <summary>引用的数据已被环形缓冲区后续写入覆盖，无法恢复。</summary>
    Overwritten = -4,

    /// <summary>待写入的数据大于当前配置的缓冲区容量。</summary>
    CacheTooSmall = -5,

    /// <summary>内存分配失败。</summary>
    NoMemory = -6,
}
