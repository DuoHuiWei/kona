# TCGA-PANCAN 预处理与欧氏距离测速记录

本文记录将 `TCGA-PANCAN-HiSeq-801x20531` 数据导入 Kona 后，针对：

- 数据预处理
- 欧氏距离（ESD）计算

所做的独立 benchmark 结果。

日期：`2026-07-25`

## 1. 数据来源与导入

原始数据来源目录：

```text
/mnt/c/Users/77231/Downloads/gene+expression+cancer+rna+seq/
TCGA-PANCAN-HiSeq-801x20531.tar/
TCGA-PANCAN-HiSeq-801x20531
```

原始文件：

- `data.csv`
- `labels.csv`

导入脚本：

- `Scripts/import_tcga_pancan_to_kona.py`

导入后的 Kona 目录：

- `Player-Data/Knn-Data/knn-1/tcga-pancan-data/`

生成文件：

- `Knn-meta`
- `P0-0-X-Train`
- `P0-0-Y-Train`
- `P1-0-X-Test`
- `P1-0-Y-Test`
- `label_map.txt`

导入参数：

```text
scale = 10000
seed = 20260724
test_count = 1
```

导入脚本输出确认：

```text
features: 20531
train samples: 800
test samples: 1
labels: 5
scale: 10000
seed: 20260724
```

## 2. 数据完整性检查

当前 `tcga-pancan-data` 目录已检查完整。

关键文件：

- `Player-Data/Knn-Data/knn-1/tcga-pancan-data/Knn-meta`
- `Player-Data/Knn-Data/knn-1/tcga-pancan-data/P0-0-X-Train`
- `Player-Data/Knn-Data/knn-1/tcga-pancan-data/P0-0-Y-Train`
- `Player-Data/Knn-Data/knn-1/tcga-pancan-data/P1-0-X-Test`
- `Player-Data/Knn-Data/knn-1/tcga-pancan-data/P1-0-Y-Test`
- `Player-Data/Knn-Data/knn-1/tcga-pancan-data/label_map.txt`

`Knn-meta` 内容：

```text
20531 800 1
```

含义：

- 特征数：`20531`
- 训练样本数：`800`
- 测试样本数：`1`

文件行数检查：

```text
P0-0-X-Train : 800 行
P0-0-Y-Train : 800 行
P1-0-X-Test  :   1 行
P1-0-Y-Test  :   1 行
```

当前 label 映射：

```text
0 -> PRAD
1 -> LUAD
2 -> BRCA
3 -> KIRC
4 -> COAD
```

## 3. 说明：为什么只有 1 个 test 仍然可以测

当前导入设置为：

```text
test_count = 1
```

这意味着：

- 训练集：`800`
-
测试集：`1`

对欧氏距离 benchmark 来说，这样仍然是合理的，因为：

```text
一次 query
对全部 train 样本计算距离
正是单轮 KNN 查询的标准在线形态
```

因此当前 `compute_ESD_for_one_query()` 的测量结果，可以直接理解为：

```text
1 个 query
对 800 个训练样本
做完整距离生成
```

这对后续评估：

- 单次在线查询成本
- 距离核性能
- top-k 前端负载

都很有意义。

## 4. 预处理 Benchmark

预处理 benchmark 入口：

- `Machines/kona-preprocess-bench.cpp`

设计原则：

- 不重写核心逻辑
- 直接复用原组件
- 只加计时

复用组件：

- `read_meta_data()`
- `gen_fake_dcf(...)`
- `generate_triples_save_file_optimized()`

运行结果：

```text
dataset=knn-1/tcga-pancan
num_features=20531
num_train_data=800
num_test_data=1
meta_read_seconds=0.000301247
dcf_prep_seconds=0.00151237
triple_prep_seconds=1.2956
total_seconds=1.29741
```

### 4.1 结果解读

- `meta_read_seconds` 几乎可以忽略
- `dcf_prep_seconds` 很短
- 当前预处理主要时间几乎全部在：

```text
triple_prep_seconds = 1.2956
```

也就是说，这个数据集下：

```text
预处理的主要成本是 triple 生成
不是 DCF key 生成
```

## 5. 欧氏距离 Benchmark

ESD benchmark 入口：

- `Machines/kona-esd-bench.cpp`

设计原则：

- 不重写核心逻辑
- 直接复用 Kona 原组件
- 保留原双进程通信和 share 逻辑

复用组件：

- `read_meta_and_P0_sample_P1_query()`
- `fake_load_triples()`
- `aby2_share_data_and_additive_share_label_list()`
- `compute_ESD_two_sample(...)`
- `compute_ESD_for_one_query(...)`

其中：

- `compute_ESD_for_one_query(0)`：表示 `1` 个 test 对 `800` 个 train 的完整距离生成
- `compute_all_pairs_loop_seconds`：逐个循环调用 `compute_ESD_two_sample(i, 0)`

## 6. 欧氏距离 Benchmark 结果

### 6.1 Party 0

```text
dataset=tcga-pancan
player=0
num_features=20531
num_train_data=800
num_test_data=1
read_seconds=0.511124
fake_triple_load_seconds=0.186759
share_setup_seconds=0.351966
compute_one_query_seconds=0.0257684
compute_all_pairs_loop_seconds=0.0262768
pair_ns=32846
checksum_limb0=0
total_seconds=1.10189
```

### 6.2 Party 1

```text
dataset=tcga-pancan
player=1
num_features=20531
num_train_data=800
num_test_data=1
read_seconds=0.000673837
fake_triple_load_seconds=0.173334
share_setup_seconds=0.875758
compute_one_query_seconds=0.0269904
compute_all_pairs_loop_seconds=0.0256962
pair_ns=32120.2
checksum_limb0=6703504684926890
total_seconds=1.10245
```

## 7. 结果解读

### 7.1 读数据阶段

P0：

```text
read_seconds ≈ 0.511 s
```

P1：

```text
read_seconds ≈ 0.00067 s
```

这很正常，因为：

- P0 读 `800 x 20531` 的训练特征矩阵
- P1 只读 `1 x 20531` 的测试特征向量

因此在当前导入方式下，P0 的磁盘/文本读取成本远高于 P1。

### 7.2 fake triple load

两边都在：

```text
0.17 ~ 0.19 s
```

这部分主要是 benchmark 中为当前数据尺寸分配和初始化三元组相关容器的成本。

### 7.3 share setup

P0：

```text
0.352 s
```

P1：

```text
0.876 s
```

这一步对应：

- `aby2_share_data_and_additive_share_label_list()`

其中包含真实的双进程通信和 share 构造，因此其耗时已经不再是纯本地读写，而是更接近后续主流程的在线准备阶段。

### 7.4 纯距离核

两边的核心结果非常接近：

```text
compute_one_query_seconds ≈ 0.026 s
compute_all_pairs_loop_seconds ≈ 0.026 s
pair_ns ≈ 32 ~ 33 us
```

这说明：

- 当前 `compute_ESD_for_one_query()` 的绝大头确实就是遍历所有 train 样本调用 `compute_ESD_two_sample(...)`
- 函数包装本身没有明显额外开销

### 7.5 当前数据下的全量 ESD 含义

因为当前：

```text
num_test_data = 1
num_train_data = 800
```

所以当前一次：

```text
compute_ESD_for_one_query()
```

就已经等价于：

```text
对当前 tcga-pancan 数据版本
做完整一次 query-to-all-train 距离生成
```

也就是：

```text
1 个 test
对 800 个 train
全量距离计算
```

## 8. 关键结论

### 8.1 预处理

当前 `tcga-pancan` 下：

- 预处理总时间约为：

```text
1.4324 s
```

- 主要瓶颈是：

```text
triple 生成
```

### 8.2 欧氏距离

当前 `tcga-pancan` 下：

- 单个 query 对全部 `800` 个 train 的完整距离生成约为：

```text
0.026 s
```

- 单个 train-test pair 的距离核成本约为：

```text
32 ~ 33 us
```

### 8.3 工程判断

对这个数据集来说：

- 纯 ESD 本身并不算慢
- 后面更可能成为主瓶颈的仍然是：
  - Top-k 网络
  - 比较器
  - 通信轮次

因此，如果后续你要做完整 `kona.x` 性能分析，这份 ESD benchmark 可以作为：

```text
距离核基线
```

用于和：

- `legacy-top1`
- `cong-pcr`
- `cong-dcf`

这些主路径 benchmark 做拆分对比。

## 9. Beaver 风格欧氏距离 Benchmark

### 9.1 benchmark 入口

- `Machines/kona-beaver-esd-bench.cpp`

设计原则：

- 不重写数据读取与分享方式
- 继续复用 Kona 的真实双进程通信
- 只把距离核改成：

```text
x_share - y_share
-> 安全乘法求平方
-> 累加得到欧氏距离份额
```

### 9.2 Beaver ESD（pair_batched）结果

这是已完整跑完的一版结果。

#### Party 0

```text
dataset=tcga-pancan
player=0
num_features=20531
num_train_data=800
num_test_data=1
read_seconds=0.498215
share_setup_seconds=0.411586
beaver_compute_one_query_seconds=0.252626
beaver_compute_all_pairs_loop_seconds=0.24344
beaver_pair_ns=304300
checksum_limb0=0
total_seconds=1.40587
```

#### Party 1

```text
dataset=tcga-pancan
player=1
num_features=20531
num_train_data=800
num_test_data=1
read_seconds=0.00176257
share_setup_seconds=0.907866
beaver_compute_one_query_seconds=0.252816
beaver_compute_all_pairs_loop_seconds=0.243443
beaver_pair_ns=304303
checksum_limb0=6703504684926890
total_seconds=1.40589
```

### 9.3 与原优化版 ESD 对比

原优化版 ESD：

```text
compute_one_query_seconds ≈ 0.026 s
pair_ns ≈ 32 ~ 33 us
```

Beaver 风格（pair_batched）：

```text
beaver_compute_one_query_seconds ≈ 0.253 s
beaver_pair_ns ≈ 304 us
```

结论：

```text
Beaver 风格 ESD
比当前 Kona 优化版 ESD
大约慢 9 ~ 10 倍
```

### 9.4 通信字节数与轮次说明

当前 `kona-preprocess-bench.cpp`、`kona-esd-bench.cpp` 与这版
`kona-beaver-esd-bench.cpp` 的早期输出中，没有直接把：

- `sent_bytes`
- `rounds`

打印到日志里。

因此这几组 benchmark 的文档目前可以严谨记录：

- 时间
- 数据规模
- 分阶段耗时
- checksum

但不能在本节里假装给出“精确实测字节数和轮次”。

也就是说：

```text
这些 benchmark 的逻辑和通信是真实的
但这份文档中的 bytes/rounds 对这些条目暂时属于未记录
```

### 9.5 pair_comm_coalesced 完整规模结论

后续我们把 `kona-beaver-esd-bench.cpp` 改成了一个更激进的版本：

```text
单个 pair 的逻辑仍然独立
但把所有 pair 的乘法输入统一发送、统一接收
```

在当前完整规模：

```text
20531 features
800 train
1 test
```

下，这个版本：

- 编译成功
- 双进程成功启动
- 但完整规模运行时间过长，长时间未收口
- 最终人工停止

因此当前结论是：

```text
pair_comm_coalesced 版本
在 tcga-pancan 的完整规模下
当前实现不可行
不适合作为默认全量 benchmark
```

### 9.6 工程结论

当前针对 `tcga-pancan` 的距离核可以得出：

1. 原优化版 ESD 仍然更适合作为默认主路径距离核。
2. Beaver 风格 `pair_batched` 适合作为“真实乘法通信形状”的独立对照。
3. `pair_comm_coalesced` 在完整规模下当前不可行，不适合作为默认实现。

## 10. 传统 Beaver 三元组预生成 Benchmark

### 10.1 benchmark 入口

- `Machines/kona-beaver-triple-prep-bench.cpp`

设计目标：

- 不测 ESD 在线计算
- 只测“按当前数据集实际 pair 消耗量，预先生成一批普通 `[a,b,c]` 三元组”的速度
- 当前按：

```text
800 个 pair
每个 pair 20531 个 triple
```

进行生成。

### 10.2 运行模型

当前 benchmark 采用：

```text
pair by pair
```

每个 pair：

- 生成 `20531` 个 `a_local`
- 生成 `20531` 个 `b_local`
- 双方交换本地份额
- 本地恢复 `a,b`
- 构造满足 `c = a*b mod 2^64` 的加法份额

这里的目标是：

```text
测传统 Beaver triple 预生成吞吐
```

而不是：

```text
测把 triple 接回 ESD 后的在线速度
```

### 10.3 结果

#### Party 0

```text
dataset=tcga-pancan
player=0
triples_per_pair=20531
num_pairs=800
total_triples=16424800
single_pair_gen_seconds=0.00155667
all_pairs_gen_and_write_seconds=1.07549
triples_per_second=1.5272e+07
sent_bytes=262796800
rounds=1600
file_bytes_written=394195200
checksum_limb0=0
```

#### Party 1

```text
dataset=tcga-pancan
player=1
triples_per_pair=20531
num_pairs=800
total_triples=16424800
single_pair_gen_seconds=0.00143442
all_pairs_gen_and_write_seconds=1.07524
triples_per_second=1.52755e+07
sent_bytes=262796800
rounds=1600
file_bytes_written=394195200
checksum_limb0=5008070106966606724
```

### 10.4 结果解读

当前模型下：

- 单个 pair 的 triple 预生成时间大约：

```text
1.43 ~ 1.56 ms
```

- 全部 `800` 个 pair 的 triple 预生成总时间大约：

```text
1.075 s
```

- 吞吐大约：

```text
1.53e7 triples / second
```

### 10.5 通信代价

当前这版 benchmark 的通信统计已经完整输出：

```text
sent_bytes = 262,796,800
rounds     = 1600
file_bytes_written = 394,195,200
```

这和当前“pair by pair”模型一致：

- `800` 个 pair
- 每个 pair 一次双向交换
- 在 `Player` 统计口径里累计为 `1600` 轮

### 10.6 工程结论

从当前结果看：

1. 传统 Beaver triple 在“生成 + 通信 + 落盘”的公平条件下，完整 `tcga-pancan` 当前 pair 消耗量约为 `1.075 s`。
2. 即使把落盘成本算进去，它仍然比 Beaver 风格 ESD 在线计算时间更小。
3. 因此如果后续要做“预生成 triples + 在线消费”的拆分实验，当前预生成阶段不会是主要瓶颈。
