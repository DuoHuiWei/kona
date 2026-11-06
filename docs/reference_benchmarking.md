# ConstantPP与Shao相关入口的在线统计

> 2026-11-04入口收束：当前四方案使用 `Machines/knn-bench.cpp` 和 `Scripts/run-knn-experiment.py`，直接运行Python脚本，无额外启动文件。下文旧入口名是历史记录；当前命令和统计范围以 [统一规范](bench_recording_protocol.md) 开头为准。历史结果保留。


更新：2026-11-04（Asia/Shanghai）。统一规则见[bench记录规范](bench_recording_protocol.md)。本页描述当前入口与本次迁移，不修改历史数据的含义。

正式单核对比还必须满足[调度与核心独占标准](bench_recording_protocol.md#13-调度脚本不同物理核心与核心独占的正式标准)：P0/P1分属不同物理核心，每个核心只服务对应协议进程，SMT同胞不得被其他任务占用。固定映射为P0→CPU 0、P1→CPU 2；已有runner的taskset绑定保留，新/修改实验优先C++ bench绑定，方式与清单见[CPU绑定说明](bench_cpu_affinity.md)。绑核不能单独证明核心独占；环境核验与隔离尚未落实时，不能将其结果标为满足该严格标准。

## 入口与边界

相关入口不只有两份；这次选择并改造的是下面两份数据对比主入口：

| 入口 | 当前测量范围 | 输入与输出 | 本次状态 |
|---|---|---|---|
| `ConstantPP/tests/test-sec-kc-data.cpp` → `constantpp-test-sec-kc-data.x` | 完整在线Distance＋Top-k＋投票；也支持distance-only | 读取数据集第一条query，测试份额与原有辅助材料就绪后开始；最终类别秘密份额就绪停止，随后才reveal验证 | 双方阶段与独立完整在线总量；统一runner汇总 |
| `Machines/kona-cong-rmsb-dataset-size-bench.cpp` → `kona-cong-rmsb-dataset-size-bench.x` | Cong＋Rotated-MSB的Top-k-only | 按真实数据集N生成确定性模拟距离，payload为行号；输出有序K条距离/payload份额 | 比较、swap、输出整理、其他在线时间及Top-k总量；统一runner汇总 |

ConstantPP还有`test-sec-kc.cpp`、`test-sec-kc-batch.cpp`的合成输入与兼容测试，以及SecED/SecKMin/SecFre/SecMax等组件入口。Shao相关还包括`kona-nhss-distance-data-bench.cpp`、NHSS及Rotated-MSB功能/组合测试。它们不是同一入口的别名，也不能根据这两份主入口就断言其他文件多余。这些其他入口未在本次批量迁移。

**Rotated-MSB规模bench没有执行NHSS距离或安全投票，因此不是完整Shao KNN，`knn_online_e2e_ms=NOT_IN_SCOPE`。** 不把单独NHSS测量、该Top-k测量及其他投票测量相加后标为实测端到端。其数据集标签表示规模来源，`input_kind=synthetic_distances_real_N`明确区别于真实输入；`--n`覆盖规模时以日志实际N为准。

## 统计实现

两份C++入口输出`metrics_version=reference-online-v1`及双方原始数据。每条`BENCH_PHASE`包含本方`online_ms`、`sent_bytes`、`transport_rounds`、实际`exchange_rounds`和独立的`theoretical_rounds`。

- ConstantPP完整阶段：distance、topk、vote、online_other；`knn_online`为独立的完整在线外层测量。输出为获胜类别秘密份额；reveal、正确性验证和双方验证状态同步均在结束快照之外。distance-only只输出distance阶段，分类与完整端到端标为不在范围内，正确性仍为NOT_CHECKED。
- ConstantPP复用`sec_kc_batch_kona`，增加可选`SecKCBatchTrace`。在线保存阶段边界快照；bench在外层结束之后调用`finalize_sec_kc_batch_stats`进行差分/汇总。原函数的调用顺序、比较器、投票、材料和并列处理不变；旧调用方省略trace时仍自动生成原stats。
- Rotated-MSB阶段：compare、swap、output_materialize、online_other；`topk`为独立阶段总量。原逐层日志和统计处理已移到外层结束之后；公开flatten索引、chunk索引及比较结果外层缓冲在计时外准备。后端内原有分配与计数仍保留在调用成本中。
- 每方检查阶段字节/动作数与外层差值闭合，轮数按实际成对交互定义；Rotated-MSB额外检查每比较chunk的188次交换，以及swap由其实际标量批次推导的交换数。chunk可能增加实际交换，旧logical数只能作为理论数据。
- 比较器数、深度/批次、协议调用等适用计数保留。ConstantPP按rank比较、阈值比较和投票操作分别记录计数，网络深度标为不适用；不能把理论层数当成实际轮数。
- 删除Rotated-MSB原本将本方数值标为`p0_sent_bytes`和直接乘二标为`bidirectional_wire_bytes`的字段；实际记录的是应用payload，不是TCP/IP线速字节。
- 统计插桩仍有轻量开销；公开准备移出和日志延后也改变了旧计时边界，不据跨批时间差宣称加速。

## 调度与结果文件

WSL入口为`python3 Scripts/run-reference-bench.py --scheme constantpp|cong-rmsb ...`。它强制通过既有`Scripts/codex-container-build.sh`同步/构建，再构建指定目标，在`kona-dev`启动两个真实进程。每trial使用新进程，按trial外层、dataset内层交错数据集；不同方案若做正式性能归因，仍须另按统一规则安排同批交错对照。

每方通过`Scripts/kona-bench-log.sh`保存独立原始日志，含run/job、命令、CPU affinity、源码与二进制指纹、实际qdisc/class/filter和offload、开始/结束/退出状态；ConstantPP另保存数据文件指纹。源依赖指纹、Git状态、规模与选项写入manifest。默认CPU为0/2，表示绑核，不宣称独占。默认网络标签为UNVERIFIED；该runner**不配置网络**，实际条件以原始日志为准，外部profile标签不能替代真实配置。

每trial、每阶段，先取`max(P0_ms,P1_ms)`与`max(P0_sent_bytes,P1_sent_bytes)`，两个max可以来自不同party；双方动作数和交换次数分别保留。阶段跨方max之和不等于完整max。C++直接运行只给出本方统计，Rotated-MSB自身summary明确标为`local_party_only`，正式双方汇总由runner生成。

| 文件 | 含义 |
|---|---|
| `run_manifest.json` | 版本、源码依赖指纹、命令/参数、输入类型、网络操作说明 |
| `raw/tNNN-dNN-P0.log` / `P1.log` | 各trial双方完整原始日志，含全部阶段数据和状态 |
| `order.jsonl` | 实际执行顺序、dataset、trial、双方退出码 |
| `phase_trials.csv` | 每阶段双方原始时间/字节/动作/实际交换/理论轮数及逐trial max；保留scope、input_kind、N/features/k |
| `summary.csv` | 按dataset、阶段分别统计mean/median/sample SD/CV/min/max及max发送量的均值；双方轮数trial序列不合并 |
| `status.json` | 运行完成或失败状态；失败保留已产生原始日志和部分trial记录 |

单样本SD/CV为不适用（CSV空值），慢trial不删除，预热次数明确为0。默认只选Arcene，3次；其他数据集通过`--datasets`显式指定。默认结果目录带UTC时间和PID，指定`--output`也要求目标目录不存在，避免覆盖历史。`--timeout`限制单方协议进程，失败或超时不混入成功汇总。

## 小规模使用示例

先有可用DCF测试材料；runner不会向主实验目录生成或覆盖密钥。`--runtime-dir`指定容器内已有材料的运行目录，源和二进制仍来自WSL同步的仓库。

```bash
# ConstantPP真实数据的有限子集：完整在线分类与全部阶段。
python3 Scripts/run-reference-bench.py --scheme constantpp \
  --datasets arcene --limit-train 16 --limit-features 4 --k 5 --repeats 3

# Rotated-MSB小规模模拟距离Top-k；不是完整KNN。
python3 Scripts/run-reference-bench.py --scheme cong-rmsb \
  --datasets arcene --n 17 --k 5 --compare-batch-size 3 --repeats 3
```

ConstantPP全量rank使用上三角配对，规模和内存随N平方增长；是否执行全量需单独按资源决定。本次未运行四数据集全量或网络性能实验。

## 历史证据与回归

历史[ConstantPP 20260829批次](../KNN-experiment-res/constantpp-sec-kc-batch-localhost-3run-20260829/README.md)和[Rotated-MSB四规模结果](../KNN-experiment-res/cong-rmsb-four-datasets-k5-formal/summary.md)保持原格式及P0统计含义，不改标成新口径。

本次回归见[reference-metrics-v1-20261104](../KNN-experiment-res/reference-metrics-v1-20261104/README.md)。正式完整KNN与Top-k-only必须分表或明确分栏，不能将二者总时间直接作为同范围性能比。

## 鸢尾花独立启动脚本（2026-11-04）

`Scripts/run-constantpp-iris-lan-wan.py`供用户在WSL终端自行启动；不会依赖Codex持续轮询。固定实验条件为：

- 数据：`ConstantPP/data/iris-data`，150条中固定1条作查询，其余149条作训练；4个特征，沿用已有x10整数编码，不重新抽样或更换查询。
- k=5，测完整在线Distance、Top-k、投票及独立KNN在线总量；各阶段时间/字节/双方动作与实际交换全部保留。
- 默认每网络3次；顺序为LAN1、WAN1、LAN2、WAN2、LAN3、WAN3，逐个执行进程对，避免先连跑完一种网络再跑另一种。可用`--repeats`改变次数。
- LAN每方向1 Gbps、无额外延迟；WAN每方向40 Mbps、单程10 ms。调用已有唯一网络配置脚本，普通协议进程不使用privileged。
- 使用既有kona-dev并按规定同步/构建；自动查找现有DCF材料目录，也可用`--runtime-dir`指定，不生成或覆盖密钥。CPU仍为P0=0、P1=2，无预热。
- 结束或失败时清除网络整形，并恢复启动时查询到的TSO/GSO/GRO状态；保存清理日志及退出状态。运行期间不要并行启动其他网络整形实验。
- 根结果目录保留`order.jsonl`、全部逐trial目录、合并`phase_trials.csv`和`summary.csv`、`batch_status.json`；单trial目录中的原始日志和版本信息仍由已有runner保存。重复次数不足2时，标准差/CV为空。

前台启动：

```bash
cd /home/u7231/kona-work/Kona
python3 -u Scripts/run-constantpp-iris-lan-wan.py
```

后台启动（关闭终端后仍继续；日志文件名带UTC时间）：

```bash
cd /home/u7231/kona-work/Kona
mkdir -p KNN-experiment-res
iris_run_log="KNN-experiment-res/constantpp-iris-launch-$(date -u +%Y%m%dT%H%M%SZ).log"
nohup python3 -u Scripts/run-constantpp-iris-lan-wan.py > "$iris_run_log" 2>&1 < /dev/null &
echo "PID=$! LOG=$iris_run_log"
```

程序输出的`RESULT_DIRECTORY`指出本批独立结果目录；查看进度可用`tail -f`读取启动日志。仅查看顺序、不调用Docker或执行实验：

```bash
python3 Scripts/run-constantpp-iris-lan-wan.py --dry-run
```

本次交付仅做脚本语法和dry-run验证，未替用户启动正式Iris协议实验。MNIST全量试验已按用户要求取消；原ConstantPP实现及默认通信分批保持不变，尚未完成的自动流式rank改动已恢复，不能宣称其1.5倍策略已启用。
