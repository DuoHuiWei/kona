"""Explicit Beaver triple helpers for arithmetic and Boolean shares.

Version 1 uses naked tuples:
    Share = (s0, s1)
    Arithmetic triple = (a_share, b_share, c_share), c = a*b mod M
    Boolean triple = (a_share, b_share, c_share), c = a&b over packed bits

Naming rule in this file:
    arithmetic_*_zmod      -> additive shares over Z_M
    boolean_*_bitset       -> XOR shares over packed Python-int bitsets
"""

from __future__ import annotations
from typing import TypeAlias
from shares import Share
import random
from shares import bit_mask
from stats import Channel

ArithmeticTriple: TypeAlias = tuple[Share, Share, Share]
BooleanTriple: TypeAlias = tuple[Share, Share, Share]


# ============================================================
# Arithmetic Beaver triples over Z_mod, additive sharing
# ============================================================

# 整数三元组生成
def arithmetic_gen_beaver_triple_zmod(mod: int) -> ArithmeticTriple:
    """Generate one additive-share Beaver triple over Z_mod."""
    # Missing protocol types: subtractive-domain triples and field-specific triples such as Z_2 / Z_{2^l} named wrappers.
    a = random.randrange(mod)
    b = random.randrange(mod)
    c = (a * b) % mod

    a0 = random.randrange(mod)
    b0 = random.randrange(mod)
    c0 = random.randrange(mod)
    a_share = (a0, (a - a0) % mod)
    b_share = (b0, (b - b0) % mod)
    c_share = (c0, (c - c0) % mod)
    return a_share, b_share, c_share

# list三元组批量生成-pool
def arithmetic_gen_beaver_pool_zmod(count: int, mod: int) -> list[ArithmeticTriple]:
    """Generate a list pool of additive-share Beaver triples over Z_mod."""
    # Missing protocol types: separate pools for arithmetic Z_{2^l}, arithmetic prime field, and Boolean Z_2 when needed.
    if count < 0:
        raise ValueError("count must be non-negative")
    return [arithmetic_gen_beaver_triple_zmod(mod) for _ in range(count)]

# 使用三元组，出池子
def arithmetic_pop_beaver_triple_zmod(pool: list[ArithmeticTriple]) -> ArithmeticTriple:
    """Pop one arithmetic Beaver triple from a naked list pool."""
    # Missing protocol type: typed pool dispatch by domain, for example arithmetic_z2l vs boolean_bitset.
    if not pool:
        raise RuntimeError("arithmetic Beaver triple pool exhausted")
    return pool.pop()


# 暂时不开放批量取三元组接口。
# 原因：当前还没有实现 list 批量 Beaver 乘法；提前暴露批量出池函数容易误导。
# 等实现 arithmetic_mul_beaver_zmod_list(...) 后，再恢复这个函数。
# def arithmetic_pop_beaver_triples_zmod(pool: list[ArithmeticTriple], count: int) -> list[ArithmeticTriple]:
#     if count < 0:
#         raise ValueError("count must be non-negative")
#     if len(pool) < count:
#         raise RuntimeError("not enough arithmetic Beaver triples in pool")
#     return [arithmetic_pop_beaver_triple_zmod(pool) for _ in range(count)]


# Backward-compatible aliases. Prefer the explicit names above in new code.
gen_beaver_triple = arithmetic_gen_beaver_triple_zmod
gen_beaver_pool = arithmetic_gen_beaver_pool_zmod
pop_triple = arithmetic_pop_beaver_triple_zmod


# ============================================================
# Arithmetic opening over Z_mod, additive sharing
# ============================================================

# 公开广播合成份额
def arithmetic_open_additive_share_zmod(
    share: Share,
    mod: int,
    channel: Channel | None = None,
    *,
    bits: int | None = None,
    tag: str = "arithmetic_open_zmod",
) -> int:
    """Open one additive share over Z_mod and optionally record one round."""
    # Missing protocol functions: subtractive-share open and XOR-share open are separate functions, not this one.
    if channel is not None:
        bit_len = bits if bits is not None else max(1, mod.bit_length())
        channel.send("S0", "S1", share[0], bit_len, tag)
        channel.send("S1", "S0", share[1], bit_len, tag)
        channel.next_round()
    return (share[0] + share[1]) % mod

# 公开广播合成份额，合成两个
def arithmetic_open_two_additive_shares_zmod_same_round(
    left: Share,
    right: Share,
    mod: int,
    channel: Channel | None = None,
    *,
    bits: int | None = None,
    tag: str = "arithmetic_open_pair_zmod",
) -> tuple[int, int]:
    """Open two additive shares over Z_mod in the same communication round."""
    # Missing protocol function: batch open for arithmetic shares; this only opens two masks for one multiplication.
    if channel is not None:
        bit_len = bits if bits is not None else max(1, mod.bit_length())
        channel.send("S0", "S1", left[0], bit_len, f"{tag}:left")
        channel.send("S1", "S0", left[1], bit_len, f"{tag}:left")
        channel.send("S0", "S1", right[0], bit_len, f"{tag}:right")
        channel.send("S1", "S0", right[1], bit_len, f"{tag}:right")
        channel.next_round()
    return (left[0] + left[1]) % mod, (right[0] + right[1]) % mod


# Backward-compatible alias. Prefer arithmetic_open_additive_share_zmod.
open_additive = arithmetic_open_additive_share_zmod


# ============================================================
# Arithmetic Beaver multiplication over Z_mod
# ============================================================

# 算数三元组安全乘法
def additive_mul_beaver_zmod(
    x_share: Share,
    y_share: Share,
    pool: list[ArithmeticTriple],
    mod: int,
    channel: Channel | None = None,
    *,
    bits: int | None = None,
) -> Share:
    """Securely multiply two additive shares over Z_mod with Beaver triples."""
    # Missing protocol functions: subtractive-share Beaver multiplication and batch/list arithmetic multiplication.
    a_share, b_share, c_share = arithmetic_pop_beaver_triple_zmod(pool)
    alpha_share = ((x_share[0] - a_share[0]) % mod, (x_share[1] - a_share[1]) % mod)
    beta_share = ((y_share[0] - b_share[0]) % mod, (y_share[1] - b_share[1]) % mod)

    alpha, beta = arithmetic_open_two_additive_shares_zmod_same_round(
        alpha_share,
        beta_share,
        mod,
        channel,
        bits=bits,
        tag="arithmetic_beaver_masks_zmod",
    )

    # Formula: xy = c + alpha*b + beta*a + alpha*beta.
    # Shares: put public alpha*beta on S0 only, otherwise it is added twice.
    z0 = (c_share[0] + alpha * b_share[0] + beta * a_share[0] + alpha * beta) % mod
    z1 = (c_share[1] + alpha * b_share[1] + beta * a_share[1]) % mod
    return z0, z1


# Backward-compatible aliases. Prefer additive_mul_beaver_zmod.
arithmetic_mul_beaver_zmod = additive_mul_beaver_zmod
beaver_mul = additive_mul_beaver_zmod


# ============================================================
# Boolean Beaver triples over packed bitsets, XOR sharing
# ============================================================

# 布尔三元组生成
def boolean_gen_beaver_triple_bitset(width: int) -> BooleanTriple:
    """Generate one XOR-share Boolean Beaver triple over packed bitsets."""
    # Missing protocol types: scalar single-bit Z_2 triples and multi-bit packed bitset triples are currently represented by the same bitset function.
    mask = bit_mask(width)
    a = random.getrandbits(width) & mask
    b = random.getrandbits(width) & mask
    c = a & b

    a0 = random.getrandbits(width) & mask
    b0 = random.getrandbits(width) & mask
    c0 = random.getrandbits(width) & mask
    a_share = (a0, a ^ a0)
    b_share = (b0, b ^ b0)
    c_share = (c0, c ^ c0)
    return a_share, b_share, c_share

# 布尔三元组 pool池子生成
def boolean_gen_beaver_pool_bitset(count: int, width: int) -> list[BooleanTriple]:
    """Generate a list pool of XOR-share Boolean Beaver triples."""
    # Missing protocol types: explicit pools for scalar Boolean AND and packed-bitset Boolean AND if we later split them.
    if count < 0:
        raise ValueError("count must be non-negative")
    return [boolean_gen_beaver_triple_bitset(width) for _ in range(count)]

# 布尔三元组出池
def boolean_pop_beaver_triple_bitset(pool: list[BooleanTriple]) -> BooleanTriple:
    """Pop one Boolean Beaver triple from a naked list pool."""
    # Missing protocol type: typed Boolean pool dispatch, for example boolean_scalar_z2 vs boolean_packed_bitset.
    if not pool:
        raise RuntimeError("Boolean Beaver triple pool exhausted")
    return pool.pop()


# 暂时不开放批量取三元组接口。
# 原因：当前还没有实现 list 批量 Boolean Beaver AND；提前暴露批量出池函数容易误导。
# 等实现 boolean_and_beaver_bitset_list(...) 后，再恢复这个函数。
# def boolean_pop_beaver_triples_bitset(pool: list[BooleanTriple], count: int) -> list[BooleanTriple]:
#     if count < 0:
#         raise ValueError("count must be non-negative")
#     if len(pool) < count:
#         raise RuntimeError("not enough Boolean Beaver triples in pool")
#     return [boolean_pop_beaver_triple_bitset(pool) for _ in range(count)]


# ============================================================
# Boolean opening over packed bitsets, XOR sharing
# ============================================================

# 布尔异或公开通信
def boolean_open_xor_share_bitset(
    share: Share,
    width: int,
    channel: Channel | None = None,
    *,
    tag: str = "boolean_open_bitset",
) -> int:
    """Open one XOR share over packed bitsets and optionally record one round."""
    # Missing protocol function: scalar Boolean open can be a width=1 wrapper; batch Boolean open is not implemented.
    if channel is not None:
        channel.send("S0", "S1", share[0], width, tag)
        channel.send("S1", "S0", share[1], width, tag)
        channel.next_round()
    return (share[0] ^ share[1]) & bit_mask(width)

# 布尔异或公开通信
def boolean_open_two_xor_shares_bitset_same_round(
    left: Share,
    right: Share,
    width: int,
    channel: Channel | None = None,
    *,
    tag: str = "boolean_open_pair_bitset",
) -> tuple[int, int]:
    """Open two XOR shares over packed bitsets in the same communication round."""
    # Missing protocol function: batch open for Boolean circuits; this only opens d/e for one AND gate batch packed in one int.
    if channel is not None:
        channel.send("S0", "S1", left[0], width, f"{tag}:left")
        channel.send("S1", "S0", left[1], width, f"{tag}:left")
        channel.send("S0", "S1", right[0], width, f"{tag}:right")
        channel.send("S1", "S0", right[1], width, f"{tag}:right")
        channel.next_round()
    mask = bit_mask(width)
    return (left[0] ^ left[1]) & mask, (right[0] ^ right[1]) & mask


# ============================================================
# Boolean Beaver multiplication over packed bitsets
# ============================================================

# 布尔安全乘法 AND
def boolean_and_beaver_bitset(
    x_share: Share,
    y_share: Share,
    pool: list[BooleanTriple],
    width: int,
    channel: Channel | None = None,
) -> Share:
    """Secure Boolean multiplication: XOR-shared bitset AND via Beaver."""
    # Missing protocol functions: secure OR, NAND, NOT+AND compositions, and list/batch Boolean circuit wrappers.
    mask = bit_mask(width)
    a_share, b_share, c_share = boolean_pop_beaver_triple_bitset(pool)
    d_share = ((x_share[0] ^ a_share[0]) & mask, (x_share[1] ^ a_share[1]) & mask)
    e_share = ((y_share[0] ^ b_share[0]) & mask, (y_share[1] ^ b_share[1]) & mask)

    d_open, e_open = boolean_open_two_xor_shares_bitset_same_round(
        d_share,
        e_share,
        width,
        channel,
        tag="boolean_beaver_masks_bitset",
    )

    # Formula: x&y = c ^ (d&b) ^ (e&a) ^ (d&e).
    # Shares: put public d&e on S0 only, otherwise it is XORed twice and cancels.
    z0 = (c_share[0] ^ (d_open & b_share[0]) ^ (e_open & a_share[0]) ^ (d_open & e_open)) & mask
    z1 = (c_share[1] ^ (d_open & b_share[1]) ^ (e_open & a_share[1])) & mask
    return z0, z1


# Alias for users who think of Boolean AND as multiplication over bits.
boolean_mul_beaver_bitset = boolean_and_beaver_bitset




