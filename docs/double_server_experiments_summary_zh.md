# 双服务器安全计算实验总览（Kona / Cong / PCR / DCF / ESD / Triple）

日期：`2026-08-04`

本文汇总本次工作中**所有已经实际做过的双服务器相关实验**，不包含同态加密（HE）方案。

目标是统一回答以下问题：

- 用了什么数据集，数据维度是多少
- 用了什么 Top-k 网络
- 用了什么比较器
- 比较/交换是串行还是并行
- 用了哪些加速方案
- 跑过哪些双进程 benchmark
- 实际测出来了什么结果

---

## 1. 全局说明

### 1.1 双服务器含义

本文中的“双服务器”统一指：

```text
两方双进程
通过 RealTwoPartyPlayer / octetStream / send-receive
在同一台机器上或经 tc 模拟网络条件运行
```

也就是说，实验中：

- 通信是真实发生的
- 字节数是真实统计的（若 benchmark 已输出）
- 轮次是真实统计的（若 benchmark 已输出）

### 1.2 不纳入本文的内容

本文**不包含**：

- CKKS / HE 相关实验
- OpenFHE 相关实验
- 纯单进程明文结构测试（除非它和双服务器主路径直接相关）

---

## 2. 数据集与规模

### 2.1 arcene

主路径运行中实际使用的 `arcene` 配置：

```text
train samples = 199
test samples  = 1
features      = 10000
```

来源：

- `Player-Data/Knn-Data/knn-1/arcene-data/`

用途：

- `kona.x` 主路径 benchmark
- `legacy-top1 / cong-pcr / cong-dcf` 主路径后端对照

### 2.2 tcga-pancan

导入后的 `tcga-pancan` 配置：

```text
train samples = 800
test samples  = 1
features      = 20531
labels        = 5
scale         = 10000
```

来源：

- `Player-Data/Knn-Data/knn-1/tcga-pancan-data/`

用途：

- 预处理 benchmark
- ESD benchmark
- Beaver 风格 ESD benchmark
- 传统 Beaver triple 预生成 benchmark

### 2.3 synthetic compare benchmark

在 `PCR vs DCF` comparator benchmark 中，输入通常不是实际数据集，而是：

```text
基于固定 seed 生成的 additive shares
规模分别为 1000 / 4000 / 10000 / 20000 / 262144
```

用途：

- 独立比较器性能对照
- LAN / WAN 环境下的多规模通信-计算特征分析

---

## 3. 组件分类

## 3.1 Top-k 网络

### legacy-top1

位置：

- `Machines/kona.cpp`

特点：

- 原始 Kona 路径
- 通过反复调用：

```text
for i in [0, k_const):
    top_1(m_ESD_vec, num_train_data - i, true)
```

- 本质上是“逐次 top-1”式选择

并行性：

```text
更偏串行
每次只处理当前一层/当前一轮需要的比较
```

### Cong 网络

位置：

- `Machines/kona-cong-topk.hpp`
- `Machines/kona-cong-kona-adapter.hpp`

特点：

- 固定比较网络
- 公开 `levels + output_wires`
- 每层比较对互不冲突
- 每层只调用一次批量 compare-and-swap

并行性：

```text
网络层并行
每层 pair 可并行
```

当前已接入主路径后端：

- `cong-pcr`
- `cong-dcf`

---

## 3.2 比较器

### DCF 比较器

位置：

- 原主路径：`Machines/kona.cpp`
- 独立高性能模块：`Machines/kona-dcf-compare.hpp`

特点：

- 基于 DCF / FSS 风格比较
- 通信轮次少
- 本地 `evaluate` 内核重

加速方案：

- key/r 预加载
- fast evaluate 内核
- batch evaluate 外壳

并行性：

```text
通信层更接近低轮次
本地计算更重
```

### PCR 比较器

位置：

- `Machines/kona-pcr-compare.hpp`

特点：

- sparse final-carry
- active bitpack
- 输出 Kona 风格算术比较位份额

加速方案：

- endpoint bit 压缩/展开
- 按层批量 Boolean AND
- B2A batch

并行性：

```text
每层批量
更偏并行比较器
```

注意：

- 当前语义带 `|x-y| < 2^63` 前提

---

## 3.3 交换器

### 原 Kona SS_vec / SS_scalar

位置：

- `Machines/kona.cpp`

特点：

- 用比较结果 `u=[left>right]`
- 同步交换：
  - distance
  - label

### Cong 适配层 SS_vec

位置：

- `Machines/kona-cong-kona-adapter.hpp`

特点：

- 复用 Kona 风格交换公式
- 当前已回补为更贴近原版的：

```text
double_res 风格批量乘法
一次同时处理 distance + label
```

并行性：

```text
每层批量交换
```

---

## 3.4 欧氏距离计算

### 原优化版 ESD

位置：

- `Machines/kona.cpp`
- 关键函数：
  - `compute_ESD_two_sample(...)`
  - `compute_ESD_for_one_query(...)`

特点：

- 使用定制预处理：
  - `m_Train_Triples_0`
  - `m_Train_Triples_1`
  - `m_Test_Triples`
  - `m_Test_Triples_0`
  - `m_Test_Triples_1`
- 不是传统普通 Beaver triple

### Beaver 风格 ESD

位置：

- `Machines/kona-beaver-esd-bench.cpp`

特点：

- 先做本地份额互减
- 再做安全乘法平方
- 再求和

版本：

1. `pair_batched`
2. `pair_comm_coalesced`

---

## 3.5 传统 Beaver triple 预生成

位置：

- `Machines/kona-beaver-triple-prep-bench.cpp`

特点：

- 目标是测：

```text
普通 [a,b,c=ab] triple
按 tcga-pancan 当前 pair 消耗量
预先生成 + 通信 + 落盘
```

不是测：

```text
接进 ESD 后的在线速度
```

---

## 4. 已完成的双服务器实验

## 4.1 主路径三后端对照（arcene）

入口：

- `kona.x`

后端：

- `legacy-top1`
- `cong-pcr`
- `cong-dcf`

数据集：

```text
arcene
199 train
1 test
10000 features
```

结果（P0）：

| 后端 | Rounds | Time(s) | Data sent(MB) | call_evaluate_nums | Evaluation time(s) |
|---|---:|---:|---:|---:|---:|
| legacy-top1 | 89 | 0.0735292 | 0.071456 | 2018 | 0.0672765 |
| cong-pcr | 293 | 0.00946171 | 0.093202 | 50 | 0.00165793 |
| cong-dcf | 90 | 0.0213393 | 0.050360 | 50 | 0.00188577 |

结果（P1）：

| 后端 | Accuracy | Rounds | Time(s) | Data sent(MB) | call_evaluate_nums | Evaluation time(s) |
|---|---:|---:|---:|---:|---:|---:|
| legacy-top1 | 1 | 88 | 0.0736303 | 0.071448 | 2018 | 0.0668005 |
| cong-pcr | 1 | 292 | 0.00955629 | 0.093194 | 50 | 0.00165803 |
| cong-dcf | 1 | 98 | 0.0214554 | 0.050352 | 50 | 0.00162949 |

结论：

- `cong-pcr` 主路径最快
- `cong-dcf` 比原始 `legacy-top1` 明显更快
- 三者当前样例准确率一致

---

## 4.2 PCR vs DCF comparator benchmark（无 netem）

入口：

- `Machines/kona-pcr-dcf-bench.cpp`

规模：

- `1000`
- `20000`

结果（历史版本）：

| 规模 | 方案 | 总时间(ms) | ns/compare | sent_bytes | rounds |
|---|---:|---:|---:|---:|---:|
| 1000 | PCR | 0.595501 | 595.501 | 70000 | 未记录 |
| 1000 | DCF | 137.748 | 137748 | 8000 | 未记录 |
| 20000 | PCR | 15.8106 | 790.531 | 1400000 | 未记录 |
| 20000 | DCF | 471.396 | 23569.8 | 160000 | 未记录 |

小规模优化后 DCF 结果：

| DCF 版本 | 1000 compare 总时间(ms) |
|---|---:|
| 最早版 benchmark | 137.748 |
| key/r 预加载版 | 56.5412 |
| fast evaluate 内核版 | 20.1704 |

结论：

- DCF 的主要收益来自 `evaluate` 内核优化
- 外围文件读取优化只是第一步

---

## 4.3 PCR vs DCF comparator benchmark（LAN）

网络：

- `1 Gbps`
- `0.5 ms` 单程延迟

规模：

- `1000 / 4000 / 10000 / 20000`

结果：

| 规模 | 方案 | 总时间(ms) | ns/compare | sent_bytes | rounds | DCF evaluate 次数 | DCF evaluate 时间(ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1000 | PCR | 0.574079 | 574.079 | 70000 | 16 | 0 | 0 |
| 1000 | DCF | 19.8842 | 19884.2 | 8000 | 2 | 2000 | 19.8254 |
| 4000 | PCR | 2.87619 | 719.047 | 280000 | 16 | 0 | 0 |
| 4000 | DCF | 79.3644 | 19841.1 | 32000 | 2 | 8000 | 79.1678 |
| 10000 | PCR | 5.29102 | 529.102 | 700000 | 16 | 0 | 0 |
| 10000 | DCF | 203.562 | 20356.2 | 80000 | 2 | 20000 | 203.126 |
| 20000 | PCR | 11.5931 | 579.656 | 1400000 | 16 | 0 | 0 |
| 20000 | DCF | 406.508 | 20325.4 | 160000 | 2 | 40000 | 405.572 |

结论：

- PCR 更吃通信量和轮次
- DCF 更吃本地计算
- 在 LAN 下 PCR 仍显著更快

---

## 4.4 PCR vs DCF comparator benchmark（WAN）

网络：

- `40 Mbps`
- `20 ms` 单程延迟

规模：

- `1000 / 4000 / 10000 / 20000`

结果：

| 规模 | 方案 | 总时间(ms) | ns/compare | sent_bytes | rounds | DCF evaluate 次数 | DCF evaluate 时间(ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1000 | PCR | 1.31336 | 1313.36 | 70000 | 16 | 0 | 0 |
| 1000 | DCF | 22.0253 | 22025.3 | 8000 | 2 | 2000 | 21.9542 |
| 4000 | PCR | 2.64711 | 661.778 | 280000 | 16 | 0 | 0 |
| 4000 | DCF | 88.5577 | 22139.4 | 32000 | 2 | 8000 | 88.3362 |
| 10000 | PCR | 6.06698 | 606.698 | 700000 | 16 | 0 | 0 |
| 10000 | DCF | 213.235 | 21323.5 | 80000 | 2 | 20000 | 212.767 |
| 20000 | PCR | 12.9072 | 645.36 | 1400000 | 16 | 0 | 0 |
| 20000 | DCF | 440.717 | 22035.9 | 160000 | 2 | 40000 | 439.613 |

结论：

- WAN 下 PCR 受轮次影响更明显
- 但当前规模下仍保持优于 DCF

---

## 4.5 tcga-pancan 预处理 benchmark

入口：

- `Machines/kona-preprocess-bench.cpp`

数据集：

```text
tcga-pancan
20531 features
800 train
1 test
```

结果：

```text
meta_read_seconds   = 0.000301247
dcf_prep_seconds    = 0.00151237
triple_prep_seconds = 1.2956
total_seconds       = 1.29741
```

结论：

- 预处理的主要成本在 triple 生成
- DCF key 生成几乎可忽略

---

## 4.6 tcga-pancan 原优化版 ESD benchmark

入口：

- `Machines/kona-esd-bench.cpp`

结果（P0）：

```text
read_seconds=0.511124
fake_triple_load_seconds=0.186759
share_setup_seconds=0.351966
compute_one_query_seconds=0.0257684
compute_all_pairs_loop_seconds=0.0262768
pair_ns=32846
total_seconds=1.10189
```

结果（P1）：

```text
read_seconds=0.000673837
fake_triple_load_seconds=0.173334
share_setup_seconds=0.875758
compute_one_query_seconds=0.0269904
compute_all_pairs_loop_seconds=0.0256962
pair_ns=32120.2
total_seconds=1.10245
```

结论：

- 单 query 对全部 800 个 train 的完整距离生成约 `0.026 s`
- 单 pair 距离核约 `32 ~ 33 us`

---

## 4.7 tcga-pancan Beaver 风格 ESD benchmark

入口：

- `Machines/kona-beaver-esd-bench.cpp`

### pair_batched 结果（已完整跑通）

结果（P0）：

```text
read_seconds=0.498215
share_setup_seconds=0.411586
beaver_compute_one_query_seconds=0.252626
beaver_compute_all_pairs_loop_seconds=0.24344
beaver_pair_ns=304300
total_seconds=1.40587
```

结果（P1）：

```text
read_seconds=0.00176257
share_setup_seconds=0.907866
beaver_compute_one_query_seconds=0.252816
beaver_compute_all_pairs_loop_seconds=0.243443
beaver_pair_ns=304303
total_seconds=1.40589
```

结论：

- Beaver 风格 pair_batched 比原优化版 ESD 慢约 `9 ~ 10x`

### pair_comm_coalesced 结果（完整规模）

状态：

```text
编译成功
双进程成功启动
完整 tcga-pancan 规模下运行时间过长
人工停止
```

结论：

```text
当前实现不可行
不适合作为默认全量 benchmark
```

---

## 4.8 tcga-pancan 传统 Beaver triple 预生成 benchmark

入口：

- `Machines/kona-beaver-triple-prep-bench.cpp`

### 旧版（生成 + 通信，不落盘）

结果（P0）：

```text
triples_per_pair=20531
num_pairs=800
total_triples=16424800
single_pair_gen_seconds=0.00128238
all_pairs_gen_seconds=0.300918
triples_per_second=5.45823e+07
sent_bytes=262796800
rounds=1600
```

### 公平版（生成 + 通信 + 落盘）

结果（P0）：

```text
triples_per_pair=20531
num_pairs=800
total_triples=16424800
single_pair_gen_seconds=0.00155667
all_pairs_gen_and_write_seconds=1.07549
triples_per_second=1.5272e+07
sent_bytes=262796800
rounds=1600
file_bytes_written=394195200
```

结果（P1）：

```text
single_pair_gen_seconds=0.00143442
all_pairs_gen_and_write_seconds=1.07524
triples_per_second=1.52755e+07
sent_bytes=262796800
rounds=1600
file_bytes_written=394195200
```

结论：

- 公平版下传统 Beaver triple 预生成约 `1.075 s`
- 与 Kona 预处理文件生成的 `1.297 s` 相比，传统 Beaver triple 预生成略快，但不是数量级差距

---

## 4.9 Cong + DCF Top-k 网络 benchmark（容器内 LAN / WAN）

入口：

- `Machines/kona-topk-network-bench.cpp`
- 运行模式：
  - `--mode cong-dcf`

说明：

- 手工 share 数据
- 不走 `kona.cpp` 主路径
- 统一使用：
  - `Cong` 网络
  - `DCF` 比较器
- 当前矩阵网络不纳入这一组结果

### LAN：1 Gbps + 0.5 ms one-way

#### `N=1024, k=5`

```text
total_ms         = 109.38
compute_ms       = 70.0165
comm_ms          = 39.3636
sent_bytes       = 257328
transport_rounds = 136
logical_rounds   = 68
```

#### `N=4096, k=16`

```text
total_ms         = 623.282
compute_ms       = 549.763
comm_ms          = 73.5193
sent_bytes       = 2042496
transport_rounds = 200
logical_rounds   = 100
```

### WAN：40 Mbps + 20 ms one-way

#### `N=1024, k=5`

```text
total_ms         = 1437.12
compute_ms       = 67.2216
comm_ms          = 1369.9
sent_bytes       = 257328
transport_rounds = 136
logical_rounds   = 68
```

#### `N=4096, k=16`

```text
total_ms         = 3008.16
compute_ms       = 566.151
comm_ms          = 2442.01
sent_bytes       = 2042496
transport_rounds = 200
logical_rounds   = 100
```

### 结果解读

- 在 `N=1024, k=5` 时：
  - `LAN` 下已经是计算和通信都重要
  - `WAN` 下明显转为通信主导
- 在 `N=4096, k=16` 时：
  - `LAN` 下计算占比显著增加
  - `WAN` 下仍然是通信主导
- 与同规模 `Cong + PCR` 相比：
  - `Cong + DCF` 轮次更少
  - 但本地计算更重

---

## 5. 串行 / 并行 / 加速方式总表

| 组件/实验 | 串行/并行特征 | 使用的加速方式 |
|---|---|---|
| legacy-top1 主路径 | 更偏串行 | 原始 `top_1` 重复调用 |
| Cong 网络 | 按层并行 | 固定比较网络，公开 `levels` |
| PCR comparator | 每层批量 | sparse final-carry + active bitpack + batch B2A |
| DCF comparator | 低轮次、本地重 | key/r 预加载 + fast evaluate 内核 |
| 原优化版 ESD | 定制优化 | aby2 风格份额 + 定制预处理 |
| Beaver ESD pair_batched | pair 内并行 | 每个 pair 按 feature 批量乘 |
| Beaver ESD pair_comm_coalesced | 通信合并 | pair 语义保持，统一 send/receive（完整规模不可行） |
| 传统 Beaver triple 预生成 | pair-by-pair | 真实通信 + 公平版落盘 |

---

## 6. 当前总体工程结论

### 6.1 从主路径看

- `cong-pcr` 是当前最有竞争力的主路径后端
- `cong-dcf` 可作为中间基线
- `legacy-top1` 保留了原始 Kona 行为

### 6.2 从比较器看

- `PCR`：通信更大、轮次更多，但端到端更快
- `DCF`：通信更小、轮次更少，但 `evaluate` 内核仍然重

### 6.3 从距离核看

- 原优化版 ESD 是当前最适合主路径继续使用的距离核
- Beaver 风格 ESD 更适合作为对照实验，而不是默认主路径选择

### 6.4 从预处理看

- Kona 预处理文件生成约 `1.30 s`
- 公平版传统 Beaver triple 预生成约 `1.08 s`
- 说明 Kona 定制预处理虽然更重，但并没有重到离谱

### 6.5 当前仍需注意的点

- `tcga-pancan` 当前只有 `1` 个 test，不适合代表完整吞吐实验
- `PCR` 比较器仍带 `|x-y| < 2^63` 前提
- `pair_comm_coalesced` 版本目前完整规模不可行

---

## 7. 推荐阅读顺序

1. `docs/kona_primitives_audit_zh.md`
2. `docs/interface_tests_and_benchmarks_checklist_zh.md`
3. `docs/kona_mainpath_topk_backends_benchmark_zh.md`
4. `docs/kona_pcr_dcf_benchmark_summary_zh.md`
5. `docs/tcga_pancan_preprocess_esd_benchmark_zh.md`

这样可以从：

```text
源码审计
-> 组件清单
-> 主路径
-> 比较器
-> tcga-pancan 专项测速
```

逐层理解整个实验体系。
