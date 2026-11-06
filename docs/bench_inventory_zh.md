# Bench与实验脚本分类清单

历史盘点日期：2026-11-03。下文数量和旧入口表是清理前快照，不再代表当前可执行入口。

2026-11-04已收束当前四方案为两个实验文件：`Machines/knn-bench.cpp`＋`Scripts/run-knn-experiment.py`，直接运行 `python3 -u Scripts/run-knn-experiment.py --scheme hybrid`；方案可选constantpp/secknn/kona/hybrid。没有第三个启动文件。规范与命令见 [bench_recording_protocol.md](bench_recording_protocol.md)。旧重复入口已移除，带哈希原件归档于 `KNN-experiment-res/bench-consolidation-20261104/`，历史结果未删除。

## 1. 先看这几条主线

| 任务 | C++入口 | 调度脚本 |
|---|---|---|
| 当前分块Cong＋完整/裁剪矩阵及末端对照 | `Machines/kona-matrix-bench.cpp` | `Scripts/run-matrix-experiment.sh` |
| Kona连续Top-1＋改造批量DCF基线 | `Machines/kona-mainpath-esd-topk-dcf-bench.cpp` | `Scripts/run-kona-bubble-dcf-topk-only-bench.sh` / `run-kona-bubble-dcf-topk-lan-wan.sh` |
| 四真实数据集Cong＋PCR/Hybrid | `Machines/kona-topk-network-bench.cpp` | `Scripts/run-cong-pcr-topk-datasets-bench.sh` / `run-cong-hybrid-topk-datasets-bench.sh` |
| Kona距离单独测量 | `Machines/kona-distance-only-bench.cpp` | `Scripts/run-kona-distance-only-bench.sh` |
| PCR/DCF单层compare-only | `Machines/kona-pcr-dcf-bench.cpp` / `kona-pcr-dcf-layer-bench.cpp` | 网络扫描等脚本，见第4节 |
| PCR/DCF compare＋swap | `Machines/kona-pcr-dcf-compare-swap-bench.cpp` | `Scripts/run-pcr-dcf-compare-swap-network-bench.sh` |
| NHSS / Rotated-MSB | `Machines/kona-nhss-distance-data-bench.cpp` / `kona-cong-rmsb-dataset-size-bench.cpp` | 各C++入口参数驱动 |

当前矩阵调用关系：

```text
run-matrix-experiment.sh
    → kona-matrix-bench.x
    → kona-reduced-depth-cong-topk.hpp：规划＋执行
    → 已有Cong生成器、PCR/DCF比较器和swap
    → 可选kona-mask-vote.hpp：独立计时，输出类别份额
```

详细接口见 [matrix_modules.md](matrix_modules.md)。最新计时/通信目标规范见 [bench_recording_protocol.md](bench_recording_protocol.md)。

## 2. 数量与统计范围

| 类型 | 文件数 | 统计说明 |
|---|---:|---|
| Kona相关C++ bench | 26 | 当前`Machines/*bench.cpp` |
| HE相关C++ bench | 2 | `HECompare/tests`下名称含bench的C++文件 |
| 业务调度Shell脚本 | 16 | `Scripts`下14个＋根目录2个；包含2个兼容转发脚本 |
| 当前容器网络配置脚本 | 1 | `Scripts/container-network.sh`，包含5个profile |
| ConstantPP功能测试入口 | 10 | 不计入上述28个bench |
| 新矩阵双进程回归脚本 | 1 | `Scripts/test-matrix-modules.py`，不是性能主实验 |

28个bench程序不等于28套协议方案。本文不把MP-SPDZ通用运行脚本、依赖库、参数CSV、结果目录中的归档代码副本计成独立bench，也不将全部`*-test.cpp`都算作性能bench。

## 3. 26个Kona C++ bench分类

下表文件均位于`Machines/`，一般对应同名`.x`。分类是用途判断，不是可以立即删除的清单。

| 分类 | 文件 | 做什么 / 当前定位 |
|---|---|---|
| 主性能入口 | `kona-matrix-bench.cpp` | 当前统一分块矩阵入口；old/full/pruned、逆序、OCTM、OB-SSN；完整Top-k或末端-only；可选独立投票 |
| 主性能入口 | `kona-mainpath-esd-topk-dcf-bench.cpp` | Kona连续Top-1＋改造批量DCF，v2双方max＋阶段/外层计时；见统计规范第10节 |
| 旧综合入口 | `kona-topk-network-bench.cpp` | legacy、Cong、DCF、Hybrid及旧standalone PCR矩阵横向测试；各mode范围需分别核对 |
| 距离 | `kona-distance-only-bench.cpp` | Kona本地Distance-only；不能与ConstantPP通信距离核混称 |
| 距离 | `kona-esd-bench.cpp` | 较早Kona ESD计时入口；与distance-only存在用途交集 |
| 距离 | `kona-beaver-esd-bench.cpp` | Beaver乘法形式的距离实验；不是原Kona本地零在线通信核 |
| 离线准备 | `kona-preprocess-bench.cpp` | DCF及距离预处理耗时，不计作在线协议性能 |
| 离线准备 | `kona-beaver-triple-prep-bench.cpp` | 三元组准备成本 |
| 比较器 | `kona-pcr-dcf-bench.cpp` | PCR/DCF正确性检查及批量compare-only性能 |
| 比较器 | `kona-pcr-dcf-layer-bench.cpp` | 固定层宽的compare-only、内部重复测量 |
| 比较器 | `kona-pcr-dcf-compare-swap-bench.cpp` | compare＋swap成本，不等同compare-only |
| 比较器 | `kona-pcr-batchsize-bench.cpp` | PCR batch大小对成本的影响 |
| 比较器 | `kona-pcr-layer-depth-bench.cpp` | 多层串行PCR累积成本 |
| 比较器 | `kona-layer-cost-calibration-bench.cpp` | 单层PCR/DCF成本标定 |
| Cong交叉点 | `kona-cong-breakpoint-bench.cpp` | Cong＋纯PCR/DCF的规模交叉比较 |
| 混合策略 | `kona-cong-hybrid-breakpoint-bench.cpp` | 混合后端与纯后端比较 |
| 混合策略 | `kona-cong-hybrid-comparator-breakpoint-bench.cpp` | 按比较器宽度切换策略的交叉点实验 |
| 混合策略 | `kona-cong-hybrid-auto-selector-bench.cpp` | 自动后端选择策略实验 |
| 混合策略 | `kona-cong-wan-workload-aware-bench.cpp` | WAN工作量感知后端选择 |
| 调度诊断 | `kona-cong-layer-cpu-overlap-bench.cpp` | 同核后台负载与协议层重叠 |
| 调度诊断 | `kona-cong-layer-multitask-crossover-bench.cpp` | 多任务负载下的交叉点 |
| 调度诊断 | `kona-cong-layer-wan-load-sweep-bench.cpp` | WAN＋后台负载比例扫描 |
| 层级交叉点 | `kona-cong-layer-pcr-dcf-crossover-bench.cpp` | Cong式单层比较与交换的PCR/DCF对照 |
| Shao相关 | `kona-nhss-distance-data-bench.cpp` | NHSS距离的数据集及batch实验 |
| Shao相关 | `kona-cong-rmsb-dataset-size-bench.cpp` | Cong＋Rotated-MSB规模实验；不是另一份Cong生成器 |
| 结构验证 | `kona-octm-obssn-structure-bench.cpp` | OCTM、OB-SSN及裁剪矩阵的结构比较和明文验证，不是在线协议吞吐 |

### 矩阵旧名不再是独立实现

```text
kona-matrix-bench.x
kona-reduced-depth-hybrid-bench.x
kona-reduced-depth-cong-bench.x
```

以上3个可执行名称都链接`Machines/kona-matrix-bench.o`。
旧`kona-reduced-depth-hybrid-bench.cpp`和`kona-reduced-depth-cong-bench.cpp`源码已从`Machines`删除，原文保存在本次重构的`source-before`目录。

`kona-topk-network-bench.cpp --mode matrix`仍是旧standalone PCR矩阵路径，包含rank、mask及标签票数统计，不能直接与新bench的纯矩阵末端时间混表。

## 4. 16个调度脚本分类

前14个位于`Scripts/`，最后2个位于仓库根目录。

| 脚本 | 做什么 / 状态 |
|---|---|
| `run-matrix-experiment.sh` | 当前统一矩阵runner；支持CSV参数、模式选择、后端、scope、重复、预热、轮换顺序和分方/max/aggregate汇总 |
| `run-reduced-depth-hybrid-experiment.sh` | 旧主runner名，现在只转发到统一runner，不是另一套实现 |
| `run-reduced-depth-cong-experiment.sh` | 更早纯PCR/DCF对照名，现在只转发；默认保留旧模式参数 |
| `run-lan-hybrid-threshold-sweep.sh` | LAN阈值扫描；仍查找旧`reduced-depth-hybrid-host-*`结果目录，与新runner目录失配，使用前需修复 |
| `run-kona-bubble-dcf-topk-only-bench.sh` | 四数据集Kona Top-k；不主动整形，实际网络见日志 |
| `run-kona-bubble-dcf-topk-lan-wan.sh` | 同一Kona基线的LAN/WAN实验；现在调用统一网络配置 |
| `run-cong-pcr-topk-datasets-bench.sh` | 四数据集Cong＋PCR |
| `run-cong-hybrid-topk-datasets-bench.sh` | 四数据集Cong混合后端；仍保留LAN190/WAN2808历史阈值，不是矩阵主线的10/6000 |
| `run-kona-distance-only-bench.sh` | 四数据集Kona距离 |
| `run-kona-esd-bench.sh` | 较早ESD实验调度，与新distance-only入口有交集 |
| `run-pcr-dcf-lan-bench.sh` | 较早四档规模比较；脚本名含LAN，但自身不负责施加LAN整形 |
| `run-pcr-dcf-compare-only-network-sweep.sh` | 多网络profile下compare-only规模扫描，包含Super-DC/Mid-LAN/Mid-WAN |
| `run-pcr-dcf-compare-swap-network-bench.sh` | LAN/WAN compare＋swap交错实验 |
| `run-he-ckks-distance-bench.sh` | HE距离分支；使用另一套容器配置，不应误作kona-dev主线入口 |
| 根目录`run-kona-baseline.sh` | Kona upstream基线，含绑核和启动记录，默认使用kona-upstream.x |
| 根目录`KNN-experiment.sh` | 很早的一体化四方案实验；开头make clean，优先归档候选，不作当前正式runner |

当前新runner的结果目录名为`KNN-experiment-res/matrix-bench-host-*`。旧脚本即使名称没变，转发后也不是旧输出契约，依赖旧目录或字段的分析脚本应检查。

## 5. 48组历史主实验：参数、脚本和结果

最符合“原来48组主力实验”的是：

```text
原run-reduced-depth-hybrid-experiment.sh
    → 原kona-reduced-depth-hybrid-bench.x
    → reduced_depth_48_cases.csv
    → old-hybrid / full-matrix-hybrid / pruned-matrix-hybrid各3次
```

参数文件：`KNN-experiment-res/reduced_depth_48_cases.csv`。

- N取200、256、300、512、800、1024、2000、4096、5000、7000、8192、13000、32768、50000、65536、102944。
- 每个N分别配K=5、17、31，合计48组。
- 每个网络48×3模式×3次＝432个trial，864条P0/P1分方记录。

| 历史结果目录（均在KNN-experiment-res下） | 配置 | 已核对记录 |
|---|---|---|
| `reduced-depth-hybrid-host-20260908-200107/` | LAN阈值10，共享2Gbps/0ms，CPU0/2，无预热 | 432 trial、864条分方记录，全部标记PASS；报告threshold10_report.md |
| `reduced-depth-hybrid-host-20260908-203011/` | WAN阈值6000，共享80Mbps/20ms，CPU0/2，无预热 | 432 trial、864条分方记录，全部标记PASS；报告wan_threshold6000_report.md |
| `reduced-depth-hybrid-host-20260908-174155/` | 更早LAN阈值190 | 作为阈值演变历史，不与阈值10结果改标混用 |

旧主runner和bench源码原文：
`KNN-experiment-res/matrix-module-refactor-20261103/source-before/`。

另一套48组是**修正版逆序预筛matrix-only**，不是上面的完整分块Top-k实验：

| 结果目录 | 范围 | 记录 |
|---|---|---|
| `reduced-depth-hybrid-host-20260909-161157/` | LAN10，构造有序候选块后的逆序预筛及矩阵 | 144 trial、288条分方记录，全部标记PASS |
| `reduced-depth-hybrid-host-20260909-161259/` | WAN6000，同一末端范围 | 144 trial、288条分方记录，全部标记PASS |

这套“修正版”指修复了逆序预筛入口；旧代码仍有计时内构造候选的问题，后来重构才移出。不能只凭PASS或“修正版”名称就认定其时间满足当前口径。

这些48组结果是重要历史证据，但不是新统计规范的完成证明。新的统一bench完成了小规模回归，尚未按最新网络条件和统计规范重新跑完整48组。

## 6. 参数文件不要当成多套脚本

| 文件 | 行数 | 用途 |
|---|---:|---|
| `KNN-experiment-res/reduced_depth_48_cases.csv` | 48 | 完整主参数表 |
| `Scripts/reduced-depth-hybrid-cases-core.csv` | 6 | 小规模核心场景 |
| `Scripts/reduced-depth-cong-cases-core.csv` | 6 | 旧纯后端核心场景 |
| `Scripts/reduced-depth-cong-cases.csv` | 14 | 较早扩展参数表 |
| `Scripts/reduced-depth-v2-core12-cases.csv` | 12 | v2结构场景 |
| `KNN-experiment-res/diagnostic_cases.csv` | 4 | 小规模诊断 |
| `KNN-experiment-res/reverse_8_cases.csv` | 8 | 两块逆序实验 |
| `KNN-experiment-res/reverse_prefilter_17_cases.csv` | 17 | 逆序预筛第一批规模 |
| `KNN-experiment-res/reverse_prefilter_remaining31_cases.csv` | 31 | 48组扣除17组后的其余规模 |

一个runner读取不同CSV即可覆盖这些场景，不需要每张参数表再维护一份执行脚本。

## 7. 测试、HE与网络辅助工具

- `Machines/kona-matrix-plan-test.cpp`：规划、边界与full/pruned mask一致性回归。
- `Machines/kona-matrix-topk-test.cpp`：旧standalone矩阵rank、mask及计票功能测试。
- `Scripts/test-matrix-modules.py`：小规模真实双进程集成回归，不是完整性能批处理。
- `Machines/kona-reduced-depth-cong-topk-smoke.cpp`：分块规划/结构smoke。
- ConstantPP的10个入口为`test-sec-ed`、`test-sec-shuffle`、`test-sec-bcom`、`test-sec-kmin`、`test-sec-eq`、`test-sec-fre`、`test-sec-max`、`test-sec-kc`、`test-sec-kc-batch`、`test-sec-kc-data`，位于`ConstantPP/tests/`。数据集入口可记录性能，但不能据文件数将每个单元测试算成独立方案。
- `HECompare/tests/openfhe_tcga_ckks_distance_benchmark.cpp`：CKKS距离。
- `HECompare/tests/openfhe_ckks_single_compare_swap_bench.cpp`：CKKS单次比较交换。
- `Scripts/measure-container-profile-throughput.py`：纯网络单向/双向吞吐、echo RTT和class计数，不执行密码协议。
- `Scripts/container-network.sh`：唯一整形入口，5个profile只改变速率/延迟，其余设置统一。旧配置脚本已归档删除。当前参数和实测见 [network_profiles.md](network_profiles.md)。

## 8. 后续清理建议

1. 保留新矩阵主bench/runner、48组参数、原始基线及回归测试。
2. 先处理已确认失配的run-lan-hybrid-threshold-sweep.sh，不把它当作当前可直接运行的正式扫描入口。
3. 旧矩阵可执行名和两个转发脚本可暂作兼容；它们不复制协议实现，删除前核对外部使用。
4. 优先评估KNN-experiment.sh、旧ESD调度和旧四档比较器调度的归档价值。
5. 五个Cong混合策略bench和多种负载诊断bench存在用途重叠，下一轮应先对照计时、后端策略、结果来源，再决定合并；不能只看名字相似就删除。
6. 保留历史日志/CSV/归档及版本对应关系。历史已跑过，不等于当前源码可无条件复现或已经满足新规范。

**统计口径待迁移：** 当前部分bench/汇总仍以P0发送量为主指标。最新要求是逐trial时间max(P0,P1)、发送量max(P0,P1)、轮数分别保留双方；不能宣称本清单中的全部入口已经完成该迁移。

原盘点阶段只整理文档；2026-11-04后续迁移与小规模验证见下文及对应回归报告。

`Scripts/kona-bench-log.sh`是逐party日志包装工具，不增加一套协议或业务调度入口；负责环境、网络快照和执行状态。


## 9. ConstantPP与Shao相关入口的后续迁移（2026-11-04）

新增1个Python业务调度入口`Scripts/run-reference-bench.py`，与上表16个Shell调度入口分开计数。它支持ConstantPP数据集完整在线KNN，以及Cong＋Rotated-MSB的模拟距离Top-k；双方各阶段时间/字节/动作与实际交换均保存，逐trial时间和字节取max。详见[reference_benchmarking.md](reference_benchmarking.md)。

相关性能入口并不只有这两个：ConstantPP有合成/批量/组件测试，Shao相关另有NHSS距离数据bench；本次仅迁移上述两个选定主入口及其统计支持，不删除其他入口。Rotated-MSB规模bench仍无NHSS距离和投票，完整在线端到端为NOT_IN_SCOPE。
