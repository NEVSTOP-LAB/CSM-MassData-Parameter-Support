// <copyright file="CsmMassDataException.cs">
// MIT 许可证 —— 详见仓库根目录的 LICENSE 文件。
// </copyright>

namespace Csm.MassData;

/// <summary>
/// 当 <see cref="CsmMassData"/> 中的任意 API 检测到错误时抛出。
/// </summary>
/// <remarks>
/// <see cref="Status"/> 属性给出了与 C 语言移植版本对齐的错误码，
/// 便于在异常处理逻辑中以与 LabVIEW / C 一致的语义进行分支。
/// </remarks>
public sealed class CsmMassDataException : Exception
{
    /// <summary>
    /// 使用指定的错误状态与人类可读的描述构造异常。
    /// </summary>
    /// <param name="status">触发异常的错误状态。</param>
    /// <param name="message">面向开发者的错误描述。</param>
    public CsmMassDataException(CsmMassDataStatus status, string message)
        : base(message)
    {
        this.Status = status;
    }

    /// <summary>
    /// 使用指定的错误状态、描述与内层异常构造异常。
    /// </summary>
    /// <param name="status">触发异常的错误状态。</param>
    /// <param name="message">面向开发者的错误描述。</param>
    /// <param name="innerException">引发本异常的底层异常。</param>
    public CsmMassDataException(CsmMassDataStatus status, string message, Exception innerException)
        : base(message, innerException)
    {
        this.Status = status;
    }

    /// <summary>
    /// 获取与 C / LabVIEW 端语义一致的错误状态码。
    /// </summary>
    public CsmMassDataStatus Status { get; }
}
