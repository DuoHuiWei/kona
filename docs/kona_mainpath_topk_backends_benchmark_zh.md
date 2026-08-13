# Kona 主路径 Top-k 后端 Benchmark 说明

本文记录 `kona.x` 主路径中三种 Top-k 后端的接入方式、运行结果和当前工程化程度。这里的目标不是做独立比较器 micro-benchmark，而是尽量保持原 Kona 主流程不变，只替换 Top-k 网络/比较后端，以便更公平地比较性能。

当前日期：`2026-07-23`

## 1. 接入原则

当前主路径保持如下结构不变：

1. `read_meta_and_P0_sample_P1_query()`
2. `fake_load_triples()`
3. `aby2_share_data_and_additive_share_label_list()`
4. `compute_ESD_for_one_query(idx)` 生成 `m_ESD_vec`
5. Top-k 选择
6. `label_compute(...)`
7. 赢家标签选择
8. `reveal_one_num_to(...)` 输出预测 label

其中：

- `m_ESD_vec` 的含义不变，仍然是：
  - `m_ESD_vec[i][0]`：第 `i` 个训练样本到当前 query 的距离份额
  - `m_ESD_vec[i][1]`：第 `i` 个训练样本的标签份额
- `label_compute(...)` 仍然按原 Kona 路径执行
- 最终投票 winner 的选择也仍然在主路径里完成

因此当前改造遵循：

```text
只换 Top-k 网络/比较后端
不改 m_ESD_vec 的生成
不改 label_compute 的语义
不改最终 reveal/accuracy 统计方式
```

## 2. 后端选项

`kona.x` 当前支持：

```text
--topk-backend legacy-top1   默认，原始 Kona 路径
--topk-backend cong-pcr      Cong 网络 + PCR 比较
--topk-backend cong-dcf      Cong 网络 + DCF 比较
```

默认值仍然是：

```text
legacy-top1
```

因此如果不显式传参，原主路径行为不变。

## 3. 相关文件

### 3.1 主路径接入

- `Machines/kona.cpp`

### 3.2 Cong 网络与适配层

- `Machines/kona-cong-topk.hpp`
  - Cong 网络生成
  - `levels`
  - `output_wires`
  - 通用 `cong_top_k()` 调度
- `Machines/kona-cong-kona-adapter.hpp`
  - `cong_top_k_with_pcr()`
  - `cong_top_k_with_dcf()`
  - Kona 风格 `SS_vec` 条件交换公式复用

### 3.3 比较器

- `Machines/kona-pcr-compare.hpp`
- `Machines/kona-dcf-compare.hpp`

## 4. 正确性状态

### 4.1 默认主路径未破坏

已验证：

```text
legacy-top1 双进程运行正常退出
```

### 4.2 Cong 适配层双进程测试

已验证：

```text
CONG_PCR_TOPK PASS n=64 k=5
CONG_DCF_TOPK PASS n=64 k=5
```

### 4.3 主路径精度

在当前 `arcene` 样例（`sample size=199`, `test size=1`）下：

```text
legacy-top1 预测准确率 = 1
cong-pcr    预测准确率 = 1
cong-dcf    预测准确率 = 1
```

说明：

- 当前 `Cong` 替换后端已经尽量保持了原 Kona 主路径语义。
- 我们还专门修正过“投票赢家选择”的语义，使其与原 `top_1(..., false)` 更一致。

## 5. 主路径 Benchmark 结果

以下结果来自 `arcene` 数据集、`sample size=199`、`test size=1` 的真实 `kona.x` 主路径运行。

### 5.1 Party 0 日志

| 后端 | Round count | Party total time (s) | Party Data sent (MB) | call_evaluate_nums | Evaluation total time (s) |
|---|---:|---:|---:|---:|---:|
| `legacy-top1` | 89 | 0.0735292 | 0.071456 | 2018 | 0.0672765 |
| `cong-pcr` | 293 | 0.00946171 | 0.093202 | 50 | 0.00165793 |
| `cong-dcf` | 90 | 0.0213393 | 0.050360 | 50 | 0.00188577 |

### 5.2 Party 1 日志

| 后端 | Accuracy | Round count | Party total time (s) | Party Data sent (MB) | call_evaluate_nums | Evaluation total time (s) |
|---|---:|---:|---:|---:|---:|---:|
| `legacy-top1` | 1 | 88 | 0.0736303 | 0.071448 | 2018 | 0.0668005 |
| `cong-pcr` | 1 | 292 | 0.00955629 | 0.093194 | 50 | 0.00165803 |
| `cong-dcf` | 1 | 98 | 0.0214554 | 0.050352 | 50 | 0.00162949 |

## 6. 结果解读

### 6.1 legacy-top1

- 保持了原 Kona 结构
- 轮次较低
- 但 `call_evaluate_nums` 很高：`2018`
- `evaluate()` 相关时间占比很高

### 6.2 cong-pcr

- 轮次显著增加
- 发送字节也更高
- 但总时间反而最低
- `call_evaluate_nums` 降到 `50`

说明：

```text
Cong 网络 + PCR 比较
在这个主路径样例下
明显用更多通信换掉了大量 DCF 计算
```

### 6.3 cong-dcf

- 轮次和 `legacy-top1` 接近
- 通信量比 `legacy-top1` 还低
- 总时间也明显低于 `legacy-top1`
- `call_evaluate_nums` 同样显著下降到 `50`

说明：

```text
Cong 网络本身就能减少比较调用次数
即使仍用 DCF comparator
主路径也能受益
```

## 7. 当前工程化程度

### 7.1 已达到的程度

当前可以认为已经达到：

```text
主路径可切换后端
默认行为不变
Cong 网络可真正驱动 Kona 风格 top-k
PCR / DCF 两种比较器都能挂到同一个网络上
主路径 benchmark 可以直接运行
```

### 7.2 还没有做的事

当前还没有做到：

- 没有把 `KNN_party_base` 的 `top_1()` 抽成统一虚接口后端体系
- `cong-pcr` / `cong-dcf` 还只接在 `KNN_party_optimized::run()` 主流程
- 还没有做更多数据集、多 query、多次重复实验
- 还没有把 `Cong` 主路径 benchmark 自动汇总到脚本里

### 7.3 工程结论

如果以“尽量不碰坏原主路径”为原则，当前状态已经比较适合后续继续迭代：

- 默认 `legacy-top1` 保留
- `cong-pcr` 可作为更快的新候选后端
- `cong-dcf` 可作为“只换网络、不换比较器思路”的中间基线

因此现在的工程状态可以描述为：

```text
已完成主路径接入
已完成基本正确性验证
已完成单数据集样例 benchmark
适合继续扩展实验与代码清理
```
