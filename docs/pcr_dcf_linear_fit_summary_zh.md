# PCR / DCF 单层比较器规模拟合总结

## 1. 实验口径

本总结对应 Kona 单层 `compare-only` benchmark，不包含 secure swap、Top-k、投票或 correctness reveal 时间。

- 网络：无 netem，容器 `lo` 为 `noqueue`，不附加 LAN/WAN 延迟。
- 进程：P0 使用 CPU0，P1 使用 CPU2；二者属于不同物理核心，非 SMT sibling。
- 数据：确定性 seed=`20260819`，PCR/DCF 使用相同构造规则。
- 统计：每个规模使用独立 trial 的中位数；PCR 和 DCF 均采用升序/降序交错 sweep。
- PCR：10 轮 sweep，每个点每次内部执行 1000 次 compare。
- DCF：10 轮 sweep，使用 `ceil(50000/m)` 次内部 compare，使不同规模的单个 trial 保持约 1 秒计时窗。
- 每次 trial 的 online 时间除以内循环次数，得到单次 batch compare 时间。
- correctness smoke 独立于正式 online timer，全部通过。

## 2. 测试规模

PCR 扩展实验的规模为：

```text
50, 60, 70, ..., 200,
300, 400, 500, ..., 2000
```

也就是 `50..200` 每 10 一个点，`200..2000` 每 100 一个点。`m=200` 的重复数据合并后取中位数。

DCF 实验的规模为：

```text
50, 100, 150, 200, 300, 400, ..., 2000
```

## 3. 代表性实测数据

下表使用各规模的 online 中位数，单位为 ms。PCR 和 DCF 的独立实验中位数不是同一进程内配对样本，因此表中的倍数只作为规模趋势参考，不替代严格配对 benchmark。

| m | PCR online | DCF online | DCF/PCR 时间比 | 较快方案 |
|---:|---:|---:|---:|---|
| 50 | 0.174 | 0.992 | 5.71x | PCR |
| 100 | 0.202 | 1.975 | 9.75x | PCR |
| 150 | 0.196 | 2.947 | 15.01x | PCR |
| 200 | 0.206 | 3.952 | 19.22x | PCR |
| 500 | 0.296 | 9.798 | 33.10x | PCR |
| 1000 | 0.495 | 19.375 | 39.13x | PCR |
| 1500 | 0.633 | 29.273 | 46.25x | PCR |
| 2000 | 0.917 | 38.764 | 42.27x | PCR |

上述表中的 DCF/PCR 比值定义为：

```text
DCF/PCR = DCF online median / PCR online median
```

大于 1 表示 PCR 更快。

## 4. PCR 拟合

### 4.1 推荐的全区间 online 模型

在 `50 <= m <= 2000` 范围内，将每个不同 `m` 的中位数等权进行普通最小二乘线性拟合：

```text
T_PCR_online(m) = 0.139664 + 0.000370386 * m  ms
```

等价地：

```text
T_PCR_online(m) = 0.139664 ms + 0.370386 us * m
```

拟合质量：

```text
R2                       = 0.988656
平均绝对百分比误差 MAPE  = 6.413%
中位绝对百分比误差       = 5.724%
P90 绝对百分比误差       = 12.676%
最大绝对百分比误差       = 14.255%  (m=90)
RMSE                     = 0.025257 ms
```

因此这条直线可以作为 PCR 在 `m=50..2000` 内的工程估算式，通常误差约为 5% 到 10%，保守上界约 15%。

### 4.2 PCR 计算部分的分段观察

原先由 `m=50..200` 得到的计算拟合为：

```text
compute_ms = 0.013910 + 0.000300820 * m
```

扩展到 `m=200..2000` 后，全区间计算拟合变为：

```text
compute_ms = -0.001140 + 0.000366782 * m
R2 = 0.992557
```

更细地看：

```text
m=200..1000:
compute_ms = 0.004981 + 0.000333831 * m
R2 = 0.999674

m=1000..2000:
compute_ms = -0.102785 + 0.000433790 * m
R2 = 0.970130
```

解释：PCR 仍然是线性增长，但 `m` 超过约 1000 后边际计算成本有所升高。可能原因包括消息缓冲区扩容、向量复制和 cache 压力；协议依赖深度没有增加，因此不能解释为 PCR 多了协议轮次。

### 4.3 PCR 通信结构

当前 PCR compare+B2A 路径在本 benchmark 中表现为：

```text
sent_bytes       = 70 * m
measured rounds  = 8 logical interactions
```

其中 PCR Boolean comparison 的理论依赖深度为 7，加入 B2A 后理论深度为 8；本实验的实测逻辑交互也为 8，且不随 `m` 增加。PCR 在线时间主要由固定通信阶段加上随 `m` 增长的本地计算组成。

## 5. DCF 拟合

### 5.1 推荐的全区间 online 模型

对 DCF 的 `m=50..2000` 各规模中位数进行同样的等权普通最小二乘拟合：

```text
T_DCF_online(m) = 0.078516 + 0.019362074 * m  ms
```

等价地：

```text
T_DCF_online(m) = 0.078516 ms + 19.362074 us * m
```

拟合质量：

```text
R2                       = 0.999979
平均绝对百分比误差 MAPE  = 0.580%
中位绝对百分比误差       = 0.180%
P90 绝对百分比误差       = 1.222%
最大绝对百分比误差       = 5.510%  (m=50)
RMSE                     = 0.054972 ms
```

因此 DCF 在 `m=50..2000` 内基本可以视为严格线性，使用该公式进行成本估计的误差通常低于 1% 到 2%。

### 5.2 DCF evaluate 成本

DCF evaluate 自身的拟合为：

```text
dcf_evaluate_ms = -0.004513 + 0.019204773 * m
R2 = 0.999983
MAPE = 0.191%
```

这说明 DCF 总在线时间几乎完全由 evaluate 成本决定。

### 5.3 DCF 通信结构

当前 DCF compare-only 路径在本 benchmark 中表现为：

```text
sent_bytes          = 8 * m
measured rounds     = 1 logical interaction
dcf_evaluate_calls  = 2 * m
```

DCF 用更少的通信轮次和更少的通信字节换取了较高的本地 evaluate 计算成本。

## 6. 拟合方法说明

对于每个后端，具体步骤如下：

1. 对每个规模 `m` 收集多个独立 trial。
2. 对该规模的 trial online 时间取 median，降低偶发调度噪声影响。
3. 令 `x=m`，令 `y=online_median_ms`。
4. 使用普通最小二乘拟合：

```text
y = a + b*x
```

5. 对每个点计算预测值 `y_hat=a+b*x`。
6. 计算：

```text
R2   = 1 - SSE/SST
APE  = abs(y_hat-y)/y * 100%
MAPE = mean(APE)
RMSE = sqrt(mean((y_hat-y)^2))
```

7. `R2` 衡量整体线性趋势；MAPE、P90 和最大 APE 衡量实际预测误差。

拟合时每个不同的 `m` 等权，不让某个规模因为内部 trial 数更多而主导回归。拟合只用于描述和估算，不改变 PCR/DCF 的协议或 Hybrid threshold。

## 7. 最终结论

```text
PCR:
  T_PCR(m) ≈ 0.139664 + 0.000370386m ms
  在 50..2000 内近似线性，但 1000 左右后斜率略有增加。

DCF:
  T_DCF(m) ≈ 0.078516 + 0.019362074m ms
  在 50..2000 内几乎严格线性，主要由 DCF evaluate 主导。
```

无延迟环境下，`m` 增大时 PCR 的批量优势迅速扩大；DCF 的低通信轮次只有在存在明显网络延迟时才可能抵消其较高的本地 evaluate 成本。因此不能把无延迟拟合结果直接当作 LAN/WAN 下的最终 Hybrid threshold，网络 profile 变化后应重新标定。

## 8. 原始结果位置

- PCR `m=50..200`：[raw_results.csv](/home/u7231/kona-work/Kona/KNN-experiment-res/native-compare-only-m50-200-stable-20260902-045732/raw_results.csv)
- PCR `m=200..2000`：[raw_results.csv](/home/u7231/kona-work/Kona/KNN-experiment-res/native-pcr-scaling-m200-2000-20260902-051117/raw_results.csv)
- DCF `m=50..2000`：[raw_results.csv](/home/u7231/kona-work/Kona/KNN-experiment-res/native-dcf-scaling-m50-2000-20260902-052009/raw_results.csv)
- PCR 汇总：[summary.md](/home/u7231/kona-work/Kona/KNN-experiment-res/native-pcr-scaling-m200-2000-20260902-051117/summary.md)
- DCF 汇总：[summary.md](/home/u7231/kona-work/Kona/KNN-experiment-res/native-dcf-scaling-m50-2000-20260902-052009/summary.md)
