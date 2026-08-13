# 矩阵型 Top-k 模块状态说明

日期：`2026-08-05`

本文用于把当前仓库里的“矩阵型 Top-k”方案收口整理，说明：

- 现有文件
- 当前实现内容
- 已完成部分
- 未完成部分
- 为什么当前不直接接入主路径
- 后续如果继续推进，应该从哪里接

## 1. 相关文件

当前矩阵型 Top-k 相关文件：

- `Machines/kona-matrix-topk.hpp`
- `Machines/kona-matrix-topk-test.cpp`

这两个文件当前都属于：

```text
独立实验模块
不接入 kona.cpp 主路径
```

## 2. 当前实现内容

### 2.1 已实现

`Machines/kona-matrix-topk.hpp` 当前已经实现：

1. 上三角 pair 生成
   - `build_upper_triangle_pairs(n)`

2. 秘密排名计算
   - `secure_matrix_rank_with_pcr(...)`

3. `rank < k` 的秘密 mask
   - `secure_topk_mask_from_rank_with_pcr(...)`

4. 不做交换、不重排数组，直接基于 mask 做 label vote
   - `matrix_topk_vote_with_pcr(...)`

### 2.2 当前语义

它走的是：

```text
distance 两两比较
-> 计算每个元素的秘密 rank
-> 计算 [rank < k]
-> 直接对被标记为 top-k 的元素做 label vote
```

也就是说：

```text
不交换位置
不构造排序网络
不把 top-k 元素搬到尾部
```

这和 `Cong` 路径明显不同。

## 3. 比较器接入情况

当前矩阵方案只接了：

```text
PCR comparator
```

对应接口在：

- `KonaPcrCompare::pcr_compare_gt_batch_l2(...)`
- `KonaPcrCompare::pcr_compare_lt_batch_l2(...)`

当前还没有：

- `DCF` 版本矩阵 Top-k
- 主路径后端切换版矩阵 Top-k

## 4. 测试状态

### 4.1 单文件编译

`kona-matrix-topk-test.cpp` 已经成功编译过。

### 4.2 双进程测试

当前测试结果处于：

```text
基本跑通
但尚未形成最终稳定“已完全通过”的交付状态
```

更准确地说：

- 在调试过程中，`P0` 已出现过 `MATRIX_TOPK_PCR PASS`
- 但后续由于双进程命令组织与日志抓取过程中的问题，未沉淀为一轮完全干净的最终确认记录

因此当前不应该把它记成：

```text
主路径已可用
```

而应记成：

```text
独立实验模块
逻辑主体已搭起
仍需最终双进程稳定确认
```

## 5. 为什么当前不直接接主路径

当前不建议直接把矩阵方案接进 `kona.cpp` 主路径，原因有三点。

### 5.1 还没有完成稳定双进程确认

如果一个模块还没有干净地完成：

- 编译
- 双进程运行
- 正确性确认
- 日志沉淀

就不适合直接接主路径。

### 5.2 它和 Kona 现有流程差异较大

当前 `Kona` / `Cong` 路径仍然保持：

```text
m_ESD_vec
-> compare / swap
-> 位置式 Top-k
-> 从末尾取 label
-> label_compute
-> winner 选择
```

而矩阵方案是：

```text
rank
-> topk_mask
-> 直接 vote
```

这意味着它不是“只换网络结构”，而是会改变：

- Top-k 输出形态
- 后续 label 处理方式

### 5.3 需要更完整的实验目标才能公平比较

矩阵方案真正的优势在：

- 不交换位置
- 不走排序网络
- pair 全并行

但也意味着比较次数是：

```text
O(n^2)
```

所以如果要和：

- `legacy-top1`
- `cong-pcr`
- `cong-dcf`

做公平对比，需要先明确：

- 比的是单 query 延迟
- 还是多 query 吞吐
- 还是 WAN 下的通信深度表现

在这些还没定清楚前，不宜贸然接主路径。

## 6. 当前建议定位

目前矩阵方案应定位为：

```text
独立实验模块
用于探索“比较次数换并行深度”的方案
```

而不是：

```text
主路径候选默认后端
```

## 7. 后续如果继续推进，建议顺序

如果以后要继续推进矩阵方案，建议按下面顺序来：

1. 先把 `kona-matrix-topk-test.cpp` 的双进程最终确认补完整
2. 再补 `DCF` 版矩阵比较器接入
3. 再做 `rank / topk_mask / label vote` 的 benchmark
4. 最后再考虑是否接主路径

## 8. 当前结论

一句话总结：

```text
矩阵型 Top-k 方案现在已经“做出来了”，
但还没有被收敛成主路径可切换后端；
当前最合适的状态是：
保留为独立实验模块，先不继续强接主路径。
```
