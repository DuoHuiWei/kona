# 接口测试与 Benchmark 说明清单

日期：`2026-07-24`

本文汇总当前仓库中新增或重要的接口测试、benchmark 和实验脚本，说明：

- 文件位置
- 作用
- 是否双进程
- 是否走 `kona.cpp` 主路径
- 测什么
- 当前状态

## 1. Share Conversion

### 1.1 组件

- `Machines/kona-share-conversion.hpp`

作用：

- 提供 L2 份额转换组件
- 包括：
  - additive / xor / subtractive 打开
  - `A2B`
  - `B2A`
  - bit 级转换
  - 复用 Kona 风格乘法

当前状态：

- 已实现
- 已在独立测试中验证过正确性与速度

### 1.2 测试入口

- `Machines/kona-share-conversion-test.cpp`

是否双进程：

- 是

是否主路径：

- 否

测试内容：

- `A2B_batch_l2`
- `B2A_batch_l2`
- `ABit2BBit_batch_l2`
- 正确性与速度

当前状态：

- 已通过

## 2. PCR Compare

### 2.1 组件

- `Machines/kona-pcr-compare.hpp`

作用：

- Kona 风格 `PCR` 比较器
- 可输出 `Z2<64>` 算术比较位份额
- 提供：
  - `pcr_compare_lt_batch_l2`
  - `pcr_compare_gt_batch_l2`
  - `pcr_compare_in_vec_l2`

当前状态：

- 已实现
- 已做：
  - sparse final-carry compare
  - active bitpack 压缩
  - 与 Kona `SS_vec` 风格对齐

注意：

- 当前语义仍带有 `|x-y| < 2^63` 的使用前提

### 2.2 active bitpack

- `Machines/kona-active-bitpack.hpp`

作用：

- 为 PCR sparse carry 树提供 endpoint bit 压缩/展开
- 被 PCR 模块复用

当前状态：

- 已实现
- 已通过专门测试

### 2.3 测试入口

- `Machines/kona-pcr-compare-test.cpp`

是否双进程：

- 是

是否主路径：

- 否

测试内容：

- `ACTIVE_BITPACK`
- `PCR_ADDER_UINT64`
- `PCR_COMPARE_SMALL`
- `PCR_COMPARE_BENCH`

当前状态：

- 已通过

## 3. DCF Compare

### 3.1 组件

- `Machines/kona-dcf-compare.hpp`

作用：

- 独立抽出的高性能 DCF comparator 模块
- 不直接依赖 `kona.cpp` 里的 compare 函数
- 提供：
  - `compare_in_vec(vector<Z2<K>>...)`
  - `compare_in_vec(vector<array<Z2<K>,2>>...)`

已做优化：

- key/r 预加载
- 独立模块化
- scratch 复用
- fast evaluate 内核
- batch evaluate 入口

当前状态：

- 已实现
- 已小规模验证正确性
- benchmark 结果已用于和 PCR 对照

## 4. PCR vs DCF 对照

### 4.1 benchmark 入口

- `Machines/kona-pcr-dcf-bench.cpp`

是否双进程：

- 是

是否主路径：

- 否

测试内容：

- 同一批 additive shares
- `PCR compare_in_vec + B2A`
- `DCF compare_in_vec`
- 输出：
  - 总时间
  - 平均时间
  - `ns_per_compare`
  - `sent_bytes`
  - `rounds`
  - `dcf_evaluate_calls`
  - `dcf_eval_total_ms`

当前状态：

- 已通过
- 已跑：
  - 无 netem
  - LAN
  - WAN
  - 多规模

### 4.2 四档自动实验脚本

- `Scripts/run-pcr-dcf-lan-bench.sh`

是否双进程：

- 是

是否主路径：

- 否

测试内容：

- 默认跑 `1000 / 4000 / 10000 / 20000`
- 每档执行：
  - `PCR`
  - `DCF`

当前状态：

- 已使用
- 当前命名虽含 `lan`，但如果外部先执行 `LAN.sh` / `WAN.sh`，也能在对应网络条件下运行

## 5. Cong 网络

### 5.1 组件

- `Machines/kona-cong-topk.hpp`

作用：

- Cong 网络生成
- 输出：
  - `levels`
  - `output_wires`
- 通用 `cong_top_k()` 调度器

当前状态：

- 已实现

### 5.2 网络测试

- `Machines/kona-cong-topk-test.cpp`

是否双进程：

- 否

是否主路径：

- 否

测试内容：

- `n=16,k=3` comparator 数与深度
- 明文 top-k 排序正确性

当前状态：

- 已通过

### 5.3 矩阵型 Top-k 模块

- `Machines/kona-matrix-topk.hpp`
- `Machines/kona-matrix-topk-test.cpp`

是否双进程：

- 模块：否
- 测试：是

是否主路径：

- 否

测试内容：

- 上三角 pair 生成
- 秘密 rank 计算
- `rank < k` 的秘密 mask
- 不交换位置、直接做 label vote

当前状态：

- 已实现为独立实验模块
- 当前不接 `kona.cpp` 主路径
- 详细说明见：
  - `docs/matrix_topk_module_status_zh.md`

## 6. Cong + Kona 适配层

### 6.1 组件

- `Machines/kona-cong-kona-adapter.hpp`

作用：

- 把 `Cong` 网络接到 Kona 风格数据结构上
- 当前直接支持：
  - `std::vector<std::array<Z2<K>,2>>`
- 提供：
  - `cong_top_k_with_pcr()`
  - `cong_top_k_with_dcf()`

设计目标：

- 尽量复用 Kona 风格 `SS_vec` 条件交换语义
- 只替换 Top-k 网络/比较后端

当前实现说明：

- 当前 `ss_vec_kona_l2()` 已回到更贴近原 Kona 的乘法形状：
  - 一次打包 `distance` 段与 `label` 段
  - 复用 `double_res` 风格的批量乘法
  - 不再拆成两次独立批量乘

这意味着当前 `Cong` 适配层在交换阶段更接近原 `Machines/kona.cpp` 中 `SS_vec()` 的实现风格。

### 6.2 适配测试

- `Machines/kona-cong-kona-test.cpp`

是否双进程：

- 是

是否主路径：

- 否

测试内容：

- `Cong + PCR`
- `Cong + DCF`
- 检查最小 `k` 个元素是否被搬到 Kona 习惯的尾部位置

当前状态：

- 已通过

## 7. 主路径接入

### 7.1 主路径文件

- `Machines/kona.cpp`

当前支持后端：

- `legacy-top1`
- `cong-pcr`
- `cong-dcf`

命令行参数：

```text
--topk-backend legacy-top1
--topk-backend cong-pcr
--topk-backend cong-dcf
```

默认值：

```text
legacy-top1
```

当前接入原则：

- `m_ESD_vec` 的生成方式不变
- `label_compute(...)` 语义不变
- 最终投票 winner 选择仍在主路径里完成
- 默认旧行为不变

### 7.2 主路径 benchmark 文档

- `docs/kona_mainpath_topk_backends_benchmark_zh.md`

内容：

- 三种后端主路径结果
- 默认行为说明
- 工程化程度说明

当前状态：

- 已完成

## 8. tcga-pancan 数据导入

### 8.1 导入脚本

- `Scripts/import_tcga_pancan_to_kona.py`

作用：

- 将：
  - `data.csv`
  - `labels.csv`

转换成 Kona 当前数据目录格式

输出目录：

- `Player-Data/Knn-Data/knn-1/tcga-pancan-data/`

输出文件：

- `Knn-meta`
- `P0-0-X-Train`
- `P0-0-Y-Train`
- `P1-0-X-Test`
- `P1-0-Y-Test`
- `label_map.txt`

当前状态：

- 已成功使用

## 9. 预处理与 ESD Benchmark

### 9.1 预处理 benchmark

- `Machines/kona-preprocess-bench.cpp`

是否双进程：

- 否

是否主路径：

- 否

复用原组件：

- `read_meta_data()`
- `gen_fake_dcf(...)`
- `generate_triples_save_file_optimized()`

测试内容：

- `meta_read_seconds`
- `dcf_prep_seconds`
- `triple_prep_seconds`
- `total_seconds`

当前状态：

- 已通过

### 9.2 ESD benchmark

- `Machines/kona-esd-bench.cpp`

是否双进程：

- 是

是否主路径：

- 否

复用原组件：

- `read_meta_and_P0_sample_P1_query()`
- `fake_load_triples()`
- `aby2_share_data_and_additive_share_label_list()`
- `compute_ESD_two_sample(...)`
- `compute_ESD_for_one_query(...)`

测试内容：

- `read_seconds`
- `fake_triple_load_seconds`
- `share_setup_seconds`
- `compute_one_query_seconds`
- `compute_all_pairs_loop_seconds`
- `pair_ns`
- `checksum_limb0`

当前状态：

- 已通过

### 9.3 Beaver 风格 ESD benchmark

- `Machines/kona-beaver-esd-bench.cpp`

是否双进程：

- 是

是否主路径：

- 否

复用原组件思路：

- `read_meta_and_P0_sample_P1_query()`
- `additive_share_all_data()` 同型路径
- 两方份额先本地做 `x_share - y_share`
- 再复用现有 `mul_vector_additive_kona_l2()` 做平方项乘法

测试内容：

- `read_seconds`
- `share_setup_seconds`
- `beaver_pair_batched_one_query_seconds`
- `beaver_query_batched_one_query_seconds`
- `beaver_pair_batched_all_pairs_loop_seconds`
- `beaver_pair_batched_pair_ns`
- `checksum_pair_batched_limb0`
- `checksum_query_batched_limb0`

说明：

- 这个 benchmark 不走当前 Kona 优化版 `aby2 + 预生成平方项`
- 它更像“直接用 Beaver 风格乘法做欧氏距离”的独立对照
- 当前同时包含两种批量粒度：
  - `pair_batched`
  - `query_batched`

当前状态：

- 已实现
- `pair_batched` 已完整运行
- `pair_comm_coalesced` 在 `tcga-pancan` 全量规模下已验证为当前实现不可行

### 9.4 传统 Beaver 三元组预生成 benchmark

- `Machines/kona-beaver-triple-prep-bench.cpp`

是否双进程：

- 是

是否主路径：

- 否

测试内容：

- `triples_per_pair`
- `num_pairs`
- `total_triples`
- `single_pair_gen_seconds`
- `all_pairs_gen_seconds`
- `triples_per_second`
- `sent_bytes`
- `rounds`
- `checksum_limb0`

说明：

- 不测 ESD 在线速度
- 只测“按当前数据集 pair 消耗量，预先生成普通 `[a,b,c]` 三元组”的速度
- 当前按：
  - `800` 个 pair
  - 每个 pair `20531` 个 triple

当前状态：

- 已实现
- 已完整运行

## 10. 推荐使用顺序

如果后续要做实验，我建议优先按这个顺序：

1. 数据导入：
   - `Scripts/import_tcga_pancan_to_kona.py`
2. 预处理/距离核：
   - `kona-preprocess-bench.cpp`
   - `kona-esd-bench.cpp`
3. 比较器对照：
   - `kona-pcr-dcf-bench.cpp`
4. 主路径验证：
   - `kona.cpp` + `--topk-backend`
5. 网络条件实验：
   - 先执行 `LAN.sh` / `WAN.sh`
   - 再执行相应 benchmark

## 11. 当前清单结论

当前代码库已经不再是“只有一个主程序”，而是形成了：

```text
独立组件
独立测试
独立benchmark
主路径可切换后端
数据导入工具
网络实验脚本
```

也就是说，现在已经具备了一个较完整的实验工程骨架。

后续最大的问题不再是“缺入口”，而是：

- 如何继续统一实验矩阵
- 如何做更多 query / 更多数据集的回归
- 如何把 DCF/PCR/Cong 的实验结果沉淀成更稳定的报告
