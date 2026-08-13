"""Python-int bitset helpers.
Python int 形式的 bitset 工具函数。
"""

from __future__ import annotations


def bit_mask(width: int) -> int:
    if width < 0:
        raise ValueError("width must be non-negative")
    return (1 << width) - 1

# 取出 value 的第 index 位。
def get_bit(value: int, index: int) -> int:
    if index < 0:
        raise ValueError("index must be non-negative")
    return (value >> index) & 1

# 把一个整数拆成 bit 列表。默认是 低位在前。
def int_to_bits(value: int, width: int, *, msb_first: bool = False) -> list[int]:
    value &= bit_mask(width)
    bits = [(value >> i) & 1 for i in range(width)]
    if msb_first:
        bits.reverse()
    return bits

# 把 bit 列表重新打包成一个整数。默认认为输入列表是 低位在前。
def bits_to_int(bits: list[int], *, msb_first: bool = False) -> int:
    if msb_first:
        bits = list(reversed(bits))
    value = 0
    for i, bit in enumerate(bits):
        if bit & 1:
            value |= 1 << i
    return value

# 在固定 width 位内循环右移。
def rotate_right(value: int, shift: int, width: int) -> int:
    if width <= 0:
        raise ValueError("width must be positive")
    mask = bit_mask(width)
    shift %= width
    value &= mask
    return ((value >> shift) | (value << (width - shift))) & mask

# 在固定 width 位内循环左移。
def rotate_left(value: int, shift: int, width: int) -> int:
    if width <= 0:
        raise ValueError("width must be positive")
    mask = bit_mask(width)
    shift %= width
    value &= mask
    return ((value << shift) | (value >> (width - shift))) & mask
