"""Naked-tuple secret sharing helpers for version 1.

Representation:
    share = (s0, s1)

Semantics depend on the helper being used:
    additive:    x = s0 + s1 mod M
    subtractive: x = s1 - s0 mod M
    xor:         x = s0 ^ s1

This file intentionally avoids a Share class in version 1. The function names
carry the semantics explicitly, and modulus/width are passed by the caller.
"""

from __future__ import annotations

import random
from typing import Iterable, TypeAlias

Share: TypeAlias = tuple[int, int]


# ============================================================
# Random seed helper
# ============================================================

# 固定随机数种子，让每次运行得到一样的随机数。
def set_random_seed(seed: int) -> None:
    """Set the Python PRNG seed for reproducible smoke tests."""
    random.seed(seed)


# ============================================================
# Additive sharing: x = s0 + s1 mod M
# ============================================================

# 把一个明文秘密 secret 拆成两个份额 (s0, s1)，满足：
def share_additive(secret: int, mod: int) -> Share:
    """Share a scalar secret as x = s0 + s1 mod M."""
    if mod <= 0:
        raise ValueError("mod must be positive")
    secret = secret % mod
    s0 = random.randrange(mod)
    s1 = (secret - s0) % mod
    return s0, s1

# 把两个加法份额重新合成明文。
def reconstruct_additive(share: Share, mod: int) -> int:
    """Reconstruct an additive share."""
    s0, s1 = share
    return (s0 + s1) % mod

# 在不恢复明文的情况下，对两个秘密共享的数做加法。
def add_additive(x: Share, y: Share, mod: int) -> Share:
    """Add two additive shares locally."""
    return (x[0] + y[0]) % mod, (x[1] + y[1]) % mod

# 在不恢复明文的情况下，对两个秘密共享的数做减法。
def sub_additive(x: Share, y: Share, mod: int) -> Share:
    """Subtract two additive shares locally."""
    return (x[0] - y[0]) % mod, (x[1] - y[1]) % mod


# ============================================================
# Subtractive sharing: x = s1 - s0 mod M
# ============================================================

# 把明文 secret 拆成减法共享份额。
def share_subtractive(secret: int, mod: int) -> Share:
    """Share a scalar secret as x = s1 - s0 mod M."""
    if mod <= 0:
        raise ValueError("mod must be positive")
    secret = secret % mod
    s0 = random.randrange(mod)
    s1 = (secret + s0) % mod
    return s0, s1

# 把减法份额恢复成明文。
def reconstruct_subtractive(share: Share, mod: int) -> int:
    """Reconstruct a subtractive share."""
    s0, s1 = share
    return (s1 - s0) % mod

# 对两个减法共享的秘密做加法。
def add_subtractive(x: Share, y: Share, mod: int) -> Share:
    """Add two subtractive shares locally."""
    return (x[0] + y[0]) % mod, (x[1] + y[1]) % mod

# 对两个减法共享的秘密做减法，也就是计算 x - y。
def sub_subtractive(x: Share, y: Share, mod: int) -> Share:
    """Subtract y from x for subtractive shares."""
    return (x[0] - y[0]) % mod, (x[1] - y[1]) % mod


# ============================================================
# 加减法秘密份额互转，服务器s1变化
# ============================================================
# 加法秘密份额转减法秘密份额
def additive_to_subtractive(share: Share, mod: int) -> Share:
    """Convert additive share x = s0 + s1 mod M to subtractive share x = t1 - t0 mod M."""
    s0, s1 = share
    return (-s0) % mod, s1 % mod

# 减法秘密份额转加法秘密份额
def subtractive_to_additive(share: Share, mod: int) -> Share:
    """Convert subtractive share x = s1 - s0 mod M to additive share x = t0 + t1 mod M."""
    s0, s1 = share
    return (-s0) % mod, s1 % mod


# ============================================================
# XOR sharing: x = s0 ^ s1 
# 这组函数是异或秘密共享 XOR sharing，主要用于处理二进制比特向量。
# 这里的 ^ 是 Python 里的按位异或。
# 里面 int 被当成一个二进制比特串来使用。
# Python 里没有专门的“二进制数类型”。下面这些本质上都是 int
# 10          # 十进制写法
# 0b1010      # 二进制写法
# 0xA         # 十六进制写法
# 它们在 Python 里都是同一个整数：

# 10 == 0b1010 == 0xA
# 案例：
# secret = 0b1010
# width = 4
# 实际还是int
# ============================================================

# 生成一个低 width 位全是 1 的掩码。
# 这个函数的作用是限制比特长度，防止多余高位参与计算。
def bit_mask(width: int) -> int:
    """Return a mask with width low bits set."""
    if width < 0:
        raise ValueError("width must be non-negative")
    return (1 << width) - 1

# 把一个二进制秘密拆成两个 XOR 份额。
# 0b1010
def share_xor_bitset(secret: int, width: int) -> Share:
    """XOR-share a packed bit vector stored as a Python int."""
    mask = bit_mask(width)
    secret &= mask
    s0 = random.getrandbits(width) & mask
    s1 = secret ^ s0
    return s0, s1

# 把 XOR 份额恢复成原始秘密。
def reconstruct_xor(share: Share, width: int | None = None) -> int:
    """Reconstruct an XOR share, optionally masking the output width."""
    value = share[0] ^ share[1]
    if width is None:
        return value
    return value & bit_mask(width)

# 对两个 XOR 共享的秘密做异或运算。
def xor_share(x: Share, y: Share, width: int | None = None) -> Share:
    """XOR two XOR shares locally."""
    s0 = x[0] ^ y[0]
    s1 = x[1] ^ y[1]
    if width is None:
        return s0, s1
    mask = bit_mask(width)
    return s0 & mask, s1 & mask

# 对 XOR 共享的秘密做按位取反 NOT
def not_xor_share(x: Share, width: int) -> Share:
    """NOT an XOR share by flipping one party's packed bits."""
    mask = bit_mask(width)
    return x[0] ^ mask, x[1]



# ============================================================
# Small vector helpers using Python lists for version 1
# ============================================================

# 制造一组加法秘密共享
def share_additive_list(values: Iterable[int], mod: int) -> tuple[list[int], list[int]]:
    """Additively share a list of scalar values using Python lists."""
    left: list[int] = []
    right: list[int] = []
    for value in values:
        s0, s1 = share_additive(value, mod)
        left.append(s0)
        right.append(s1)
    return left, right

# 恢复一组加法秘密共享。
def reconstruct_additive_list(share: tuple[list[int], list[int]], mod: int) -> list[int]:
    """Reconstruct a Python-list additive vector share."""
    s0, s1 = share
    if len(s0) != len(s1):
        raise ValueError("share lists must have the same length")
    return [(a + b) % mod for a, b in zip(s0, s1)]

# 把一组明文数全部拆成减法秘密共享。
def share_subtractive_list(values: Iterable[int], mod: int) -> tuple[list[int], list[int]]:
    """Subtractive-share a list of scalar values using Python lists."""
    left: list[int] = []
    right: list[int] = []
    for value in values:
        s0, s1 = share_subtractive(value, mod)
        left.append(s0)
        right.append(s1)
    return left, right

# 恢复一组减法秘密共享。
def reconstruct_subtractive_list(share: tuple[list[int], list[int]], mod: int) -> list[int]:
    """Reconstruct a Python-list subtractive vector share."""
    s0, s1 = share
    if len(s0) != len(s1):
        raise ValueError("share lists must have the same length")
    return [(b - a) % mod for a, b in zip(s0, s1)]



