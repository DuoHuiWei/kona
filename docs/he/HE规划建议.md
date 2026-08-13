# HE规划建议

本文记录当前对 HE 对比组的后续建议、尚未收口的点，以及继续推进时的优先级判断。

## 1. 当前阶段结论

当前最小环境接入已经成立，且 BFV 与 CKKS 两条候选路线的公开 API 边界已经基本查清。

当前正式结论是：

- BFV 系数编码和 `ciphertext × plaintext polynomial` 前半段成立
- BFV→BinFHE 的桥在当前公开 API 约束下不可落地
- 主线正式切换为 `CKKS 距离 + CKKS↔FHEW 比较 + Cong Top-k 网络`

同时，CKKS 主线的当前状态要表述得更准确一些：

```text
单次 compare-and-swap 已成立；
链式 compare-and-swap 小网络已成立；
比较分辨率对 scaleSign 明显敏感，并且已经通过 quick 模式测到部分趋势。
```

因此后续工作不应该再继续扩建 BFV 正式模块，而应开始围绕 CKKS 主线解决“比较分辨率”和“多层链式执行”的收口问题。

## 2. 建议的推进顺序

### 第一阶段：稳定最小入口

目标：

- `kona-he.x` 可稳定编译
- `kona-he.x` 可稳定运行最小 OpenFHE smoke
- `Makefile` 中 OpenFHE 依赖路径可配置
- `HECompare/CMakeLists.txt` 可独立编译 smoke test

建议：

- 保持 `kona-he.x` 尽量简单
- 不要在这一步引入真实 KNN 业务逻辑
- 不要急着让 `kona-he.x` 依赖大量 Kona 内部模块

### 第二阶段：搭好 CKKS 主线骨架

目标：

- 在 `HECompare/` 中建立清晰的职责边界
- 先围绕 CKKS 距离与方案切换比较建立最小测试闭环

建议先演化成：

- `tests/openfhe_ckks_distance_compare_test.cpp`
- 之后再拆分出：
  - `ckks_distance`
  - `ckks_compare_swap`
  - `ckks_topk`

这一步的重点是“由功能驱动增量建文件”，不是“先把抽象层摆满”。

### 第三阶段：接入最小业务复用

目标：

- 先复用 Kona 的数据读取和基础统计习惯
- 不直接把现有双方 MPC 比较器、分享类型和通信模型塞进 HE 主流程

建议优先复用：

- `Tools/octetStream`
- `Tools/time-func`
- `Tools/TimerWithComm`
- `Tools/benchmarking`
- `Tools/ExecutionStats`

建议谨慎对待：

- `RealTwoPartyPlayer`
- 强依赖 party id 的抽象
- 强绑定在线轮数语义的通信统计

### 第四阶段：做真实 HE KNN 路径

目标：

- 设计 query 编码方式
- 设计训练集组织方式
- 设计距离计算和结果返回方式
- 固定要对比的指标

这里的关键不是“先把代码写出来”，而是先想清楚对比口径。

## 3. 当前未收口的问题

### 3.1 `kona-he.x` 和 `HECompare/` 的边界

当前建议已经收敛为：

- `Machines/kona-he.cpp` 作为唯一正式入口
- `HECompare/` 集中放正式实现和测试

不要再保留与 `HECompare/` 平级的另一套正式实现名字。

### 3.2 要不要尽快复用 Kona 数据读取代码

建议是“要复用，但不要立刻深绑”。

原因：

- 现在最重要的是先把 HE 路径自己站稳
- 太早深绑现有 `kona.cpp` 内部流程，会让重构成本变高
- 数据格式和结果口径可以先对齐，内部实现可以逐步靠近

### 3.3 为什么不再继续深挖 BFV

当前不是数学上证明 BFV 永远不可行，而是已经证明：

```text
在不修改 OpenFHE、不依赖私有实现、只使用公开 API 和小型适配层的约束下，
BFV→BinFHE 的桥没有打通。
```

对本项目来说，这已经足够作为停线条件。

继续往下做会从“做单服务器对照组”变成“扩展 OpenFHE 密码库”，不再符合当前主线目标。

## 4. 当前未完全收口的动作

下面这些事情已经开始做，但还没有完全收口，后续应优先围绕它们推进。

### 4.1 `compare_resolution` 已完成若干关键校准，但不是 full sweep

当前已经切换为 TCGA 专用分辨率测试：

- `openfhe_tcga_compare_resolution_test.cpp`

早期实际完成的是：

```text
quick 模式
```

而不是完整的全量扫描。

当前 quick 模式已经给出一个有价值的趋势：

- `scaleSign=1` 时，对 `gap=0.1` 不稳定
- `scaleSign=64` 和 `scaleSign=512` 时，对 `gap=0.1` 已经能稳定区分
- quick 模式当前实际输出为：

```text
scale_sign=1
gap=1 stable=true
gap=0.1 stable=false

scale_sign=64
gap=1 stable=true
gap=0.1 stable=true

scale_sign=512
gap=1 stable=true
gap=0.1 stable=true

minimum_correct_compare_gap=0.1
quick_mode=true
```

这说明：

- 当前比较分辨率明显受 `scaleSign` 影响
- 之前 `minimum_correct_compare_gap=-1` 不能再被理解为“路线不通”
- 但也不能把 quick 模式结果误记成“完整扫描已经完成”

因此必须明确记录：

```text
tcga full compare-resolution sweep = not finished
tcga quick compare-resolution trend = historical baseline
tcga P50 / P05 / min-gap local calibration = completed
tcga full-range safety probe = completed for high scaleSign boundary
```

后续已经基于这些校准结果固定了第一版正式 benchmark profile。这个 profile 不是“所有最小距离差都可稳定分辨”的证明，而是在当前 TCGA 输入范围、比较稳定性和 full-range safety 之间选择的工程基线。

### 4.2 链式 compare-and-swap 已经通过小网络，但还不是完整 Cong 网络

当前已经新增并运行了：

- `openfhe_ckks_compare_chain_test.cpp`

它已经验证了：

```text
4 个候选
3 层固定 sorting network
上一层同态输出直接进入下一层
中间不解密
最后结果正确
```

这说明“链式多层执行”在当前小网络上已经成立。

当前链式测试已经输出了每一层的 level 变化：

```text
level_0_input  : 所有 score/label level = 0
level_0_output : 所有 score/label level = 13
level_1_output : 所有 score/label level = 14
level_2_output : score/label level 出现 14/15 分化
```

并且最终测试通过：

```text
openfhe-ckks-compare-chain-test passed
```

这说明在当前小网络规模下：

- 上一层输出可以直接进入下一层比较
- 至少三层固定 sorting network 没有立刻因为层级问题失败
- 但 level 已经开始分化，后续更深网络仍然需要继续盯层级和深度

但这还不能直接推出：

- `n=8, k=3` 的完整 Cong 网络一定稳定
- 更深层网络一定没有层级或深度问题
- Arcene 尺度下的真实网络一定稳定

因此当前正确表述应是：

```text
small chained compare-and-swap network = verified
full Cong schedule = not yet verified
```

### 4.3 正式模块已拆分，但仍然只是“小模块 + 回归测试”阶段

当前已经正式拆出了：

- `ckks_context`
- `ckks_distance`
- `ckks_compare`

并保留：

- `openfhe_ckks_distance_compare_test.cpp`

作为回归测试。

这一步是对的，但还不能把它误认为：

```text
正式 HE-KNN 模块已经完成
```

现在仍然只是：

```text
正式小模块 + 回归基线阶段
```

## 5. 我对后续实现的建议

### 建议一：先做一个极小的 CKKS 距离与比较闭环

先不要上完整 Arcene。

第一份真正重要的测试应是：

- `openfhe_ckks_distance_compare_test.cpp`

最小闭环只包含：

- CKKS 加密查询
- 服务器端简化欧氏分数
- `EvalCompareSchemeSwitching`
- score / label 的加密 compare-and-swap

### 建议二：尽早固定 CKKS 误差与比较指标

后面最容易乱的是指标口径。

建议尽早统一输出：

- dataset
- dimension
- query_count
- total_time
- average_time_per_query
- keygen_time
- encode_time
- encrypt_time
- server_compute_time
- decrypt_time
- upload_bytes
- download_bytes
- total_bytes
- max_abs_error
- max_relative_error
- compare_error_rate
- topk_consistency

等正式方案稳定后，再继续扩展更细的 HE 操作计数。

### 建议三：区分“BFV 证据文件”和“CKKS 主线文件”

建议长期区分：

- smoke：验证依赖、链接、最小运行
- prototype：验证接口和流程
- benchmark：输出正式实验指标
- final experiment：用于论文或正式汇报的数据采集

另外还要区分：

- BFV 正确性/边界证明文件
- CKKS 正式主线文件

这样后面回看历史时，不会把“已停止的 BFV 分支”和“继续推进的 CKKS 主线”混在一起。

## 6. 正确推进顺序

当前不要马上做完整 Cong schedule。虽然正式 benchmark profile 已经固定，但 Cong 网络还需要先从小规模、可解释的路径继续验证。

正确顺序现在应当改为：

1. `CKKS/FHEW` 比较分辨率扫描
2. 选定初步 `scaleSign` 和输入归一化范围
3. 四元素三层链式 `compare-and-swap`
4. 解决层级和深度问题
5. `n=8, k=3` 的 Cong 网络
6. 再考虑并行批处理

而不是：

```text
fresh compare 通过
→ 直接 Cong 网络
```

当前状态对应到这条顺序上，应记录为：

- 第 1 步：已完成 quick、P50、P05、min-gap local 与 full-range safety 探针；不是 full exhaustive scan
- 第 2 步：正式 benchmark profile 已固定，第一版 `scaleSign = 262144`
- 第 3 步：已经通过
- 第 4 步：仍未完全收口
- 第 5 步：还没开始
- 第 6 步：明确先不要做

## 7. 后续性能问题要提前记住

当前结构是：

```text
一个候选一个 CKKS 密文
一次 comparator 调一次 EvalCompareSchemeSwitching
```

这非常适合正确性测试，但大概率不适合作为最终性能版本。

OpenFHE 的接口允许一次从 CKKS 密文中抽取并处理多个值，`numCtxts` 就是抽取值的数量。

因此以后 Cong 每一层互不依赖的比较器应该考虑：

- 把同一层多个 `lhs` 分数打包到一个 CKKS 密文
- 把多个 `rhs` 分数打包到另一个 CKKS 密文
- 一次 `EvalCompareSchemeSwitching`
- 同时得到多个比较位

否则在 `n=199` 时，如果每个 comparator 都独立做一次 scheme switching，单服务器对照组可能慢到失去实际实验价值。

但这属于：

```text
链式正确性通过后的优化阶段
```

现在先不要做。

换句话说，当前结构：

```text
一个候选一个 CKKS 密文
一次 comparator 调一次 EvalCompareSchemeSwitching
```

是正确性优先的结构，不是最终性能优先的结构。

因此后续如果进入 `n=199`，一定要考虑：

- 同层多个 lhs 分数打包
- 同层多个 rhs 分数打包
- 一次 scheme switching 同时得到多个比较位

但这一步必须排在：

```text
compare_resolution 收口
链式 compare-and-swap 收口
小规模 Cong 网络通过
```

之后。

## 8. 我建议暂时不要做的事

在 HE 主路径还没固定前，暂时不要急着做：

- 直接把 HE 逻辑塞进 `Machines/kona.cpp`
- 直接重写大规模 10000 维完整同态 KNN
- 直接把 OpenFHE 和现有 MPC sharing 类型强行混合
- 直接引入更多加速依赖再叠复杂度
- 过早做过深的架构抽象
- 先创建大量没有实现内容的占位头文件
- 再继续深挖 BFV→BinFHE 公共桥
- 退回“客户端解密完整多项式”的假正式方案
- 还没做完分辨率/链式验证就直接扩到完整 Cong 网络
- 还没稳定正确性就提前做批量并行 comparator 打包优化

这些事不是不能做，而是现在做会让排错和对比都变难。

## 9. 下一步的最优先任务

如果继续推进，我建议优先顺序如下：

1. 保留并整理 BFV 相关证明文件与结论文档
2. 保持 `openfhe_ckks_distance_compare_test.cpp` 作为回归基线
3. 按固定 benchmark profile 继续做 `n=8, k=3` 的 Cong 网络 smoke test
4. 记录 Cong 网络中的层级、深度、比较次数和错误率
5. 如果正确性或速度不足，再单独研究 packed compare、分层 `scaleSign` 或 range-aware compare
6. 最后才考虑接完整 Top-k、accuracy 和正式 benchmark 逻辑

这个顺序比较稳，也最容易保住当前已经验证好的技术边界。

## 10. TCGA 正式 Benchmark Profile 固化结论

当前 HE 单服务器对照组的正式 benchmark profile 固定为：

```text
TCGA_FORMAL_BENCHMARK_PROFILE = FIXED
slot_count = 4096
ring_dimension = 8192
multiplicative_depth = 20
scaleSign = 262144
pLWE = 131072
scaling_technique = FLEXIBLEAUTOEXT
fhew_parameter_set = STD128
```

这组参数对应当前 TCGA-PANCAN 输入口径：

```text
data = UCI TCGA-PANCAN 原始 data.csv / labels.csv
feature_count = 20531
global_scale = 21.0
distance = sum((x / 21 - q / 21)^2) / 20531
chunk_count = 6
chunk_layout = 4096 + 4096 + 4096 + 4096 + 4096 + 51
```

选择 `scaleSign = 262144` 的含义要严格表述：

- 它是当前正式 benchmark 的工程基线参数。
- 它已经覆盖 P50 / P05 级别比较校准中的稳定需求。
- 它保留了比极高 `scaleSign` 更大的 full-range safety 空间。
- 它不等价于“TCGA 全局最小正 gap 一定稳定可分辨”。
- 若后续必须追求最小 gap 正确性，应单独研究分层缩放、局部归一化、range-aware compare 或其他比较策略。

因此后续实验报告中应使用：

```text
BENCHMARK_PROFILE = TCGA_FORMAL_BENCHMARK_PROFILE
PACKED_COMPARE = NOT_IMPLEMENTED
MIN_GAP_GLOBAL_STABILITY = NOT_CLAIMED
FULL_TOPK_CORRECTNESS = NOT_CLAIMED_UNTIL_NETWORK_TEST
```

不要再把 `scaleSign = 8388608` 作为正式默认参数。它能通过局部 min-gap 探针，但 full-range safety 探针已经暴露出高缩放下的模空间边界风险，不适合作为当前完整 benchmark 的默认 profile。
