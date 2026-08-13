"""
pcr_final_carry_reduce_uint64.py

64 位最终进位比较器：uint64 稀疏归约树版
===============================================

目标
----
比较两个无回绕减法秘密共享值：

    x = x1 - x0
    y = y1 - y0

输出 XOR Boolean shares：

    lt = lt0 XOR lt1
    lt = 1  <=>  x < y

与全前缀版本的区别
------------------
本版本不计算每个位置的前缀 G/P，也不生成完整 carry 向量。

归约树只保留下列活动节点：

    64 -> 32 -> 16 -> 8 -> 4 -> 2 -> 1

每层只计算相邻两个区间的父节点。活动节点分别位于：

    distance=1 : bit 1,3,5,...,63
    distance=2 : bit 3,7,11,...,63
    distance=4 : bit 7,15,...,63
    ...
    distance=32: bit 63

为了直接得到最终 carry-out，将减法中的初始 carry-in=1
嵌入最低位 generate：

    G_0 <- G_0 OR P_0 = G_0 XOR P_0

因此根节点 G 就是 carry64，不再需要计算根节点 P。

Boolean AND 数量（按单个比较的逻辑 bit 计）
------------------------------------------

    初始化 G                     : 64
    前五层 G/P 归约             : 2*(32+16+8+4+2)
    最后一层只计算根节点 G       : 1

    总计                         : 189

当前实现仍是单进程双服务器模拟器：kernel 内直接打开 d/e。
真实双服务器部署时，应按每一层拆成 prepare / exchange / finalize。
"""

from __future__ import annotations

from pathlib import Path
import sys

import numpy as np
import cupy as cp

_ROOT = Path(__file__).resolve().parents[1]
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from gpu_common.boolean_shares import reconstruct_xor as reconstruct_bool
from gpu_common.no_wrap import (
    subtractive_reconstruct_no_wrap,
    subtractive_share_no_wrap,
)
from gpu_common.random_tools import make_rng, random_u64


BIT_LENGTH = 64
SIGN_MASK = cp.uint64(1 << 63)
FULL_MASK_INT = (1 << 64) - 1

# slot 0: 初始化 G
# slots 1..10: distance 1,2,4,8,16，每层两个 AND
# slot 11: distance 32，只计算根节点 G
TRIPLE_SLOTS = 12
GP_DISTANCES = (1, 2, 4, 8, 16)
FINAL_DISTANCE = 32


def endpoint_mask(distance: int) -> int:
    """
    返回当前归约层父节点所在 bit 的掩码。

    distance=1  -> bits 1,3,5,...,63
    distance=2  -> bits 3,7,11,...,63
    ...
    distance=32 -> bit 63
    """
    if distance <= 0 or distance > 32 or distance & (distance - 1):
        raise ValueError("distance 必须是 1,2,4,8,16,32 之一")

    mask = 0
    step = distance * 2
    for bit in range(step - 1, BIT_LENGTH, step):
        mask |= 1 << bit
    return mask


def slot_masks() -> tuple[int, ...]:
    """每个 Beaver triple slot 对应的活动 bit 掩码。"""
    masks = [FULL_MASK_INT]
    for distance in GP_DISTANCES:
        mask = endpoint_mask(distance)
        masks.extend((mask, mask))
    masks.append(endpoint_mask(FINAL_DISTANCE))
    assert len(masks) == TRIPLE_SLOTS
    return tuple(masks)


# ============================================================
# 1. 无回绕减法秘密分享
# ============================================================


# ============================================================
# 2. 离线 packed Boolean Beaver triples
# ============================================================


def generate_triples_uint64(n: int, rng):
    """
    生成本次比较所需的全部 packed Boolean triples。

    返回六个数组，shape 均为：

        (TRIPLE_SLOTS, n)

    不同 slot 只在对应归约层的活动 bit 上包含随机三元组，
    非活动 bit 全部置零，避免把全前缀图中的无效逻辑节点
    带入当前归约图。
    """
    if n < 0:
        raise ValueError("n 不能为负数")

    shape = (TRIPLE_SLOTS, n)
    masks = cp.asarray(slot_masks(), dtype=cp.uint64).reshape(TRIPLE_SLOTS, 1)

    a = random_u64(shape, rng) & masks
    b = random_u64(shape, rng) & masks
    c = a & b

    a0 = random_u64(shape, rng) & masks
    b0 = random_u64(shape, rng) & masks
    c0 = random_u64(shape, rng) & masks

    a1 = a ^ a0
    b1 = b ^ b0
    c1 = c ^ c0

    return a0, a1, b0, b1, c0, c1


# ============================================================
# 3. CUDA kernels
# ============================================================

CUDA_SOURCE = r"""
__device__ __forceinline__
void beaver_and64(
    unsigned long long x0,
    unsigned long long x1,
    unsigned long long y0,
    unsigned long long y1,
    unsigned long long a0,
    unsigned long long a1,
    unsigned long long b0,
    unsigned long long b1,
    unsigned long long c0,
    unsigned long long c1,
    unsigned long long* z0,
    unsigned long long* z1)
{
    // 本地双服务器模拟：真实部署时 d/e 必须由两方交换后打开。
    const unsigned long long d = (x0 ^ a0) ^ (x1 ^ a1);
    const unsigned long long e = (y0 ^ b0) ^ (y1 ^ b1);

    *z0 = c0 ^ (d & b0) ^ (e & a0) ^ (d & e);
    *z1 = c1 ^ (d & b1) ^ (e & a1);
}


extern "C" __global__
void init_pg_reduce_uint64(
    const unsigned long long* left,
    const unsigned long long* right_not,
    const unsigned long long* ta0,
    const unsigned long long* ta1,
    const unsigned long long* tb0,
    const unsigned long long* tb1,
    const unsigned long long* tc0,
    const unsigned long long* tc1,
    unsigned long long* g0,
    unsigned long long* g1,
    unsigned long long* p0,
    unsigned long long* p1,
    const unsigned long long n)
{
    const unsigned long long i =
        (unsigned long long)blockIdx.x * blockDim.x + threadIdx.x;

    if (i >= n) {
        return;
    }

    // left = 0 XOR left，~right = right_not XOR 0。
    const unsigned long long A0 = 0ULL;
    const unsigned long long A1 = left[i];
    const unsigned long long B0 = right_not[i];
    const unsigned long long B1 = 0ULL;

    const unsigned long long P0 = A0 ^ B0;
    const unsigned long long P1 = A1 ^ B1;

    unsigned long long G0, G1;
    beaver_and64(
        A0, A1,
        B0, B1,
        ta0[i], ta1[i],
        tb0[i], tb1[i],
        tc0[i], tc1[i],
        &G0, &G1
    );

    // 将减法中的初始 carry-in=1 嵌入最低位：
    // G_0 <- G_0 OR P_0 = G_0 XOR P_0。
    G0 ^= (P0 & 1ULL);
    G1 ^= (P1 & 1ULL);

    g0[i] = G0;
    g1[i] = G1;
    p0[i] = P0;
    p1[i] = P1;
}


extern "C" __global__
void reduce_stage_gp_uint64(
    const unsigned long long* old_g0,
    const unsigned long long* old_g1,
    const unsigned long long* old_p0,
    const unsigned long long* old_p1,

    const unsigned long long* t_a0,
    const unsigned long long* t_a1,
    const unsigned long long* t_b0,
    const unsigned long long* t_b1,
    const unsigned long long* t_c0,
    const unsigned long long* t_c1,

    const unsigned long long* u_a0,
    const unsigned long long* u_a1,
    const unsigned long long* u_b0,
    const unsigned long long* u_b1,
    const unsigned long long* u_c0,
    const unsigned long long* u_c1,

    unsigned long long* new_g0,
    unsigned long long* new_g1,
    unsigned long long* new_p0,
    unsigned long long* new_p1,

    const unsigned int distance,
    const unsigned long long active_mask,
    const unsigned long long n)
{
    const unsigned long long i =
        (unsigned long long)blockIdx.x * blockDim.x + threadIdx.x;

    if (i >= n) {
        return;
    }

    const unsigned long long G0 = old_g0[i];
    const unsigned long long G1 = old_g1[i];
    const unsigned long long P0 = old_p0[i];
    const unsigned long long P1 = old_p1[i];

    // 当前层只保留父区间的高端点。
    const unsigned long long high_G0 = G0 & active_mask;
    const unsigned long long high_G1 = G1 & active_mask;
    const unsigned long long high_P0 = P0 & active_mask;
    const unsigned long long high_P1 = P1 & active_mask;

    // 将相邻低区间的端点左移到父节点位置。
    const unsigned long long low_G0 = (G0 << distance) & active_mask;
    const unsigned long long low_G1 = (G1 << distance) & active_mask;
    const unsigned long long low_P0 = (P0 << distance) & active_mask;
    const unsigned long long low_P1 = (P1 << distance) & active_mask;

    unsigned long long T0, T1;
    unsigned long long U0, U1;

    // 父 G = G_high OR (P_high AND G_low)。
    beaver_and64(
        high_P0, high_P1,
        low_G0, low_G1,
        t_a0[i], t_a1[i],
        t_b0[i], t_b1[i],
        t_c0[i], t_c1[i],
        &T0, &T1
    );

    // 父 P = P_high AND P_low。
    beaver_and64(
        high_P0, high_P1,
        low_P0, low_P1,
        u_a0[i], u_a1[i],
        u_b0[i], u_b1[i],
        u_c0[i], u_c1[i],
        &U0, &U1
    );

    // G_high 与 T 在明文上互斥，因此 OR 可写为 XOR。
    new_g0[i] = (high_G0 ^ T0) & active_mask;
    new_g1[i] = (high_G1 ^ T1) & active_mask;
    new_p0[i] = U0 & active_mask;
    new_p1[i] = U1 & active_mask;
}


extern "C" __global__
void final_reduce_lt_uint64(
    const unsigned long long* old_g0,
    const unsigned long long* old_g1,
    const unsigned long long* old_p0,
    const unsigned long long* old_p1,

    const unsigned long long* t_a0,
    const unsigned long long* t_a1,
    const unsigned long long* t_b0,
    const unsigned long long* t_b1,
    const unsigned long long* t_c0,
    const unsigned long long* t_c1,

    unsigned long long* lt0,
    unsigned long long* lt1,
    const unsigned long long n)
{
    const unsigned long long i =
        (unsigned long long)blockIdx.x * blockDim.x + threadIdx.x;

    if (i >= n) {
        return;
    }

    const unsigned long long ROOT_MASK = 0x8000000000000000ULL;

    // distance=32：高半区节点在 bit63，低半区节点在 bit31。
    const unsigned long long high_G0 = old_g0[i] & ROOT_MASK;
    const unsigned long long high_G1 = old_g1[i] & ROOT_MASK;
    const unsigned long long high_P0 = old_p0[i] & ROOT_MASK;
    const unsigned long long high_P1 = old_p1[i] & ROOT_MASK;
    const unsigned long long low_G0 = (old_g0[i] << 32) & ROOT_MASK;
    const unsigned long long low_G1 = (old_g1[i] << 32) & ROOT_MASK;

    unsigned long long T0, T1;
    beaver_and64(
        high_P0, high_P1,
        low_G0, low_G1,
        t_a0[i], t_a1[i],
        t_b0[i], t_b1[i],
        t_c0[i], t_c1[i],
        &T0, &T1
    );

    // 根节点只需要 G，不计算根节点 P。
    const unsigned long long root_G0 = high_G0 ^ T0;
    const unsigned long long root_G1 = high_G1 ^ T1;

    const unsigned long long carry0 = (root_G0 >> 63) & 1ULL;
    const unsigned long long carry1 = (root_G1 >> 63) & 1ULL;

    // left < right <=> NOT carry64；只翻转 S0 份额。
    lt0[i] = carry0 ^ 1ULL;
    lt1[i] = carry1;
}
"""


INIT_KERNEL = cp.RawKernel(
    CUDA_SOURCE,
    "init_pg_reduce_uint64",
    options=("-std=c++11",),
)

REDUCE_GP_KERNEL = cp.RawKernel(
    CUDA_SOURCE,
    "reduce_stage_gp_uint64",
    options=("-std=c++11",),
)

FINAL_KERNEL = cp.RawKernel(
    CUDA_SOURCE,
    "final_reduce_lt_uint64",
    options=("-std=c++11",),
)


# ============================================================
# 4. 在线最终进位比较
# ============================================================


def _as_i64_vector(value):
    return cp.ascontiguousarray(cp.asarray(value, dtype=cp.int64).ravel())


def _validate_inputs(x0, x1, y0, y1):
    sizes = {int(x0.size), int(x1.size), int(y0.size), int(y1.size)}
    if len(sizes) != 1:
        raise ValueError("x0、x1、y0、y1 的元素数量必须一致")


def _validate_triples_uint64(triples, n: int):
    if not isinstance(triples, (tuple, list)) or len(triples) != 6:
        raise ValueError("triples 必须包含 a0,a1,b0,b1,c0,c1 六个数组")
    expected = (TRIPLE_SLOTS, n)
    for array in triples:
        if tuple(array.shape) != expected:
            raise ValueError(f"triple shape 应为 {expected}，实际为 {array.shape}")


def compare_subtractive_uint64(x0, x1, y0, y1, triples):
    """
    输入：
        x = x1 - x0
        y = y1 - y0

    输出：
        lt0, lt1

    重构：
        lt = lt0 XOR lt1
        lt=1 表示 x<y。

    前提：
        left_signed=x1-y1 与 right_signed=x0-y0 均不能发生 int64 溢出。
    """
    x0 = _as_i64_vector(x0)
    x1 = _as_i64_vector(x1)
    y0 = _as_i64_vector(y0)
    y1 = _as_i64_vector(y1)
    _validate_inputs(x0, x1, y0, y1)

    n = int(x0.size)
    if n == 0:
        empty = cp.empty(0, dtype=cp.uint64)
        return empty, empty.copy()

    _validate_triples_uint64(triples, n)

    # x<y <=> (x1-y1) < (x0-y0)。
    left_signed = x1 - y1
    right_signed = x0 - y0

    # signed int64 -> 保序 uint64。
    left = left_signed.view(cp.uint64) ^ SIGN_MASK
    right = right_signed.view(cp.uint64) ^ SIGN_MASK

    # 计算 left + (~right) + 1 的最终 carry-out。
    right_not = ~right

    a0, a1, b0, b1, c0, c1 = triples

    g0 = cp.empty(n, dtype=cp.uint64)
    g1 = cp.empty(n, dtype=cp.uint64)
    p0 = cp.empty(n, dtype=cp.uint64)
    p1 = cp.empty(n, dtype=cp.uint64)

    threads = 256
    blocks = (n + threads - 1) // threads

    INIT_KERNEL(
        (blocks,),
        (threads,),
        (
            left,
            right_not,
            a0[0], a1[0],
            b0[0], b1[0],
            c0[0], c1[0],
            g0, g1,
            p0, p1,
            np.uint64(n),
        ),
    )

    slot = 1
    for distance in GP_DISTANCES:
        ng0 = cp.empty_like(g0)
        ng1 = cp.empty_like(g1)
        np0 = cp.empty_like(p0)
        np1 = cp.empty_like(p1)

        REDUCE_GP_KERNEL(
            (blocks,),
            (threads,),
            (
                g0, g1,
                p0, p1,

                a0[slot], a1[slot],
                b0[slot], b1[slot],
                c0[slot], c1[slot],

                a0[slot + 1], a1[slot + 1],
                b0[slot + 1], b1[slot + 1],
                c0[slot + 1], c1[slot + 1],

                ng0, ng1,
                np0, np1,

                np.uint32(distance),
                np.uint64(endpoint_mask(distance)),
                np.uint64(n),
            ),
        )

        g0, g1 = ng0, ng1
        p0, p1 = np0, np1
        slot += 2

    assert slot == TRIPLE_SLOTS - 1

    lt0 = cp.empty(n, dtype=cp.uint64)
    lt1 = cp.empty(n, dtype=cp.uint64)

    FINAL_KERNEL(
        (blocks,),
        (threads,),
        (
            g0, g1,
            p0, p1,

            a0[slot], a1[slot],
            b0[slot], b1[slot],
            c0[slot], c1[slot],

            lt0, lt1,
            np.uint64(n),
        ),
    )

    return lt0, lt1


# ============================================================
# 5. 简单测试
# ============================================================

if __name__ == "__main__":
    share_rng = make_rng(100)
    triple_rng = make_rng(200)

    x = cp.array(
        [5, 10, 7, 0, 2**30, 9, -5, -9, 2**40],
        dtype=cp.int64,
    )
    y = cp.array(
        [8, 3, 7, 1, 2**30 - 1, 9, -6, -2, 2**40 + 1],
        dtype=cp.int64,
    )

    x0, x1 = subtractive_share_no_wrap(x, share_rng)
    y0, y1 = subtractive_share_no_wrap(y, share_rng)

    triples = generate_triples_uint64(int(x.size), triple_rng)

    lt0, lt1 = compare_subtractive_uint64(
        x0, x1, y0, y1, triples
    )

    result = reconstruct_bool(lt0, lt1)
    expected = (x < y).astype(cp.uint64)

    print("x      =", cp.asnumpy(x))
    print("y      =", cp.asnumpy(y))
    print("结果   =", cp.asnumpy(result))
    print("期望   =", cp.asnumpy(expected))

    cp.testing.assert_array_equal(result, expected)
    print("\nuint64 稀疏最终进位归约树：测试通过")
