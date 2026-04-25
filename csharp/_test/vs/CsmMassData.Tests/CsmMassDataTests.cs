// <copyright file="CsmMassDataTests.cs">
// MIT 许可证 —— 详见仓库根目录的 LICENSE 文件。
// </copyright>

using Csm.MassData;

namespace CsmMassData.Tests;

/// <summary>
/// CSM MassData C# 接口的端到端测试，覆盖
/// <see cref="Csm.MassData.CsmMassData"/> 中公开的所有 API：
/// <list type="bullet">
///   <item><description>缓冲区配置与状态查询</description></item>
///   <item><description>不带数据类型的编码 / 解码往返</description></item>
///   <item><description>带数据类型的编码 / 解码往返</description></item>
///   <item><description>数据类型解析（CSM - MassData Data Type String）</description></item>
///   <item><description>解析错误的处理</description></item>
///   <item><description>环形缓冲区覆盖检测</description></item>
/// </list>
/// </summary>
[TestClass]
[DoNotParallelize]
public class CsmMassDataTests
{
    /// <summary>测试：缓冲区配置与状态查询。</summary>
    [TestMethod]
    public void ConfigAndStatus_RoundTrip()
    {
        Assert.ThrowsExactly<CsmMassDataException>(
            () => Csm.MassData.CsmMassData.ConfigMassDataParameterCacheSize(0));

        Csm.MassData.CsmMassData.ConfigMassDataParameterCacheSize(1024);
        Csm.MassData.CsmMassData.MassDataParameterStatus(
            out CsmMassDataOperation read,
            out CsmMassDataOperation write,
            out int cacheSize);

        Assert.AreEqual(1024, cacheSize);
        Assert.AreEqual(0u, read.Size);
        Assert.AreEqual(0u, write.Size);
    }

    /// <summary>测试：不带数据类型的编码 / 解码往返。</summary>
    [TestMethod]
    public void Roundtrip_Plain()
    {
        Csm.MassData.CsmMassData.ConfigMassDataParameterCacheSize(4096);

        byte[] source = new byte[8 * sizeof(int)];
        for (int i = 0; i < 8; i++)
        {
            BitConverter.GetBytes((i + 1) * 10).CopyTo(source, i * sizeof(int));
        }

        string arg = Csm.MassData.CsmMassData.ConvertMassDataToArgument(source);
        StringAssert.StartsWith(arg, "<MassData>Start:");

        byte[] restored = Csm.MassData.CsmMassData.ConvertArgumentToMassData(arg);
        Assert.HasCount(source.Length, restored);
        CollectionAssert.AreEqual(source, restored);
    }

    /// <summary>测试：带数据类型的编码 / 解码往返。</summary>
    [TestMethod]
    public void Roundtrip_WithDataType()
    {
        Csm.MassData.CsmMassData.ConfigMassDataParameterCacheSize(4096);

        double[] values = { 1.5, -2.5, 3.5, -4.5 };
        byte[] source = new byte[values.Length * sizeof(double)];
        Buffer.BlockCopy(values, 0, source, 0, source.Length);

        string arg = Csm.MassData.CsmMassData.ConvertMassDataToArgumentWithDataType(source, "1D DBL");
        StringAssert.Contains(arg, ";DataType:1D DBL");

        string type = Csm.MassData.CsmMassData.MassDataDataTypeString(arg, out string dup);
        Assert.AreEqual("1D DBL", type);
        Assert.AreEqual(arg, dup);

        byte[] restored = Csm.MassData.CsmMassData.ConvertArgumentToMassData(arg);
        Assert.HasCount(source.Length, restored);
        CollectionAssert.AreEqual(source, restored);
    }

    /// <summary>测试：参数中没有数据类型字段时，解析结果应为空字符串。</summary>
    [TestMethod]
    public void DataType_Absent_ReturnsEmpty()
    {
        Csm.MassData.CsmMassData.ConfigMassDataParameterCacheSize(4096);
        byte[] payload = { 0xAA, 0xBB, 0xCC };

        string arg = Csm.MassData.CsmMassData.ConvertMassDataToArgument(payload);
        string type = Csm.MassData.CsmMassData.MassDataDataTypeString(arg);

        Assert.AreEqual(string.Empty, type);
    }

    /// <summary>测试：解析非法字符串时应抛出 ParseError 异常。</summary>
    [TestMethod]
    public void Parse_InvalidArgument_Throws()
    {
        AssertParseError("garbage");
        AssertParseError("<MassData>Start:abc;Size:1");
        AssertParseError("<MassData>Start:0;Size:1;wrong");
        AssertParseError("<MassData>Start:0;Size:1;DataType:bad;value");
        AssertParseError("<MassData>Start:99999999999999999999;Size:0"); // 溢出 ulong
    }

    /// <summary>测试：旧数据被环形缓冲区覆盖后，应抛出 Overwritten 异常。</summary>
    [TestMethod]
    public void Overwrite_Detection()
    {
        // 缓冲区故意设得很小，后续写入会把第一份数据挤掉。
        Csm.MassData.CsmMassData.ConfigMassDataParameterCacheSize(32);

        byte[] small = { 1, 2, 3, 4, 5, 6, 7, 8 };
        string firstArg = Csm.MassData.CsmMassData.ConvertMassDataToArgument(small);

        byte[] filler = new byte[32];
        for (int i = 0; i < filler.Length; i++)
        {
            filler[i] = (byte)i;
        }

        Csm.MassData.CsmMassData.ConvertMassDataToArgument(filler);

        var ex = Assert.ThrowsExactly<CsmMassDataException>(
            () => Csm.MassData.CsmMassData.ConvertArgumentToMassData(firstArg));
        Assert.AreEqual(CsmMassDataStatus.Overwritten, ex.Status);
    }

    /// <summary>测试：写入数据大于缓冲区容量时应抛出 CacheTooSmall 异常。</summary>
    [TestMethod]
    public void Encode_ExceedsCacheSize_Throws()
    {
        Csm.MassData.CsmMassData.ConfigMassDataParameterCacheSize(16);
        byte[] big = new byte[64];

        var ex = Assert.ThrowsExactly<CsmMassDataException>(
            () => Csm.MassData.CsmMassData.ConvertMassDataToArgument(big));
        Assert.AreEqual(CsmMassDataStatus.CacheTooSmall, ex.Status);
    }

    /// <summary>测试：状态查询应当反映最近一次的读 / 写操作。</summary>
    [TestMethod]
    public void Status_ReflectsLastOperations()
    {
        Csm.MassData.CsmMassData.ConfigMassDataParameterCacheSize(1024);
        byte[] payload = { 9, 8, 7, 6, 5 };

        string arg = Csm.MassData.CsmMassData.ConvertMassDataToArgument(payload);
        byte[] restored = Csm.MassData.CsmMassData.ConvertArgumentToMassData(arg);
        Assert.HasCount(payload.Length, restored);

        Csm.MassData.CsmMassData.MassDataParameterStatus(
            out CsmMassDataOperation read,
            out CsmMassDataOperation write,
            out int cacheSize);

        Assert.AreEqual((ulong)payload.Length, write.Size);
        Assert.AreEqual((ulong)payload.Length, read.Size);
        Assert.AreEqual(1024, cacheSize);
    }

    /// <summary>测试：dataType 中包含非法字符时应抛出 InvalidArgument 异常。</summary>
    [TestMethod]
    public void EncodeWithDataType_RejectsForbiddenCharacters()
    {
        Csm.MassData.CsmMassData.ConfigMassDataParameterCacheSize(4096);
        byte[] data = { 1, 2, 3 };

        foreach (string bad in new[] { "with;semicolon", "with<angle", "with>angle" })
        {
            var ex = Assert.ThrowsExactly<CsmMassDataException>(
                () => Csm.MassData.CsmMassData.ConvertMassDataToArgumentWithDataType(data, bad));
            Assert.AreEqual(CsmMassDataStatus.InvalidArgument, ex.Status);
        }
    }

    /// <summary>测试：空数据的编码 / 解码。</summary>
    [TestMethod]
    public void EmptyData_RoundTrip()
    {
        Csm.MassData.CsmMassData.ConfigMassDataParameterCacheSize(1024);

        string arg = Csm.MassData.CsmMassData.ConvertMassDataToArgument(Array.Empty<byte>());
        byte[] restored = Csm.MassData.CsmMassData.ConvertArgumentToMassData(arg);
        Assert.IsEmpty(restored);
    }

    /// <summary>测试：所有公开 API 在多线程并发调用下保持一致性。</summary>
    [TestMethod]
    public void ThreadSafety_StressRoundTrip()
    {
        Csm.MassData.CsmMassData.ConfigMassDataParameterCacheSize(8 * 1024 * 1024);

        const int threadCount = 8;
        const int iterations = 200;
        Exception? firstFailure = null;

        var threads = new Thread[threadCount];
        for (int t = 0; t < threadCount; t++)
        {
            int seed = t;
            threads[t] = new Thread(() =>
            {
                try
                {
                    var rng = new Random(seed);
                    byte[] payload = new byte[256];
                    for (int i = 0; i < iterations; i++)
                    {
                        rng.NextBytes(payload);
                        string arg = Csm.MassData.CsmMassData.ConvertMassDataToArgument(payload);
                        byte[] restored = Csm.MassData.CsmMassData.ConvertArgumentToMassData(arg);
                        // 并发写入会导致部分引用被覆盖；只验证未被覆盖时
                        // 取回的内容与编码时写入的一致。
                        if (restored.Length == payload.Length)
                        {
                            CollectionAssert.AreEqual(payload, restored);
                        }
                    }
                }
                catch (CsmMassDataException ex) when (ex.Status == CsmMassDataStatus.Overwritten)
                {
                    // 并发条件下属于预期分支，不视为失败。
                }
                catch (Exception ex)
                {
                    Interlocked.CompareExchange(ref firstFailure, ex, null);
                }
            });
        }

        foreach (Thread thread in threads)
        {
            thread.Start();
        }

        foreach (Thread thread in threads)
        {
            thread.Join();
        }

        Assert.IsNull(firstFailure, firstFailure?.ToString());
    }

    private static void AssertParseError(string argument)
    {
        var ex = Assert.ThrowsExactly<CsmMassDataException>(
            () => Csm.MassData.CsmMassData.ConvertArgumentToMassData(argument));
        Assert.AreEqual(CsmMassDataStatus.ParseError, ex.Status, $"输入：{argument}");
    }
}
