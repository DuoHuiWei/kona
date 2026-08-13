"""布尔 XOR 份额上的安全 OR。

表示方式：
    x_share = (x0, x1)，满足 x = x0 ^ x1
    y_share = (y0, y1)，满足 y = y0 ^ y1

安全 OR 使用公式：
    x OR y = x XOR y XOR (x AND y)

其中 XOR 是安全 XOR：两方只对自己的份额本地异或，不打开明文、不通信；AND 使用 Boolean Beaver triple。
"""

from __future__ import annotations

from beaver import BooleanTriple, boolean_and_beaver_bitset
from shares import Share, bit_mask
from stats import Channel


# 布尔安全或 OR
def boolean_or_beaver_bitset(
    x_share: Share,
    y_share: Share,
    pool: list[BooleanTriple],
    width: int,
    channel: Channel | None = None,
) -> Share:
    """用 Boolean Beaver AND 实现 XOR 份额上的安全 OR。"""
    # 还缺的协议功能：安全 NAND、NOR、批量/list 布尔 OR。
    mask = bit_mask(width)

    # 公式：x OR y = x XOR y XOR (x AND y)，其中 XOR 是对本地份额分别异或。
    and_share = boolean_and_beaver_bitset(x_share, y_share, pool, width, channel)

    z0 = (x_share[0] ^ y_share[0] ^ and_share[0]) & mask
    z1 = (x_share[1] ^ y_share[1] ^ and_share[1]) & mask
    return z0, z1

