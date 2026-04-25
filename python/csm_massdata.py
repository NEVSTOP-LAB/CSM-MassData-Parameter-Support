"""CSM MassData Parameter Support 插件的 Python 移植版本。

本模块公开的接口与 LabVIEW 端
``addons/MassData-Parameter/CSM MassData Parameter Support.lvlib`` 中的 VI
在功能与命名上完全一致；同时与 ``c/`` 目录下的 C 实现共享相同的
参考字符串格式与语义，可与 LabVIEW / C 端无缝互通 MassData 参数。

函数名称、参数顺序与语义均与对应的 LabVIEW VI 严格保持一致。

MassData 参数格式
-----------------
MassData 参数是一段仅包含 ASCII 字符、可读的引用字符串，指向进程内
一个全局环形缓冲区中的实际数据。支持以下两种形式：

  - 不带数据类型: ``<MassData>Start:<N>;Size:<N>``
  - 带数据类型:   ``<MassData>Start:<N>;Size:<N>;DataType:<T>``

其中 ``<N>`` 为非负十进制整数（与 C 端一致，按 64 位无符号整数解析），
``<T>`` 为自由格式的数据类型标签（例如 ``1D I32``、``Waveform`` 等），
不允许包含 ``;`` / ``<`` / ``>`` 字符。

数据生命周期
------------
MassData 内部使用环形字节缓冲区。当缓冲区写满后，新写入的数据将
从缓冲区起始位置覆盖最早的数据。被覆盖的数据无法恢复，后续对其
引用进行解码时会返回 :data:`CsmMassDataStatus.ERR_OVERWRITTEN`。
同一进程内的所有调用者共享同一份 MassData 缓冲区。

线程安全
--------
内部状态在模块加载（import）时即一次性完成初始化，避免延迟初始化
带来的并发竞争。所有公开函数对内部状态的访问均通过同一把
:class:`threading.Lock` 互斥串行化执行，可在多线程环境下安全调用。
"""

from __future__ import annotations

import enum
import threading
from typing import Optional, Tuple, Union

# --------------------------------------------------------------------------- #
#  常量与类型定义                                                             #
# --------------------------------------------------------------------------- #

#: MassData 缓冲区的默认大小（字节），与 LabVIEW VI 及 C 端保持一致：50 MiB。
CSM_MASSDATA_DEFAULT_CACHE_SIZE: int = 50 * 1024 * 1024

#: 编码函数返回的 MassData 参数字符串的最大长度（包含末尾 NUL 计入字节数）。
CSM_MASSDATA_MAX_ARGUMENT_LEN: int = 256

#: 数据类型标签字符串的最大长度（包含末尾 NUL 计入字节数）。
CSM_MASSDATA_MAX_DATATYPE_LEN: int = 128

# 引用字符串的固定前缀
_PREFIX = "<MassData>"

# 与 C 端 ``uint64_t`` 对应的最大值，用于显式检测溢出
_UINT64_MAX = (1 << 64) - 1


class CsmMassDataStatus(enum.IntEnum):
    """MassData API 所有函数返回的状态码。

    数值与 C 端 ``csm_massdata_status_t`` 完全一致，便于跨语言对照。
    """

    OK = 0                      #: 操作成功完成。
    ERR_INVALID_ARG = -1        #: 参数为 None 或无效。
    ERR_BUFFER_TOO_SMALL = -2   #: 调用方提供的输出缓冲区不足。
    ERR_PARSE = -3              #: MassData 参数字符串无法解析。
    ERR_OVERWRITTEN = -4        #: 引用的数据已被环形缓冲区覆盖。
    ERR_CACHE_TOO_SMALL = -5    #: 待写入的数据大于缓冲区容量。
    ERR_NO_MEMORY = -6          #: 内存分配失败。


class CsmMassDataOperation:
    """描述最近一次对 MassData 环形缓冲区的读或写操作。

    等价于 ``CSM - MassData Parameter Status.vi`` 返回的
    ``Active Read Operation`` / ``Active Write Operation`` 簇。
    """

    __slots__ = ("start", "size")

    def __init__(self, start: int = 0, size: int = 0) -> None:
        self.start = int(start)  #: 在缓冲区中的起始偏移量（字节）。
        self.size = int(size)    #: 该次操作的字节数。

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, CsmMassDataOperation):
            return NotImplemented
        return self.start == other.start and self.size == other.size

    def __repr__(self) -> str:  # pragma: no cover - 仅用于调试输出
        return f"CsmMassDataOperation(start={self.start}, size={self.size})"


# --------------------------------------------------------------------------- #
#  内部状态                                                                   #
# --------------------------------------------------------------------------- #

class _State:
    """内部环形缓冲区与游标状态。仅供本模块自身使用。"""

    __slots__ = ("lock", "buffer", "capacity", "write_total",
                 "last_read", "last_write")

    def __init__(self) -> None:
        # 互斥量保护所有可变成员；模块导入时即创建，避免延迟初始化竞态。
        self.lock = threading.Lock()
        self.buffer: bytearray = bytearray(CSM_MASSDATA_DEFAULT_CACHE_SIZE)
        self.capacity: int = CSM_MASSDATA_DEFAULT_CACHE_SIZE
        # 累计已写入的字节数（与 C 端 ``write_total`` 等价的 64 位单调游标）。
        self.write_total: int = 0
        self.last_read = CsmMassDataOperation()
        self.last_write = CsmMassDataOperation()


# 模块加载时即完成初始化，避免任何首次调用的并发竞争。
_state = _State()


# --------------------------------------------------------------------------- #
#  内部辅助函数                                                               #
# --------------------------------------------------------------------------- #

def _ring_write(src: bytes) -> None:
    """按 ``write_total`` 暗示的位置把 ``src`` 写入环形缓冲区。

    调用方必须已经持有 ``_state.lock``，并已确认 ``len(src) <= capacity``。
    """
    cap = _state.capacity
    offset = _state.write_total % cap
    first_run = cap - offset
    n = len(src)
    if n <= first_run:
        _state.buffer[offset:offset + n] = src
    else:
        _state.buffer[offset:offset + first_run] = src[:first_run]
        _state.buffer[0:n - first_run] = src[first_run:]


def _ring_read(start: int, size: int) -> bytes:
    """从环形缓冲区中以绝对游标 ``start`` 起始位置读取 ``size`` 字节。

    调用方必须已经持有 ``_state.lock``，并已确认所请求的范围仍然驻留。
    """
    cap = _state.capacity
    offset = start % cap
    first_run = cap - offset
    if size <= first_run:
        return bytes(_state.buffer[offset:offset + size])
    return (bytes(_state.buffer[offset:offset + first_run])
            + bytes(_state.buffer[0:size - first_run]))


def _parse_uint64(text: str) -> Tuple[Optional[int], int]:
    """从 ``text`` 起始位置解析一个非负十进制整数。

    返回 ``(value, end_index)``。若没有数字或数值超出 64 位无符号
    整数表示范围（与 C 端 ``strtoull`` 的 ``ERANGE`` 等价），则 ``value``
    为 ``None``。
    """
    i = 0
    n = len(text)
    while i < n and "0" <= text[i] <= "9":
        i += 1
    if i == 0:
        return None, 0
    value = int(text[:i])
    if value > _UINT64_MAX:
        return None, i
    return value, i


def _parse_argument(argument: str) -> Tuple[CsmMassDataStatus, int, int, str]:
    """解析形如 ``<MassData>Start:<N>;Size:<N>[;DataType:<T>]`` 的字符串。

    返回 ``(status, start, size, data_type)``；当字符串中没有 ``DataType``
    后缀时 ``data_type`` 为空字符串。
    """
    if not isinstance(argument, str):
        return CsmMassDataStatus.ERR_INVALID_ARG, 0, 0, ""
    if not argument.startswith(_PREFIX):
        return CsmMassDataStatus.ERR_PARSE, 0, 0, ""
    p = argument[len(_PREFIX):]

    if not p.startswith("Start:"):
        return CsmMassDataStatus.ERR_PARSE, 0, 0, ""
    p = p[len("Start:"):]
    start, end = _parse_uint64(p)
    if start is None or end == 0 or end >= len(p) or p[end] != ";":
        return CsmMassDataStatus.ERR_PARSE, 0, 0, ""
    p = p[end + 1:]

    if not p.startswith("Size:"):
        return CsmMassDataStatus.ERR_PARSE, 0, 0, ""
    p = p[len("Size:"):]
    size, end = _parse_uint64(p)
    if size is None or end == 0:
        return CsmMassDataStatus.ERR_PARSE, 0, 0, ""
    p = p[end:]

    # 可选的 ``;DataType:<T>`` 后缀。
    if p == "":
        return CsmMassDataStatus.OK, start, size, ""
    if not p.startswith(";"):
        return CsmMassDataStatus.ERR_PARSE, 0, 0, ""
    p = p[1:]
    if not p.startswith("DataType:"):
        return CsmMassDataStatus.ERR_PARSE, 0, 0, ""
    data_type = p[len("DataType:"):]
    # 验证 DataType 值仅包含合法字符。
    for ch in data_type:
        if ch in ";<>":
            return CsmMassDataStatus.ERR_PARSE, 0, 0, ""
    return CsmMassDataStatus.OK, start, size, data_type


def _coerce_data(data: Optional[Union[bytes, bytearray, memoryview]]) -> Optional[bytes]:
    """将允许的数据输入归一化为 ``bytes``；无效输入返回 ``None``。

    与 C 端 ``data_size == 0`` 时允许 ``data == NULL`` 的语义对应：
    本函数允许 ``data is None``，视作零长度数据。
    """
    if data is None:
        return b""
    if isinstance(data, (bytes, bytearray, memoryview)):
        return bytes(data)
    return None


# --------------------------------------------------------------------------- #
#  公开 API —— 每个函数对应一个同名的 LabVIEW VI                              #
# --------------------------------------------------------------------------- #

def CSM_ConfigMassDataParameterCacheSize(size: int) -> CsmMassDataStatus:
    """配置 MassData 后台缓冲区大小。

    对应 ``CSM - Config MassData Parameter Cache Size.vi``。

    将内部环形缓冲区重新分配为 ``size`` 字节。与 LabVIEW VI 一致，
    在程序运行过程中调用本函数会丢弃当前已缓存的数据；通常应在
    任何编码 / 解码调用之前、应用启动阶段调用一次。

    :param size: 新的缓冲区大小（字节）。若从未调用过本函数，
        则默认值为 :data:`CSM_MASSDATA_DEFAULT_CACHE_SIZE`。
    :return: 成功返回 :attr:`CsmMassDataStatus.OK`；
        若 ``size`` 不是正整数返回 :attr:`CsmMassDataStatus.ERR_INVALID_ARG`；
        若分配失败返回 :attr:`CsmMassDataStatus.ERR_NO_MEMORY`。
    """
    if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
        return CsmMassDataStatus.ERR_INVALID_ARG
    try:
        new_buf = bytearray(size)
    except (MemoryError, OverflowError):
        return CsmMassDataStatus.ERR_NO_MEMORY

    with _state.lock:
        _state.buffer = new_buf
        _state.capacity = size
        _state.write_total = 0
        _state.last_read = CsmMassDataOperation()
        _state.last_write = CsmMassDataOperation()
    return CsmMassDataStatus.OK


def _encode(data: Optional[Union[bytes, bytearray, memoryview]],
            data_type: Optional[str]) -> Tuple[CsmMassDataStatus, str]:
    """编码公共实现：对应 C 端 ``csm_massdata_encode``。"""
    src = _coerce_data(data)
    if src is None:
        return CsmMassDataStatus.ERR_INVALID_ARG, ""

    if data_type is not None:
        if not isinstance(data_type, str):
            return CsmMassDataStatus.ERR_INVALID_ARG, ""
        # 与 C 端长度限制对应（含末尾 NUL）。
        if len(data_type) + 1 > CSM_MASSDATA_MAX_DATATYPE_LEN:
            return CsmMassDataStatus.ERR_BUFFER_TOO_SMALL, ""
        # 拒绝可能破坏引用字符串语法的字符。
        for ch in data_type:
            if ch in ";<>":
                return CsmMassDataStatus.ERR_INVALID_ARG, ""

    with _state.lock:
        if len(src) > _state.capacity:
            return CsmMassDataStatus.ERR_CACHE_TOO_SMALL, ""
        start_cursor = _state.write_total
        if src:
            _ring_write(src)
            _state.write_total += len(src)
        _state.last_write = CsmMassDataOperation(start_cursor, len(src))

    if data_type is not None:
        argument = (f"{_PREFIX}Start:{start_cursor};Size:{len(src)};"
                    f"DataType:{data_type}")
    else:
        argument = f"{_PREFIX}Start:{start_cursor};Size:{len(src)}"
    # 与 C 端一致：包含末尾 NUL 后不得超过 ``CSM_MASSDATA_MAX_ARGUMENT_LEN``。
    if len(argument) + 1 > CSM_MASSDATA_MAX_ARGUMENT_LEN:
        return CsmMassDataStatus.ERR_BUFFER_TOO_SMALL, ""
    return CsmMassDataStatus.OK, argument


def CSM_ConvertMassDataToArgument(
        data: Optional[Union[bytes, bytearray, memoryview]],
) -> Tuple[CsmMassDataStatus, str]:
    """将原始数据转换为 MassData 参数（不嵌入数据类型）。

    对应 ``CSM - Convert MassData to Argument.vim``。原始数据被复制到
    环形缓冲区，并返回形如 ``<MassData>Start:<N>;Size:<N>`` 的引用字符串。

    :param data: 待保存的原始字节，可为 ``bytes`` / ``bytearray`` /
        ``memoryview``；``None`` 视作零长度数据。
    :return: ``(status, argument)``。失败时 ``argument`` 为空字符串。
    """
    return _encode(data, None)


def CSM_ConvertMassDataToArgumentWithDataType(
        data: Optional[Union[bytes, bytearray, memoryview]],
        data_type: str,
) -> Tuple[CsmMassDataStatus, str]:
    """将原始数据转换为带数据类型标签的 MassData 参数。

    对应 ``CSM - Convert MassData to Argument With DataType.vim``。
    生成的参数形如 ``<MassData>Start:<N>;Size:<N>;DataType:<data_type>``。

    :param data: 待保存的原始字节。
    :param data_type: 以普通字符串表示的数据类型标签（例如 ``"1D I32"``），
        不允许包含 ``;`` / ``<`` / ``>``。
    :return: ``(status, argument)``。
    """
    if data_type is None:
        return CsmMassDataStatus.ERR_INVALID_ARG, ""
    return _encode(data, data_type)


def CSM_ConvertArgumentToMassData(
        argument: str,
) -> Tuple[CsmMassDataStatus, bytes]:
    """将 MassData 参数还原为原始数据。

    对应 ``CSM - Convert Argument to MassData.vim``。本函数解析
    ``argument`` 中的引用字符串，并返回对应的字节。LabVIEW VI 中可选的
    ``Type`` 输入在此处刻意省略：返回的就是此前写入的原始字节，
    不受嵌入的类型标签影响。

    :param argument: MassData 参数字符串。
    :return: ``(status, data)``，其中 ``data`` 为 ``bytes``；
        当解析失败、数据已被覆盖或缓存不足时为空 ``bytes``。
    """
    status, start, size, _ = _parse_argument(argument)
    if status != CsmMassDataStatus.OK:
        return status, b""

    with _state.lock:
        if size > _state.capacity:
            return CsmMassDataStatus.ERR_OVERWRITTEN, b""
        # 当前驻留在环形缓冲区中的窗口为
        # ``[write_total - capacity, write_total)``。任何末端落在该窗口
        # 之外的请求都视为已被覆盖。Python 整数为任意精度，加法不会
        # 溢出，但仍按 C 端等价的非溢出比较方式拆开判断，便于跨语言对照。
        oldest = (_state.write_total - _state.capacity
                  if _state.write_total > _state.capacity else 0)
        if start < oldest or start > _state.write_total:
            return CsmMassDataStatus.ERR_OVERWRITTEN, b""
        end = start + size
        if end > _state.write_total:
            return CsmMassDataStatus.ERR_OVERWRITTEN, b""
        result = _ring_read(start, size) if size > 0 else b""
        _state.last_read = CsmMassDataOperation(start, size)

    return CsmMassDataStatus.OK, result


def CSM_MassDataDataTypeString(
        argument: str,
) -> Tuple[CsmMassDataStatus, str, str]:
    """从 MassData 参数中解析出数据类型字符串。

    对应 ``CSM - MassData Data Type String.vi``。本函数不会消费输入：
    返回 ``(status, argument_dup, data_type)``，``argument_dup`` 为
    ``argument`` 的副本，以模仿 LabVIEW VI 中“返回输入副本”的数据流
    行为；若参数中没有 ``DataType`` 字段，``data_type`` 为空字符串。
    """
    if argument is None or not isinstance(argument, str):
        return CsmMassDataStatus.ERR_INVALID_ARG, "", ""
    status, _, _, data_type = _parse_argument(argument)
    if status != CsmMassDataStatus.OK:
        # 即便解析失败，也按照 LabVIEW VI 的语义返回输入字符串自身。
        return status, argument, ""
    return CsmMassDataStatus.OK, argument, data_type


def CSM_MassDataParameterStatus(
) -> Tuple[CsmMassDataStatus, CsmMassDataOperation, CsmMassDataOperation, int]:
    """读取 MassData 后台缓冲区的状态信息。

    对应 ``CSM - MassData Parameter Status.vi``。

    :return: ``(status, active_read, active_write, cache_size)``，其中
        ``active_read`` / ``active_write`` 为 :class:`CsmMassDataOperation`
        快照，``cache_size`` 为当前配置的缓冲区大小（字节）。
    """
    with _state.lock:
        active_read = CsmMassDataOperation(_state.last_read.start,
                                           _state.last_read.size)
        active_write = CsmMassDataOperation(_state.last_write.start,
                                            _state.last_write.size)
        cache_size = _state.capacity
    return CsmMassDataStatus.OK, active_read, active_write, cache_size


__all__ = [
    "CSM_MASSDATA_DEFAULT_CACHE_SIZE",
    "CSM_MASSDATA_MAX_ARGUMENT_LEN",
    "CSM_MASSDATA_MAX_DATATYPE_LEN",
    "CsmMassDataStatus",
    "CsmMassDataOperation",
    "CSM_ConfigMassDataParameterCacheSize",
    "CSM_ConvertMassDataToArgument",
    "CSM_ConvertMassDataToArgumentWithDataType",
    "CSM_ConvertArgumentToMassData",
    "CSM_MassDataDataTypeString",
    "CSM_MassDataParameterStatus",
]
