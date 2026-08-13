# 当前问题、欠缺与下一步建议

日期：`2026-07-24`

本文汇总当前 Kona + Cong/PCR/DCF 改造工作的主要问题、工程欠缺、实验欠缺，以及建议的下一步优先级。

## 1. 当前主要问题

### 1.1 主路径验证范围仍然偏小

虽然当前已经完成：

- `legacy-top1`
- `cong-pcr`
- `cong-dcf`

三种后端在 `kona.cpp` 主路径中的接入与双进程跑通，但主路径层面的验证仍然偏小：

- 主要基于 `arcene`
- 当前主路径只有 `1` 个 test query
- 还没有在更多 query、更大数据集、更大 `k` 上做系统回归

因此当前结论更准确地说是：

```text
主路径已接通
单样例正确性已验证
但尚未形成大规模稳定性结论
```

### 1.2 tcga-pancan 目前更偏“核测速”而非完整主路径评估

当前 `tcga-pancan` 已成功导入，且完成了：

- 预处理 benchmark
- ESD benchmark

但它目前使用的是：

```text
800 train + 1 test
```

这非常适合做：

- 单 query 欧氏距离性能
- 距离核成本
- 预处理成本

但还不适合直接当作“完整数据集主路径评估”的最终实验设置。

### 1.3 PCR 比较器仍有语义前提

当前 `PCR` 比较器的实现逻辑基于：

```text
sign((x - y) mod 2^64)
```

因此它当前更适合：

- 距离值范围可控
- `|x-y| < 2^63`

的场景。

这在当前 Kona 距离排序实验里通常是可接受的，但它还不是“任意 full-domain uint64 无符号比较器”。

目前这个前提已经写在代码注释里，但还没有做：

- 主路径断言
- 数据范围自动检查
- 更严格的实验边界说明

### 1.4 DCF 仍然明显慢于 PCR

DCF 路径已经做过多轮工程优化：

- 独立模块化
- 预加载 key/r
- fast evaluate 内核
- batch evaluate 外壳

但从 benchmark 结果看，DCF 依然明显慢于 PCR。

这说明：

```text
外围工程浪费已经基本清掉
当前瓶颈主要集中在 evaluate 内核本体
```

也就是说，继续优化 DCF 的难度已经明显抬高。

## 2. 工程欠缺

### 2.1 `kona.cpp` 主文件仍然偏重

当前虽然很多模块已经拆出去：

- `kona-cong-topk.hpp`
- `kona-cong-kona-adapter.hpp`
- `kona-pcr-compare.hpp`
- `kona-dcf-compare.hpp`

但主文件 `Machines/kona.cpp` 仍然很重，且仍然承担：

- 数据集配置
- 后端切换
- 主流程
- 原始 DCF 逻辑
- 原始 top_1 逻辑

后续如果继续叠加实验路径，这个文件会越来越难维护。

### 2.2 后端体系仍未完全统一

当前已经统一到了：

- `KNN_party_optimized::run()` 主路径
- Top-k 选择阶段
- winner label 选择阶段

但还没有把以下路径全部统一成同一套后端接口风格：

- `KNN_party_SecKNN`
- 其他 benchmark/旧实验入口
- 可能的多数据集运行控制

### 2.3 数据同步与实验脚本仍存在人工前置步骤

当前容器开发流已经形成：

- WSL 写代码
- Docker 编译和测试

但仍有几类人工动作：

- `Player-Data` 不自动随 `codex-container-build.sh` 同步
- `LAN.sh` / `WAN.sh` 需要手动执行
- `tc` 需要本机 `sudo`

这会影响实验的可复现性和批量运行体验。

### 2.4 benchmark 入口文件增多但说明分散

目前已经有较多独立 benchmark / test 入口，例如：

- `kona-pcr-dcf-bench.cpp`
- `kona-preprocess-bench.cpp`
- `kona-esd-bench.cpp`
- `kona-cong-kona-test.cpp`
- `kona-cong-topk-test.cpp`
- `kona-pcr-compare-test.cpp`
- `kona-share-conversion-test.cpp`

这本身不是坏事，但如果没有一份统一清单，后面很容易忘记：

- 哪个入口测什么
- 是否双进程
- 是否走主路径
- 是否只测局部组件

## 3. 实验欠缺

### 3.1 缺少 tcga-pancan 主路径正式对照

当前 `tcga-pancan` 已经具备：

- 数据导入
- 预处理测速
- ESD 测速

但还没有形成以下正式对照：

```text
legacy-top1
cong-pcr
cong-dcf
```

在 `tcga-pancan` 主路径上的统一测试结果。

### 3.2 缺少多 query 正确性回归

目前很多主路径验证仍是：

```text
test size = 1
```

这适合看单次查询时间，但不适合判断：

- 多 query 稳定性
- 准确率是否持续一致
- 后端切换后是否存在边界 query 偏差

### 3.3 缺少更系统的实验矩阵

目前已经有：

- `LAN` 四档 `PCR vs DCF`
- `WAN` 四档 `PCR vs DCF`
- `arcene` 主路径三后端对照
- `tcga-pancan` 预处理和 ESD benchmark

但还缺少真正统一的实验矩阵，例如：

| 数据集 | 网络条件 | 后端 | query 数 | 指标 |
|---|---|---|---|---|
| arcene | 无 netem | legacy/cong-pcr/cong-dcf | 1 | time/rounds/data |
| arcene | LAN | comparator benchmark | 多规模 | time/rounds/data |
| arcene | WAN | comparator benchmark | 多规模 | time/rounds/data |
| tcga-pancan | 无 netem | preprocess/esd | 1 | phase timing |
| tcga-pancan | 主路径 | legacy/cong-pcr/cong-dcf | 多 query | accuracy/time |

## 4. 当前最值得优先补的事

### 优先级 1：tcga-pancan 主路径对照

建议先补：

```text
tcga-pancan
legacy-top1 / cong-pcr / cong-dcf
主路径对照
```

因为这能把“新数据集导入”与“新后端接入”的价值真正连起来。

### 优先级 2：多 query 正确性回归

建议至少补一个：

```text
test_count > 1
```

的数据版本，验证：

- 预测准确率
- 后端切换一致性
- 主路径稳定性

### 优先级 3：统一实验入口说明

建议始终保留一份最新的：

```text
接口 / benchmark / test 说明清单
```

避免后续工程继续扩展后，入口越来越散。

## 5. 工程结论

当前项目状态可以总结为：

```text
核心改造已经从“局部实验代码”进入“主路径可切换后端”阶段
但仍处于实验工程化中期
```

也就是说：

- 已经具备主路径切换能力
- 已经具备独立 benchmark 能力
- 已经具备新数据集导入能力
- 但还没有形成足够完整的大规模主路径实验闭环

因此当前最合适的工作方式仍然是：

```text
继续沿着“主路径尽量不变 + 局部后端替换 + 基准实验逐步补全”推进
```

## 6. 基础模块真实性审计

这里按照当前实验标准做一份基础模块审计。

当前标准为：

```text
1. 逻辑必须真实
2. 通信必须真实
3. 字节数统计必须真实
4. 掩码 / 随机数 / Beaver 三元组中的 a,b,c 可以挂 0
```

### 6.1 可以继续作为基础模块使用

#### `Machines/kona-dcf-compare.hpp`

结论：

```text
满足要求
```

理由：

- 比较逻辑是真实 DCF 路径
- 有真实 `send/receive`
- 通信字节会进入 `Player` / `RealTwoPartyPlayer` 统计
- 当前优化只动了：
  - key/r 预加载
  - fast evaluate 内核
  - batch evaluate 外壳
- 没有把通信做假

#### `Machines/kona-pcr-compare.hpp`

结论：

```text
基本满足要求
```

理由：

- 层级 carry 比较逻辑是真实的
- 每层 packed AND 都有真实通信
- `B2A` 也有真实通信
- 字节统计真实
- 不真实的部分主要是：
  - `a=b=c=0`

这符合当前允许范围。

#### `Machines/kona-cong-topk.hpp`

结论：

```text
满足要求
```

理由：

- 这是公开网络生成模块
- 本来就不涉及秘密随机性
- 只负责：
  - `levels`
  - `output_wires`
  - 通用调度结构

#### `Machines/kona-cong-kona-adapter.hpp`

结论：

```text
满足要求
```

理由：

- 自己不伪造协议
- 只是：
  - 调 `PCR` 或 `DCF` 比较器
  - 调 Kona 风格 `SS_vec` 交换公式
- 通信真实性取决于底层比较器与乘法

### 6.2 可以继续用，但属于 L2 检查版

#### `Machines/kona-share-conversion.hpp`

结论：

```text
可以继续用
但需要明确它是 L2 检查版
```

拆分说明：

- `open_additive_batch_l2`
- `open_xor_batch_l2`
- `open_subtractive_batch_l2`

这些部分：

```text
逻辑真实
通信真实
字节统计真实
```

而下面这些部分：

- `mul_additive_kona_l2`
- `mul_vector_additive_kona_l2`
- `B2A_batch_l2`
- bit 级 `B2A`

虽然：

```text
协议步骤真实
通信真实
字节统计真实
```

但底层仍然是：

```text
a = b = c = 0
```

因此它们不是“真实随机 Beaver triple”，而是：

```text
真实通信 + 协议形状真实 + 三元组随机性置零
```

这仍在当前允许范围内。

#### `Machines/kona-beaver-esd-bench.cpp`

结论：

```text
适合作为当前要扩展的基础方向
```

理由：

- 它走的是：
  - 本地份额互减
  - 安全乘法平方
  - 累加成距离
- 底层复用的乘法通信是真实的
- 字节统计也是真实的
- 唯一不真实的是三元组随机性挂 0

这与当前要求最一致。

### 6.3 更像工具/测试壳，不建议当协议基础继续叠

#### `Machines/kona-preprocess-bench.cpp`

结论：

```text
适合测速
不适合继续当协议基础模块扩展
```

原因：

- 它只是原预处理组件的计时壳子

#### `Machines/kona-esd-bench.cpp`

结论：

```text
适合做原 Kona ESD 对照
不适合做新的基础协议模块
```

原因：

- 它复用的是 Kona 当前优化版距离核
- 更适合对照测速
- 不等同于你现在要推进的“Beaver 风格距离核”

#### 各类 `*-test.cpp`

结论：

```text
适合验证正确性
不适合直接当正式协议模块
```

### 6.4 当前最适合继续扩展的基础模块

按当前标准，后续如果要继续做工程化扩展，优先推荐：

```text
Machines/kona-dcf-compare.hpp
Machines/kona-pcr-compare.hpp
Machines/kona-cong-topk.hpp
Machines/kona-cong-kona-adapter.hpp
Machines/kona-beaver-esd-bench.cpp 对应的 Beaver 距离核思路
```

一句话总结：

```text
现在已经可以把“真实逻辑 + 真实通信 + 字节统计真实”当作筛选标准继续推进；
只有随机掩码、随机数、Beaver 三元组本身仍允许挂 0。
```
