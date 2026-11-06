# 统一容器网络Profile

配置版本：`20261104-wan20-v2`。唯一配置入口：`Scripts/container-network.sh`。

各profile只改变速率和延迟，其余设置共用同一实现。2026-11-04按用户要求将WAN每路单程延迟改为20ms；其他四档不变。本文末尾2026-11-03的实测属于旧v1配置（WAN每路10ms），不代表本次v2实测；历史实验不重跑、不改标。

| Profile | 每路速率 | 每路额外单程延迟 | 共享父类上限 |
|---|---:|---:|---:|
| lan | 1 Gbps | 0 ms | 2 Gbps |
| wan | 40 Mbps | 20 ms | 80 Mbps |
| mid-lan | 500 Mbps | 2 ms | 1 Gbps |
| mid-wan | 100 Mbps | 8 ms | 200 Mbps |
| super-dc | 10 Gbps | 0 ms | 20 Gbps |

## 用法

默认目标是现有`kona-dev`容器的`lo`；可通过`KONA_CONTAINER`选择既有容器。
当前容器普通exec进程缺NET_ADMIN，实际施加/清除需要显式设置`KONA_NETWORK_PRIVILEGED=1`，仅配置命令使用特权exec，不重建容器。

```bash
bash Scripts/container-network.sh --list
bash Scripts/container-network.sh lan lo --dry-run
bash Scripts/container-network.sh --profile wan --dev lo --dry-run
KONA_NETWORK_PRIVILEGED=1 bash Scripts/container-network.sh lan lo
bash Scripts/container-network.sh --show --dev lo
KONA_NETWORK_PRIVILEGED=1 bash Scripts/container-network.sh --clear --dev lo
```

`--dry-run`只打印准确的Docker命令、参数和远端脚本，不调用Docker，不改变网络。`--show`只读取qdisc/class/filter。施加profile会删除目标接口原root qdisc，再安装新规则；同一接口只保留最后施加的profile。

`--clear`移除root qdisc，但不会恢复施加前的offload设置。

## 所有Profile统一的设置

- 同一HTB拓扑：根`1:`，父类`1:1`，两个独立限速子类`1:10`/`1:20`及fallback类`1:30`。
- 根HTB：`r2q=10000`、默认类`30`。
- 所有HTB class：`burst=1mb`、`cburst=1mb`、显式`quantum=65536`；rate/ceil随profile变化。
- 两个子类均挂netem，`limit=1000`，只改变delay值。0ms不注入额外延迟，但不代表本机通信成本为0。
- IPv4目标端口偶数进入`1:10`，奇数进入`1:20`，分类优先级10/11；其他流量使用fallback。
- 有ethtool时统一尝试关闭GRO/GSO/TSO，沿用原脚本的容错方式；实际生效状态需在正式实验环境核实。
- 施加后输出配置版本、速率、延迟，以及`tc -s qdisc/class/filter show`。

两路沿用Kona两方连续监听端口的分类方式，不是两个物理网卡，也不是任意应用均适用的自动方向识别。非连续端口、IPv6或其他通信方式需要另行核对分类。正式实验应保存两个子类的实际流量计数。

WAN两路各20ms，传统往返传播延迟的配置目标约为40ms，不将该值直接当作每次镜像协议交换的必然耗时。

## 变更与历史结果

2026-11-04：v2仅将WAN从两路各40Mbps/10ms改为两路各40Mbps/20ms。之前实验保留原配置记录，不重跑。下述v1变更及实测作为历史记录保留。

LAN从单共享2Gbps改成两路各1Gbps；WAN从单共享80Mbps/20ms改成两路各40Mbps/10ms；Super-DC取消原0.05ms额外延迟。Mid-LAN/Mid-WAN速率与延迟保留，但其burst等其他参数按上述规则统一。

这是新的配置版本，不能把历史共享带宽、20ms WAN、0.05ms Super-DC或旧burst配置下的结果改标为新结果。新的实际测试结果见下节；旧报告不能用于声明新配置性能。

原9个配置/别名脚本已移除，另移除了无现行调用的3个宿主机整形脚本：根目录`LAN.sh`、`WAN.sh`及`Scripts/wan.sh`。原文归档在`KNN-experiment-res/network-config-cleanup-20261103/source-before/`。

现行矩阵、四数据集Top-k、比较器网络扫描和compare+swap runner均已改用统一入口；清理操作也通过同一入口完成。协议后端、比较阈值及输入设置不在本次修改范围内。

## 2026-11-03实际验证

每profile独立施加3次；每次测单向奇/偶端口及两路同步传输，每流64MiB，另测两端口各50个echo RTT样本。结果为3次中位数，单位Mbps（带宽）/ms（RTT）。

|Profile|单向奇/偶|双向同时奇/偶|Echo RTT|
|---|---:|---:|---:|
|LAN|1016.499 / 1016.413|1011.808 / 1004.356|0.0292|
|WAN|40.487 / 40.489|35.550 / 40.156|20.2145|
|Mid-LAN|501.937 / 501.928|450.759 / 501.639|4.1347|
|Mid-WAN|100.696 / 100.696|100.520 / 100.606|16.2083|
|Super-DC|10129.684 / 10129.575|9800.224 / 9223.029|0.0297|

45个传输阶段均收足数据，fallback增量全部为0；15个双向阶段两路class各发送至少64MiB，单向测试的class归属与端口奇偶一致。单向速率接近每路限制，而非父类的双倍速率，支持两路独立限速生效。

无整形RTT中位数约0.0293ms；LAN/Super-DC与其相近，未观察到额外固定时延。其他三档RTT与2倍单程delay一致。

**双向满速不稳定**：原始样本保留了LAN一次约740Mbps、Super-DC一次约5.84Gbps等下降。未发现qdisc丢包，当前证据不足以确定根因，不能宣称每次均稳定达到标称速率。

该轮测量时容器尚未安装ethtool，offload关闭步骤实际跳过；该轮结果不代替offload明确关闭后的测量。微小单向超额与有限流及1MiB burst相容，不是持续超额速率的证明。

测试仅运行网络探针，没有运行大型协议实验。finally及独立复查均确认最终root为noqueue，class/filter为空，没有遗留探针进程。

完整报告、逐trial数据、方向class计数和原始快照位于：
`KNN-experiment-res/network-profiles-validation-20261103-232013/`。
其中`report.md`解释结论，`summary.csv`保留均值/中位数/样本SD/CV/min/max，`raw_trials.csv`保留全部trial，`class_counter_deltas.csv`保留45个阶段的分类证据。

## ethtool安装与功能核验

2026-11-03随后按要求，仅在现有kona-dev中安装ethtool（Debian包版本1:5.9-1，工具版本5.9）；未升级其他包、未改WSL环境、未创建镜像或容器。

已用显式特权exec验证lo的GRO/GSO/TSO均能关闭，随后finally恢复到安装前的on/on/on，核对全部feature状态与原状态一致。最终qdisc仍为noqueue，没有施加profile或重跑网络/协议性能测试。

以后应用统一网络profile时，已有ethtool步骤可以实际执行；清除qdisc仍不会自动恢复offload。此前offload未关闭的验证结果保持原标注，不改标为新条件。

原始feature快照：`KNN-experiment-res/ethtool-install-check-20261103/`。


## 2026-11-04 WAN v2与并发收发复核

正式WAN每路40Mbps、额外单程20ms，额外RTT约40ms；不再提供活动10ms WAN配置。临时旧延迟仅用于诊断，结束已清理。64MiB接收完成时间测得单向奇/偶40.276/40.275Mbps、Echo RTT40.186/40.184ms；同一TCP同时双向40.255/38.815Mbps。先后双向各自有效吞吐40.274/40.276Mbps，但单方字节除以整个双向阶段时间为20.138Mbps，是串行调度口径，并非每路限速减半。

单次探针不证明所有应用每次满速；双方TCP吞吐仍可能不对称。临时10ms对照测得单向40.489Mbps、Echo RTT约20.2ms。两次探针fallback流量均为0，两路限速class均有对应数据计数。结束noqueue、class/filter为空，offload恢复原值。详细证据：KNN-experiment-res/wan-bandwidth-diagnostic-20261104-192610/README.md。
