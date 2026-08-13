"""
cong_topk_gpu.py

简化版 Cong oblivious Top-k 框架。

当前版本的目的：
1. 用公开的 n、k 生成 Cong 固定比较网络；
2. 输入是非回绕减法份额：value = share1 - share0；
3. 每一层把所有比较对批量交给 compare_and_swap_level()；
4. 当前 compare_and_swap_level() 只是本地正确性测试版本，不安全；
5. 后续安全比较和安全交换只需要修改这一个函数。

纯函数版本，不使用面向对象封装。
"""

from pathlib import Path
import sys

import numpy as np

try:
    import cupy as cp
except ImportError:
    cp = None

_ROOT = Path(__file__).resolve().parents[1]
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from gpu_common.no_wrap import (
    subtractive_reconstruct_no_wrap,
    subtractive_share_no_wrap,
)
from gpu_common.random_tools import make_rng


def get_array_module(array):
    """输入是 CuPy 数组时返回 cp，否则返回 np。"""
    if cp is not None and isinstance(array, cp.ndarray):
        return cp
    return np

# ============================================================
# 1. Cong 固定比较网络生成
# ============================================================


def parallel_levels(levels_a, levels_b):
    """两个互不相交的子网络按层并行拼接。"""
    depth = max(len(levels_a), len(levels_b))
    result = []

    for i in range(depth):
        level = []
        if i < len(levels_a):
            level.extend(levels_a[i])
        if i < len(levels_b):
            level.extend(levels_b[i])
        result.append(level)

    return result


def interleave(a, b):
    """生成 a[0], b[0], a[1], b[1], ..."""
    result = []
    length = max(len(a), len(b))

    for i in range(length):
        if i < len(a):
            result.append(a[i])
        if i < len(b):
            result.append(b[i])

    return result


def chunk_size(n, k):
    """Cong Algorithm 3 使用的公开分块大小。"""
    power_of_two = 1 << (k - 1).bit_length()

    if n <= power_of_two:
        return (n + 1) // 2

    return power_of_two * (
        (n + 2 * power_of_two - 1) // (2 * power_of_two)
    )


def build_merge_network(left_wires, right_wires, k):
    """
    Cong truncated odd-even merge。

    返回：
        output_wires: 合并后最小 k 个结果所在的 wire 编号
        levels:        每一层的公开比较位置 [(left, right), ...]
    """
    if k <= 0:
        return [], []

    if not left_wires or not right_wires:
        return (left_wires + right_wires)[:k], []

    if len(left_wires) == 1 and len(right_wires) == 1:
        output_wires = [left_wires[0], right_wires[0]][:k]
        levels = [[(left_wires[0], right_wires[0])]]
        return output_wires, levels

    even_output, even_levels = build_merge_network(
        left_wires[::2],
        right_wires[::2],
        k // 2 + 1,
    )

    odd_output, odd_levels = build_merge_network(
        left_wires[1::2],
        right_wires[1::2],
        k // 2,
    )

    levels = parallel_levels(even_levels, odd_levels)
    merged_wires = interleave(even_output, odd_output)

    # 本层中的比较互不冲突，可以在 GPU 上并行。
    final_level = []
    pair_count = (len(merged_wires) - 1) // 2

    for i in range(1, pair_count + 1):
        left = merged_wires[2 * i - 1]
        right = merged_wires[2 * i]
        final_level.append((left, right))

    if final_level:
        levels.append(final_level)

    return merged_wires[:k], levels


def build_sort_network(wires, k):
    """递归生成 Cong truncated odd-even Top-k 网络。"""
    n = len(wires)

    if n == 1:
        return wires.copy(), []

    split = chunk_size(n, k)

    left_output, left_levels = build_sort_network(wires[:split], k)
    right_output, right_levels = build_sort_network(wires[split:], k)

    # 左右递归子网络可以同时执行。
    levels = parallel_levels(left_levels, right_levels)

    output_wires, merge_levels = build_merge_network(
        left_output,
        right_output,
        k,
    )
    levels.extend(merge_levels)

    return output_wires, levels


def build_cong_network(n, k):
    """
    根据公开 n、k 生成 Cong 比较网络。

    返回：
        levels:       每层公开比较位置
        output_wires: 最终最小 k 个值所在的位置
    """
    if n <= 0:
        raise ValueError("n must be positive")
    if k <= 0 or k > n:
        raise ValueError("k must satisfy 1 <= k <= n")

    output_wires, levels = build_sort_network(list(range(n)), k)
    levels = [level for level in levels if level]

    # 检查同一层中一个位置不会参加两次比较。
    for depth, level in enumerate(levels):
        used = set()

        for left, right in level:
            if left == right or left in used or right in used:
                raise RuntimeError(
                    "wire conflict at level {}: ({}, {})".format(
                        depth, left, right
                    )
                )
            used.add(left)
            used.add(right)

    return levels, output_wires


def prepare_levels_for_gpu(levels, xp):
    """把公开比较位置提前转成 NumPy/CuPy 索引数组。"""
    prepared_levels = []

    for level in levels:
        left_pos = xp.asarray(
            [pair[0] for pair in level],
            dtype=xp.int64,
        )
        right_pos = xp.asarray(
            [pair[1] for pair in level],
            dtype=xp.int64,
        )
        prepared_levels.append((left_pos, right_pos))

    return prepared_levels


# ============================================================
# 2. 当前唯一需要被替换的比较与交换函数
# ============================================================


def compare_and_swap_level(share0, share1, left_pos, right_pos):
    """
    当前版本：非安全的本地正确性测试实现。

    这里同时完成：
    1. 比较本层所有 left_value 和 right_value；
    2. 当 left_value > right_value 时交换两边的份额；
    3. 保证较小值留在 left_pos。

    当前不安全的原因：
    - 在同一进程中恢复了参与比较的明文；
    - swap 是公开布尔数组；
    - 根据公开 swap 直接交换份额。

    后续接入安全协议时，只修改本函数：

        swap0, swap1 = secure_compare(...)
        share0, share1 = secure_swap(...)

    Cong 网络生成和主循环都不需要修改。
    """
    xp = get_array_module(share0)

    left0 = share0[..., left_pos].copy()
    left1 = share1[..., left_pos].copy()
    right0 = share0[..., right_pos].copy()
    right1 = share1[..., right_pos].copy()

    # -------------------- 当前临时明文比较 --------------------
    left_value = left1 - left0
    right_value = right1 - right0
    swap = left_value > right_value
    # --------------------------------------------------------

    # -------------------- 当前临时公开交换 --------------------
    share0[..., left_pos] = xp.where(swap, right0, left0)
    share1[..., left_pos] = xp.where(swap, right1, left1)

    share0[..., right_pos] = xp.where(swap, left0, right0)
    share1[..., right_pos] = xp.where(swap, left1, right1)
    # --------------------------------------------------------

    return share0, share1


# ============================================================
# 3. Cong Top-k 主循环
# ============================================================


def cong_topk_no_wrap(
    share0,
    share1,
    k,
    compare_swap_function=compare_and_swap_level,
    prepared_levels=None,
):
    """
    对非回绕减法份额执行 Cong Top-k。

    输入：
        share0, share1: shape (..., n)
        k:              公开 Top-k 参数
        compare_swap_function:
            每层批量比较交换函数；后续安全版本从这里传入

    输出：
        topk_share0, topk_share1
        levels, output_wires

    当前按升序输出最小 k 个值。
    """
    if share0.shape != share1.shape:
        raise ValueError("share0 and share1 must have the same shape")
    if share0.ndim == 0:
        raise ValueError("shares must have at least one dimension")

    xp = get_array_module(share0)
    n = int(share0.shape[-1])

    levels, output_wires = build_cong_network(n, k)

    if prepared_levels is None:
        prepared_levels = prepare_levels_for_gpu(levels, xp)

    # 避免修改调用者传入的原始份额。
    work0 = share0.copy()
    work1 = share1.copy()

    # Cong 的核心执行过程：每层只调用一次批量比较交换。
    for left_pos, right_pos in prepared_levels:
        work0, work1 = compare_swap_function(
            work0,
            work1,
            left_pos,
            right_pos,
        )

    output_pos = xp.asarray(output_wires, dtype=xp.int64)
    topk_share0 = work0[..., output_pos]
    topk_share1 = work1[..., output_pos]

    return topk_share0, topk_share1, levels, output_wires


# ============================================================
# 4. 正确性测试
# ============================================================


def self_test():
    rng = np.random.default_rng(20260716)

    # 论文结构校验点。
    levels, output_wires = build_cong_network(16, 3)
    comparator_count = sum(len(level) for level in levels)
    depth = len(levels)

    assert comparator_count == 35
    assert depth == 9

    # 不同 n、k 的随机正确性测试。
    for n in range(1, 65):
        for k in range(1, n + 1):
            values = rng.permutation(n).astype(np.uint64)
            values_gpu = cp.asarray(values, dtype=cp.int64)
            share0, share1 = subtractive_share_no_wrap(
                values_gpu,
                make_rng(20260716 + n * 100 + k),
            )

            topk0, topk1, _, _ = cong_topk_no_wrap(
                share0,
                share1,
                k,
            )

            result = cp.asnumpy(
                subtractive_reconstruct_no_wrap(topk0, topk1)
            )
            expected = np.sort(values)[:k]
            np.testing.assert_array_equal(result, expected)

    # 二维 batch 测试。
    values = np.array(
        [
            [9, 1, 7, 3, 5, 2],
            [8, 6, 4, 0, 7, 3],
        ],
        dtype=np.uint64,
    )
    values_gpu = cp.asarray(values, dtype=cp.int64)
    share0, share1 = subtractive_share_no_wrap(
        values_gpu,
        make_rng(20260716),
    )
    topk0, topk1, _, _ = cong_topk_no_wrap(share0, share1, 3)
    result = cp.asnumpy(
        subtractive_reconstruct_no_wrap(topk0, topk1)
    )
    expected = np.sort(values, axis=-1)[..., :3]
    np.testing.assert_array_equal(result, expected)

    print("self-test passed")
    print("n=16, k=3")
    print("comparators =", comparator_count)
    print("depth       =", depth)
    print("level sizes =", [len(level) for level in levels])
    print("output wires=", output_wires)


if __name__ == "__main__":
    self_test()
