// <copyright file="CsmMassDataOperation.cs">
// MIT 许可证 —— 详见仓库根目录的 LICENSE 文件。
// </copyright>

namespace Csm.MassData;

/// <summary>
/// 描述一次对 MassData 环形缓冲区的读或写操作。
/// </summary>
/// <remarks>
/// 与 LabVIEW 端 <c>CSM - MassData Parameter Status.vi</c> 输出的
/// <c>Active Read Operation</c> / <c>Active Write Operation</c> 簇
/// 在字段命名与语义上完全一致。
/// </remarks>
public readonly struct CsmMassDataOperation : IEquatable<CsmMassDataOperation>
{
    /// <summary>
    /// 使用指定的起始游标与字节数构造一次操作描述。
    /// </summary>
    /// <param name="start">在缓冲区中的起始偏移量（字节）。</param>
    /// <param name="size">该次操作涉及的字节数。</param>
    public CsmMassDataOperation(ulong start, ulong size)
    {
        this.Start = start;
        this.Size = size;
    }

    /// <summary>获取该次操作在环形缓冲区中的起始偏移量（字节）。</summary>
    public ulong Start { get; }

    /// <summary>获取该次操作涉及的字节数。</summary>
    public ulong Size { get; }

    /// <inheritdoc/>
    public bool Equals(CsmMassDataOperation other)
        => this.Start == other.Start && this.Size == other.Size;

    /// <inheritdoc/>
    public override bool Equals(object? obj)
        => obj is CsmMassDataOperation other && this.Equals(other);

    /// <inheritdoc/>
    public override int GetHashCode() => HashCode.Combine(this.Start, this.Size);

    /// <summary>判断两次操作描述是否完全相同。</summary>
    public static bool operator ==(CsmMassDataOperation left, CsmMassDataOperation right)
        => left.Equals(right);

    /// <summary>判断两次操作描述是否不同。</summary>
    public static bool operator !=(CsmMassDataOperation left, CsmMassDataOperation right)
        => !left.Equals(right);
}
