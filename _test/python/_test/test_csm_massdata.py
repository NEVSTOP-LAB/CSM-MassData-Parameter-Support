"""CSM MassData Python API 的独立测试套件。

覆盖 ``csm_massdata`` 模块公开的所有 API：

  - 缓冲区配置与状态查询
  - 不带数据类型的编码 / 解码往返
  - 带数据类型的编码 / 解码往返
  - 数据类型解析（CSM - MassData Data Type String）
  - 解析错误的处理
  - 环形缓冲区覆盖检测
  - 输出缓冲区与缓存容量边界
  - 并发访问下的线程安全

任意断言失败时进程以非零状态退出，方便在交互模式与 CI 流水线中
同时使用。
"""

from __future__ import annotations

import os
import struct
import sys
import threading
import unittest

# 让脚本以 ``python _test/test_csm_massdata.py`` 直接运行也能找到模块。
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from csm_massdata import (  # noqa: E402  (path manipulation must come first)
    CSM_MASSDATA_MAX_ARGUMENT_LEN,
    CSM_MASSDATA_MAX_DATATYPE_LEN,
    CSM_ConfigMassDataParameterCacheSize,
    CSM_ConvertArgumentToMassData,
    CSM_ConvertMassDataToArgument,
    CSM_ConvertMassDataToArgumentWithDataType,
    CSM_MassDataDataTypeString,
    CSM_MassDataParameterStatus,
    CsmMassDataStatus,
)


class CsmMassDataTests(unittest.TestCase):
    """与 ``c/_test/vs/test_main.c`` 中的测试一一对应。"""

    def setUp(self) -> None:
        # 每个用例都从一个干净的较小缓冲区开始，避免相互影响。
        self.assertEqual(
            CSM_ConfigMassDataParameterCacheSize(4096), CsmMassDataStatus.OK
        )

    # 测试：缓冲区配置与状态查询
    def test_config_and_status(self) -> None:
        self.assertEqual(
            CSM_ConfigMassDataParameterCacheSize(0),
            CsmMassDataStatus.ERR_INVALID_ARG,
        )
        self.assertEqual(
            CSM_ConfigMassDataParameterCacheSize(-1),
            CsmMassDataStatus.ERR_INVALID_ARG,
        )
        self.assertEqual(
            CSM_ConfigMassDataParameterCacheSize(1024), CsmMassDataStatus.OK
        )
        status, r, w, cache = CSM_MassDataParameterStatus()
        self.assertEqual(status, CsmMassDataStatus.OK)
        self.assertEqual(cache, 1024)
        self.assertEqual(r.size, 0)
        self.assertEqual(w.size, 0)

    # 测试：不带数据类型的编码 / 解码往返
    def test_roundtrip_plain(self) -> None:
        source = struct.pack("<8i", 10, 20, 30, 40, 50, 60, 70, 80)
        status, arg = CSM_ConvertMassDataToArgument(source)
        self.assertEqual(status, CsmMassDataStatus.OK)
        self.assertTrue(arg.startswith("<MassData>Start:"),
                        msg=f"unexpected argument: {arg!r}")

        status, restored = CSM_ConvertArgumentToMassData(arg)
        self.assertEqual(status, CsmMassDataStatus.OK)
        self.assertEqual(len(restored), len(source))
        self.assertEqual(restored, source)

    # 测试：带数据类型的编码 / 解码往返
    def test_roundtrip_with_type(self) -> None:
        source = struct.pack("<4d", 1.5, -2.5, 3.5, -4.5)
        status, arg = CSM_ConvertMassDataToArgumentWithDataType(source, "1D DBL")
        self.assertEqual(status, CsmMassDataStatus.OK)
        self.assertIn(";DataType:1D DBL", arg)

        status, dup, dtype = CSM_MassDataDataTypeString(arg)
        self.assertEqual(status, CsmMassDataStatus.OK)
        self.assertEqual(dtype, "1D DBL")
        self.assertEqual(dup, arg)

        status, restored = CSM_ConvertArgumentToMassData(arg)
        self.assertEqual(status, CsmMassDataStatus.OK)
        self.assertEqual(len(restored), len(source))
        self.assertEqual(restored, source)

    # 测试：参数中没有数据类型字段时，解析结果应为空字符串
    def test_datatype_absent(self) -> None:
        payload = bytes([0xAA, 0xBB, 0xCC])
        status, arg = CSM_ConvertMassDataToArgument(payload)
        self.assertEqual(status, CsmMassDataStatus.OK)
        status, dup, dtype = CSM_MassDataDataTypeString(arg)
        self.assertEqual(status, CsmMassDataStatus.OK)
        self.assertEqual(dtype, "")
        self.assertEqual(dup, arg)

    # 测试：解析非法字符串时返回 PARSE 错误
    def test_parse_errors(self) -> None:
        for bad in (
            "garbage",
            "<MassData>Start:abc;Size:1",
            "<MassData>Start:0;Size:1;wrong",
            "<MassData>Start:0;Size:1;DataType:bad;value",
            "<MassData>Start:0;Size:1;DataType:bad<value",
            "<MassData>Start:0;Size:1;DataType:bad>value",
            # 超出 64 位无符号整数范围
            "<MassData>Start:18446744073709551616;Size:0",
        ):
            status, data = CSM_ConvertArgumentToMassData(bad)
            self.assertEqual(status, CsmMassDataStatus.ERR_PARSE,
                             msg=f"argument should fail to parse: {bad!r}")
            self.assertEqual(data, b"")

    # 测试：旧数据被环形缓冲区覆盖后，应报告 OVERWRITTEN
    def test_overwrite_detection(self) -> None:
        # 缓冲区故意设得很小，后续写入会把第一份数据挤掉。
        self.assertEqual(
            CSM_ConfigMassDataParameterCacheSize(32), CsmMassDataStatus.OK
        )
        status, first_arg = CSM_ConvertMassDataToArgument(bytes(range(1, 9)))
        self.assertEqual(status, CsmMassDataStatus.OK)
        status, _filler_arg = CSM_ConvertMassDataToArgument(bytes(range(32)))
        self.assertEqual(status, CsmMassDataStatus.OK)
        status, restored = CSM_ConvertArgumentToMassData(first_arg)
        self.assertEqual(status, CsmMassDataStatus.ERR_OVERWRITTEN)
        self.assertEqual(restored, b"")

    # 测试：写入数据大于缓冲区容量时返回 CACHE_TOO_SMALL
    def test_cache_too_small(self) -> None:
        self.assertEqual(
            CSM_ConfigMassDataParameterCacheSize(16), CsmMassDataStatus.OK
        )
        status, arg = CSM_ConvertMassDataToArgument(bytes(64))
        self.assertEqual(status, CsmMassDataStatus.ERR_CACHE_TOO_SMALL)
        self.assertEqual(arg, "")

    # 测试：状态查询应当反映最近一次的读 / 写操作
    def test_status_reflects_last_ops(self) -> None:
        self.assertEqual(
            CSM_ConfigMassDataParameterCacheSize(1024), CsmMassDataStatus.OK
        )
        payload = bytes([9, 8, 7, 6, 5])
        status, arg = CSM_ConvertMassDataToArgument(payload)
        self.assertEqual(status, CsmMassDataStatus.OK)
        status, restored = CSM_ConvertArgumentToMassData(arg)
        self.assertEqual(status, CsmMassDataStatus.OK)
        self.assertEqual(restored, payload)

        status, r, w, cache = CSM_MassDataParameterStatus()
        self.assertEqual(status, CsmMassDataStatus.OK)
        self.assertEqual(w.size, len(payload))
        self.assertEqual(r.size, len(payload))
        self.assertEqual(cache, 1024)

    # 测试：编码时拒绝包含非法字符的数据类型
    def test_encode_rejects_bad_data_type(self) -> None:
        for bad in (";", "<", ">", "1D I32;extra"):
            status, arg = CSM_ConvertMassDataToArgumentWithDataType(b"x", bad)
            self.assertEqual(status, CsmMassDataStatus.ERR_INVALID_ARG,
                             msg=f"data_type should be rejected: {bad!r}")
            self.assertEqual(arg, "")

        # 数据类型超长时返回 BUFFER_TOO_SMALL（与 C 端常量一致）
        too_long = "x" * CSM_MASSDATA_MAX_DATATYPE_LEN
        status, arg = CSM_ConvertMassDataToArgumentWithDataType(b"x", too_long)
        self.assertEqual(status, CsmMassDataStatus.ERR_BUFFER_TOO_SMALL)

    # 测试：编码后的引用字符串长度受 ``CSM_MASSDATA_MAX_ARGUMENT_LEN`` 约束
    def test_argument_length_bound(self) -> None:
        status, arg = CSM_ConvertMassDataToArgument(b"hello world")
        self.assertEqual(status, CsmMassDataStatus.OK)
        # 含末尾 NUL 的等价长度上限校验
        self.assertLess(len(arg) + 1, CSM_MASSDATA_MAX_ARGUMENT_LEN)

    # 测试：CSM_MassDataDataTypeString 对非法输入返回错误，但 dup 仍为输入
    def test_data_type_string_invalid(self) -> None:
        status, dup, dtype = CSM_MassDataDataTypeString("garbage")
        self.assertEqual(status, CsmMassDataStatus.ERR_PARSE)
        self.assertEqual(dup, "garbage")
        self.assertEqual(dtype, "")

        status, dup, dtype = CSM_MassDataDataTypeString(None)  # type: ignore[arg-type]
        self.assertEqual(status, CsmMassDataStatus.ERR_INVALID_ARG)
        self.assertEqual(dup, "")
        self.assertEqual(dtype, "")

    # 测试：多线程并发编码 / 解码不会损坏内部状态
    def test_thread_safety(self) -> None:
        self.assertEqual(
            CSM_ConfigMassDataParameterCacheSize(1024 * 1024),
            CsmMassDataStatus.OK,
        )
        errors: list[str] = []
        barrier = threading.Barrier(8)

        def worker(tid: int) -> None:
            barrier.wait()
            for i in range(200):
                payload = struct.pack("<2I", tid, i) * 8  # 64 字节
                status, arg = CSM_ConvertMassDataToArgument(payload)
                if status != CsmMassDataStatus.OK:
                    errors.append(f"encode failed: {status!r}")
                    return
                status, restored = CSM_ConvertArgumentToMassData(arg)
                if status != CsmMassDataStatus.OK:
                    errors.append(f"decode failed: {status!r}")
                    return
                if restored != payload:
                    errors.append(
                        f"data mismatch on tid={tid} i={i}: "
                        f"expected {payload!r}, got {restored!r}"
                    )
                    return

        threads = [threading.Thread(target=worker, args=(t,)) for t in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(errors, [], msg="\n".join(errors))


    # 测试：超长数字串应被快速拒绝（不构造任意精度大整数）
    def test_parse_long_digit_run_rejected_fast(self) -> None:
        bad = "<MassData>Start:" + ("9" * 1_000_000) + ";Size:0"
        status, data = CSM_ConvertArgumentToMassData(bad)
        self.assertEqual(status, CsmMassDataStatus.ERR_PARSE)
        self.assertEqual(data, b"")

    # 测试：解析端的 DataType 长度限制与 C 端一致
    def test_parse_rejects_overlong_data_type(self) -> None:
        too_long = "x" * CSM_MASSDATA_MAX_DATATYPE_LEN  # 含末尾 NUL 后超限
        arg = f"<MassData>Start:0;Size:0;DataType:{too_long}"
        status, dup, dtype = CSM_MassDataDataTypeString(arg)
        self.assertEqual(status, CsmMassDataStatus.ERR_BUFFER_TOO_SMALL)
        self.assertEqual(dtype, "")

    # 测试：传入 ``memoryview`` / ``bytearray`` 时不会被多余拷贝破坏数据
    def test_accepts_bytearray_and_memoryview(self) -> None:
        ba = bytearray(b"hello world")
        status, arg = CSM_ConvertMassDataToArgument(ba)
        self.assertEqual(status, CsmMassDataStatus.OK)
        status, restored = CSM_ConvertArgumentToMassData(arg)
        self.assertEqual(status, CsmMassDataStatus.OK)
        self.assertEqual(restored, bytes(ba))

        mv = memoryview(b"another payload")
        status, arg = CSM_ConvertMassDataToArgument(mv)
        self.assertEqual(status, CsmMassDataStatus.OK)
        status, restored = CSM_ConvertArgumentToMassData(arg)
        self.assertEqual(status, CsmMassDataStatus.OK)
        self.assertEqual(restored, bytes(mv))


if __name__ == "__main__":
    unittest.main(verbosity=2)
