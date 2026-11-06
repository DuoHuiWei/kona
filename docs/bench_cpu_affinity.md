# Bench固定CPU绑定：实现位置与迁移记录

> 2026-11-04入口收束：当前四方案使用 `Machines/knn-bench.cpp` 和 `Scripts/run-knn-experiment.py`，直接运行Python脚本，无额外启动文件。下文旧入口名是历史记录；当前命令和统计范围以 [统一规范](bench_recording_protocol.md) 开头为准。历史结果保留。


确认日期：2026-11-04（Asia/Shanghai）。固定P0→CPU 0、P1→CPU 2，单进程实验→CPU 0。没有可用固定CPU时失败，不自动换核。

最终规则：已有绑核实现的位置保留；无绑定的独立bench补C++；后续新增或修改实验优先C++ bench绑定，无法控制时才由脚本/taskset兜底。参数解析完成后、网络/工作线程创建及计时前调用，不能改动协议在线区间。

## 保留脚本绑定的现有入口

| C++入口 | 保留的脚本入口 |
|---|---|
| `Machines/kona-mainpath-esd-topk-dcf-bench.cpp` | `run-kona-bubble-dcf-topk-*.sh` |
| `Machines/kona-distance-only-bench.cpp` | `run-kona-distance-only-bench.sh` |
| `Machines/kona-topk-network-bench.cpp` | `run-cong-pcr/hybrid-topk-datasets-bench.sh` |
| `Machines/kona-matrix-bench.cpp` | `run-matrix-experiment.sh及旧名转发入口` |
| `Machines/kona-pcr-dcf-bench.cpp` | `run-pcr-dcf-compare-only-network-sweep.sh` |
| `Machines/kona-pcr-dcf-compare-swap-bench.cpp` | `run-pcr-dcf-compare-swap-network-bench.sh` |
| `Machines/kona-cong-rmsb-dataset-size-bench.cpp` | `run-reference-bench.py --scheme cong-rmsb` |
| `ConstantPP/tests/test-sec-kc-data.cpp` | `run-reference-bench.py --scheme constantpp及Iris封装脚本` |

上述入口的C++未为本次绑定迁移添加setaffinity；不要因此声称其裸二进制直接启动也已经自行绑核。必须使用对应启动脚本，或显式用固定0/2的taskset启动。

矩阵runner默认改为0/2，仍接受--cpu0/--cpu1兼容参数，但只接受0/2。阈值扫描、PCR网络扫描也拒绝其他CPU值。reference runner的--cpus仅允许0 2。根run-kona-baseline.sh的P1由旧CPU 1纠正为CPU 2；旧KNN-experiment.sh四套进程对补taskset 0/2，原协议实现未修改。旧KNN脚本本身的构建/统计方式不因补绑核而自动符合其他新规范。

## 新增C++绑定的独立入口

共用`Machines/kona-bench-affinity.hpp`。本次覆盖19个原无固定launcher的Kona bench、9个ConstantPP组件/合成测试、4个NHSS/Rotated-MSB验证入口：

| C++入口 | 设置位置 |
|---|---|
| `Machines/kona-beaver-esd-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-beaver-triple-prep-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-cong-breakpoint-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-cong-hybrid-auto-selector-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-cong-hybrid-breakpoint-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-cong-hybrid-comparator-breakpoint-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-cong-layer-cpu-overlap-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-cong-layer-multitask-crossover-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-cong-layer-pcr-dcf-crossover-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-cong-layer-wan-load-sweep-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-cong-wan-workload-aware-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-esd-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-layer-cost-calibration-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-nhss-distance-data-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-octm-obssn-structure-bench.cpp` | C++：单进程CPU 0 |
| `Machines/kona-pcr-batchsize-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-pcr-dcf-layer-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-pcr-layer-depth-bench.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-preprocess-bench.cpp` | C++：单进程CPU 0 |
| `ConstantPP/tests/test-sec-bcom.cpp` | C++：按party固定CPU 0/2 |
| `ConstantPP/tests/test-sec-ed.cpp` | C++：按party固定CPU 0/2 |
| `ConstantPP/tests/test-sec-eq.cpp` | C++：按party固定CPU 0/2 |
| `ConstantPP/tests/test-sec-fre.cpp` | C++：按party固定CPU 0/2 |
| `ConstantPP/tests/test-sec-kc-batch.cpp` | C++：按party固定CPU 0/2 |
| `ConstantPP/tests/test-sec-kc.cpp` | C++：按party固定CPU 0/2 |
| `ConstantPP/tests/test-sec-kmin.cpp` | C++：按party固定CPU 0/2 |
| `ConstantPP/tests/test-sec-max.cpp` | C++：按party固定CPU 0/2 |
| `ConstantPP/tests/test-sec-shuffle.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-rotated-msb-compare-test.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-cong-rotated-msb-test.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-nhss-cong-pipeline-test.cpp` | C++：按party固定CPU 0/2 |
| `Machines/kona-nhss-distance-test.cpp` | C++：单进程CPU 0 |

后台负载/重叠等诊断bench仍保留自己的诊断逻辑，不把它们的受控负载实验冒充无干扰的正式性能结果。HECompare等独立依赖工程未在本次Kona/ConstantPP迁移范围内；项目固定0/2原则对后续接入同样适用。

## 输出、冲突与独占

`BENCH_AFFINITY`记录绑定来源（cpp_bench或inherited_verified）、CPU、可见package/core/SMT同胞以及检查结果。绑定前确认固定目标CPU在继承affinity范围内；若外部命令绑到其他CPU直接失败，不静默覆盖。正确继承0/2时只核验，后续线程继承此affinity；其他库如果主动改线程affinity，仍需另外核验。

可见拓扑确认0/2属于同一core时失败；拓扑缺失记UNKNOWN，不自动挑其他核。虚拟机可见拓扑不能证明宿主独占，所以共用入口如实输出core_exclusivity=NOT_VERIFIED。严格物理核独占仍需按[规范第13节](bench_recording_protocol.md#13-调度脚本不同物理核心与核心独占的正式标准)落实系统/宿主隔离，不能把taskset或sched_setaffinity当成独占。

这次没有更改算法、比较器、阈值、材料、网络配置或系统CPU隔离设置，也不回填历史日志的绑定/独占状态。验证结果：32个新增C++入口编译通过，10项小检查/真实小规模协议验证通过，5个脚本参数拒绝检查通过。完整构建、日志和限制见[回归报告](../KNN-experiment-res/bench-affinity-policy-20261104/README.md)。
