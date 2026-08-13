# 组件性能与实现细节记录

日期：`2026-08-08`

本文专门记录当前项目中各个**组件级别**的性能实验、批量粒度实验、并行化程度审计，以及相关实现细节。

与其他文档的区别：

- 本文聚焦**组件性能与实现口径**
- 不以主路径 benchmark 为主
- 不以功能清单为主
- 不混入 HE / CKKS 方案

建议后续把以下内容都继续补到本文：

- 比较器性能
- 批量粒度实验
- 份额转换性能
- ESD 核性能
- 三元组预生成性能
- 矩阵 / Cong / Legacy 网络的并行化程度审计

---

## 0. 口径说明：无额外延迟不等于零通信时间

本文中的“无额外延迟”指的是：

```text
没有额外施加 tc / netem 的带宽限制和人工网络时延
```

它**不等于**：

```text
通信耗时为 0
```

原因是当前 benchmark 中的 `comm_ms` 统计的是底层 `send()` / `receive()` 调用的真实墙钟时间，因此即使在容器内 `localhost`、没有施加 `LAN/WAN` 条件时，仍然会存在：

- 进程切换与调度开销
- 本地 socket / TCP 栈处理开销
- `octetStream` 打包、拆包、内存拷贝开销
- 双进程之间的同步等待时间

因此应当这样理解：

- `无额外延迟版本`
  - 没有人工注入的网络时延
  - 但仍有本机双进程真实通信成本

- `LAN / WAN 版本`
  - 在上述本机通信成本基础上
  - 再叠加容器内 `tc/netem` 注入的带宽 / 时延条件

这也是为什么某些协议在“无额外延迟”条件下，`comm_ms` 仍然可能很大：  
它反映的是**本地真实通信成本**，而不是“是否跨机传播”这一件事。

---

## 1. 文档范围

当前纳入本文的组件包括：

- `kona-share-conversion.hpp`
- `kona-pcr-compare.hpp`
- `kona-dcf-compare.hpp`
- `kona-cong-topk.hpp`
- `kona-cong-kona-adapter.hpp`
- `kona-matrix-topk.hpp`
- `kona-esd-bench.cpp`
- `kona-beaver-esd-bench.cpp`
- `kona-beaver-triple-prep-bench.cpp`
- `kona-pcr-batchsize-bench.cpp`

---

## 2. 批量乘法与批量转换现状

### 2.1 批量乘法

当前已有两类可复用批量乘法：

1. 原 Kona 主路径里的：

```text
KNN_party_base::mul_vector_additive(...)
```

位置：

- `Machines/kona.cpp`

特点：

- 真实通信
- 字节数真实
- 支持 `double_res`

2. 抽出的可复用版本：

```text
KonaShareConversion::mul_vector_additive_kona_l2(...)
```

位置：

- `Machines/kona-share-conversion.hpp`

特点：

- 真实通信
- 字节数真实
- 当前也已经补齐 `double_res` 版本
- 现在 `Cong` 适配层已经回到更贴近原 Kona 的双段批量乘法形状

### 2.2 批量转换

当前已有完整批量转换接口：

- `open_additive_batch_l2`
- `open_xor_batch_l2`
- `open_subtractive_batch_l2`
- `A2B_batch_l2`
- `B2A_batch_l2`
- `ABit2BBit_batch_l2`
- `BBit2ABit_batch_l2`

位置：

- `Machines/kona-share-conversion.hpp`

特点：

- 通信真实
- 字节数真实
- 当前 L2 风格允许随机掩码 / 三元组置零

### 2.3 当前默认批量挡位

当前通用批量路径已经引入默认挡位：

```text
KONA_DEFAULT_BATCH_CHUNK = 16348
```

位置：

- `Machines/kona-share-conversion.hpp`

当前已经切到该默认挡位的通用路径包括：

- `open_additive_batch_l2`
- `open_xor_batch_l2`
- `open_subtractive_batch_l2`
- `B2A_batch_l2` 内部乘法
- `mul_vector_additive_kona_l2_chunked`
- `Cong` 适配层中的批量交换乘法
- `Matrix` 适配层中的批量 vote 乘法

当前约定是：

```text
除针对 seq-ran / tcga 这类 ESD 距离核专项实验外，
其他通用大整块 / 大批量的 Beaver 乘法、份额转换、批量打开等动作，
默认优先切成 size = 16348 的批次执行。
```

---

## 3. PCR 比较器批量粒度实验

### 3.1 实验目标

实验问题：

```text
同样是 16348 个比较任务
一次性发完更快
还是切成中等大小小块更快？
```

实验入口：

- `Machines/kona-pcr-batchsize-bench.cpp`

实验设置：

- 总比较任务数：`16348`
- 比较器：`PCR`
- 每档重复：`3` 次
- 批量大小：
  - `1024`
  - `4096`
  - `8193`
  - `16348`

输出指标：

- 每个小块时间 `chunk_ms`
- 总时间 `total_ms`
- 纯计算时间 `compute_ms`
- 通信时间 `comm_ms`
- `sent_bytes`
- `transport_rounds`
- `logical_rounds`

### 3.2 平均结果

| batch size | 平均总时间 ms | 平均纯计算 ms | 平均通信 ms | 平均 sent_bytes | 平均 transport_rounds | 平均 logical_rounds |
|---|---:|---:|---:|---:|---:|---:|
| 1024 | 8.403 | 5.717 | 2.685 | 1,144,360 | 256 | 128 |
| 4096 | 7.421 | 6.616 | 0.806 | 1,144,360 | 64 | 32 |
| 8193 | 7.906 | 7.384 | 0.522 | 1,144,360 | 32 | 16 |
| 16348 | 8.560 | 7.890 | 0.670 | 1,144,360 | 16 | 8 |

### 3.3 结论

结论非常清楚：

- `batch size = 1024`
  - 太碎
  - 通信轮次过多
  - `comm_ms` 明显偏高

- `batch size = 16348`
  - 虽然通信轮次最少
  - 但单批太大
  - 本地计算 / 打包开销上升

- `batch size = 4096`
  - 在当前实现里是最优点

一句话总结：

```text
批量太小会输在通信
批量太大会输在单批处理成本
当前 4096 是更均衡的粒度
```

### 3.4 在 LAN / WAN 下的批量粒度变化

为了更贴近双服务器真实通信，又补做了两类网络条件：

- `LAN`：容器内 `1 Gbps + 0.5 ms` 单程
- `WAN`：容器内 `40 Mbps + 20 ms` 单程

#### `num_compare = 16348`

| batch size | 网络 | 平均总时间 ms | 平均纯计算 ms | 平均通信 ms | 平均 sent_bytes | 平均 transport_rounds | 平均 logical_rounds |
|---|---|---:|---:|---:|---:|---:|---:|
| 1024 | LAN | 75.566 | 6.707 | 68.859 | 1,144,360 | 256 | 128 |
| 4096 | LAN | 30.537 | 7.171 | 23.366 | 1,144,360 | 64 | 32 |
| 8193 | LAN | 24.617 | 8.145 | 16.472 | 1,144,360 | 32 | 16 |
| 16348 | LAN | 22.324 | 9.096 | 13.228 | 1,144,360 | 16 | 8 |
| 1024 | WAN | 2578.660 | 7.196 | 2571.470 | 1,144,360 | 256 | 128 |
| 4096 | WAN | 782.814 | 8.869 | 773.945 | 1,144,360 | 64 | 32 |
| 8193 | WAN | 540.055 | 8.191 | 531.864 | 1,144,360 | 32 | 16 |
| 16348 | WAN | 456.995 | 7.660 | 449.334 | 1,144,360 | 16 | 8 |

结论：

- 在无额外延迟时，`4096` 更均衡
- 一旦进入 `LAN / WAN` 条件，batch 越大越有利
- `WAN` 下几乎完全由轮次主导，因此大批量优势最明显

#### `num_compare = 32696`，增加 `batch size = 32696`

本组实验只保留 `32696` 个比较任务，并在 `16348` 的基础上增加更大的挡位 `32696`。
同时记录双侧峰值内存。

##### LAN（`1 Gbps + 0.5 ms` 单程）

| batch size | 平均总时间 ms | 平均纯计算 ms | 平均通信 ms | 平均 sent_bytes | 平均 transport_rounds | 平均 logical_rounds | P0 峰值内存 KB | P1 峰值内存 KB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1024 | 151.986 | 13.339 | 138.647 | 2,288,720 | 512 | 256 | 7200 | 7200 |
| 4096 | 63.788 | 15.817 | 47.972 | 2,288,720 | 128 | 64 | 7564 | 7660 |
| 8193 | 49.529 | 15.413 | 34.116 | 2,288,720 | 64 | 32 | 7952 | 8368 |
| 16348 | 42.858 | 15.312 | 27.546 | 2,288,720 | 32 | 16 | 9296 | 10028 |
| 32696 | 43.057 | 16.413 | 26.644 | 2,288,720 | 16 | 8 | 12724 | 12448 |

##### WAN（`40 Mbps + 20 ms` 单程）

| batch size | 平均总时间 ms | 平均纯计算 ms | 平均通信 ms | 平均 sent_bytes | 平均 transport_rounds | 平均 logical_rounds | P0 峰值内存 KB | P1 峰值内存 KB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1024 | 5151.670 | 14.242 | 5137.420 | 2,288,720 | 512 | 256 | 7040 | 7200 |
| 4096 | 1607.660 | 16.676 | 1590.990 | 2,288,720 | 128 | 64 | 7612 | 7544 |
| 8193 | 1118.880 | 15.928 | 1102.950 | 2,288,720 | 64 | 32 | 8684 | 8424 |
| 16348 | 961.148 | 16.077 | 945.071 | 2,288,720 | 32 | 16 | 9872 | 10468 |
| 32696 | 919.585 | 17.616 | 901.969 | 2,288,720 | 16 | 8 | 13320 | 13060 |

##### 32696 组结论

- `LAN` 下：
  - `16348` 与 `32696` 总时间几乎打平
  - `32696` 通信更少，但内存峰值更高
  - 从工程均衡性看，`16348` 更适合作为默认挡位

- `WAN` 下：
  - `32696` 略优于 `16348`
  - 主要收益来自进一步减少通信轮次
  - 如果网络非常差、并且内存允许，可以考虑更大的整批

一句话总结：

```text
在当前工程实现里，
16348 适合作为通用默认挡位；
若专门针对高延迟 WAN，可再考虑放大到 32696。
```

---

## 4. 矩阵型 Top-k 当前并行化程度审计

### 4.1 当前实现位置

- `Machines/kona-matrix-topk.hpp`

### 4.2 比较层

当前矩阵型 Top-k 在比较层做的是：

```text
全部上三角 pair
一次性批量比较
```

具体过程：

1. 生成全部 `(i, j), i < j`
2. 一次性抽出：
   - `left_values`
   - `right_values`
3. 一次性调用：
   - `pcr_compare_gt_batch_l2(...)`

因此：

```text
比较器层已经批量化
而且是“全 pair 一次性批量”
```

### 4.3 布尔域转算术域

比较器内部：

- `PCR` 输出 bit
- 再通过：

```text
B2A_batch_l2(...)
```

整批转成算术份额

因此：

```text
布尔 -> 算术
已经整批完成
不是逐个转换
```

### 4.4 rank 累加

当前 `rank` 累加仍然是：

```text
比较结果是批量出来的
但累加本身仍在本地逐 pair 更新
```

也就是说：

- 逻辑正确
- 还不是 tile / block / scatter_add / 行列归约版本

### 4.5 当前问题

当前矩阵型 Top-k 的问题不是“不批量”，而是：

```text
批量过大
```

例如：

- `N=1024` 时 pair 数：
  - `523,776`
- `N=4096` 时 pair 数：
  - `8,386,560`

当前实现试图把这些 pair：

```text
一次性全部送进批量比较器
```

这会导致：

- 峰值内存高
- 一次性通信缓冲区大
- 一次性 B2A 负载大
- rank 归约压力也大

### 4.6 结论

当前矩阵型 Top-k 可以概括为：

```text
比较层：并行
B2A 层：并行
rank 累加层：未完全并行
总体：批量方向对，但粒度过大
```

因此后续如果继续推进矩阵方案，最自然的方向是：

```text
tile / block 版本
```

而不是继续全上三角一次性硬吞。

---

## 5. tcga-pancan 原优化版 ESD 性能

实验入口：

- `Machines/kona-esd-bench.cpp`

数据：

- `tcga-pancan`
- `20531` 特征
- `800 train`
- `1 test`

### P0

```text
read_seconds=0.511124
fake_triple_load_seconds=0.186759
share_setup_seconds=0.351966
compute_one_query_seconds=0.0257684
compute_all_pairs_loop_seconds=0.0262768
pair_ns=32846
total_seconds=1.10189
```

### 结论

- 当前 `compute_ESD_for_one_query()` 对 800 个样本的全量距离生成约 `0.026 s`
- 单 pair 距离核约 `32 ~ 33 us`

---

## 6. tcga-pancan Beaver 风格 ESD（pair_batched）性能

实验入口：

- `Machines/kona-beaver-esd-bench.cpp`

数据：

- `tcga-pancan`
- `20531` 特征
- `800 train`
- `1 test`

### P0

```text
read_seconds=0.498215
share_setup_seconds=0.411586
beaver_compute_one_query_seconds=0.252626
beaver_compute_all_pairs_loop_seconds=0.24344
beaver_pair_ns=304300
total_seconds=1.40587
```

### 结论

- `pair_batched` 版本比原优化版 ESD 大约慢 `9 ~ 10x`
- 适合作为“真实乘法通信形状”的独立对照

---

## 7. tcga-pancan 传统 Beaver triple 预生成（公平版）

实验入口：

- `Machines/kona-beaver-triple-prep-bench.cpp`

模型：

- `800` 个 pair
- 每个 pair `20531` 个 triple
- 含：
  - 生成
  - 通信
  - 落盘

### P0

```text
single_pair_gen_seconds=0.00155667
all_pairs_gen_and_write_seconds=1.07549
triples_per_second=1.5272e+07
sent_bytes=262796800
rounds=1600
file_bytes_written=394195200
```

### 结论

- 公平版传统 Beaver triple 预生成约 `1.075 s`
- 与 Kona 预处理文件生成的 `1.297 s` 相比略快，但不是数量级差距

---

## 8. 当前组件性能上的总体判断

### 8.1 比较器

- `PCR` 更适合当前工程落地
- `DCF` 仍然可用，但 `evaluate` 内核重

### 8.2 距离核

- 原优化版 ESD 仍然是最适合主路径使用的距离核
- Beaver 风格 ESD 更适合作为独立对照

### 8.3 Top-k 网络

- `Cong` 已在大规模测试中显示出明显优于 `legacy`
- `matrix` 的问题不是“没有批量”，而是“全量一次性批量过重”

### 8.4 后续性能工作重点

如果继续做性能工程，优先建议：

1. 继续以 `Cong + PCR` 为主线
2. `matrix` 改 tile/block 版
3. DCF 继续只在必要场景下优化，不再大面积扩主路径
