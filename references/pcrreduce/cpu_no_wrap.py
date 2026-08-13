"""CPU 普通整数 no-wrap 份额工具。

这里放 CPU 侧的小规模验证工具，语义和 GPU no-wrap 工具保持一致：

    subtractive: x = x1 - x0
    additive:    x = x0 + x1

随机份额范围和 GPU 版保持一致：

    share0 sampled from [0, share_bound)

这些函数主要用于协议正确性单元测试，不参与 GPU 性能路径。
"""

from __future__ import annotations

import random


def subtractive_share_no_wrap(
    secret: int,
    share_bound: int = 2**30,
    rng: random.Random | None = None,
    *,
    bound: int | None = None,
) -> tuple[int, int]:
    """生成 CPU 普通整数 no-wrap 减法份额：secret = share1 - share0。"""
    if bound is not None:
        share_bound = bound
    if rng is None:
        rng = random.Random()
    if share_bound <= 0:
        raise ValueError("share_bound must be positive")

    share0 = rng.randrange(0, share_bound)
    share1 = share0 + int(secret)
    return share0, share1


def subtractive_reconstruct_no_wrap(share0: int, share1: int) -> int:
    """重构 CPU 普通整数 no-wrap 减法份额。"""
    return int(share1) - int(share0)


def additive_share_no_wrap(
    secret: int,
    share_bound: int = 2**30,
    rng: random.Random | None = None,
    *,
    bound: int | None = None,
) -> tuple[int, int]:
    """生成 CPU 普通整数 no-wrap 加法份额：secret = share0 + share1。"""
    if bound is not None:
        share_bound = bound
    if rng is None:
        rng = random.Random()
    if share_bound <= 0:
        raise ValueError("share_bound must be positive")

    share0 = rng.randrange(0, share_bound)
    share1 = int(secret) - share0
    return share0, share1


def additive_reconstruct_no_wrap(share0: int, share1: int) -> int:
    """重构 CPU 普通整数 no-wrap 加法份额。"""
    return int(share0) + int(share1)
