# Final Benchmark Protocol v1

状态：冻结  
适用范围：KNN 端到端方案主性能实验  
生效日期：2026-08-29

本规范用于统一 Kona、ConstantPP 和后续方案的 KNN 主性能统计口径。从本版本开始，主表不再增加与核心比较无关的指标。模块级 benchmark 可以保留额外诊断字段，但不得混入 KNN 主性能表。

## 1. Online 计时边界

只统计 online phase：

> 从秘密共享输入已经就绪、所有离线材料已经驻留内存开始计时，到最终结果 share 生成结束。

统一拆分为三个阶段：

1. `Distance`：安全距离计算，输出距离 shares。
2. `Top-k`：从距离 shares 中安全选出 K 个标签或候选 shares。
3. `Voting`：频次统计与最大频次标签选择，输出最终结果 share。

总在线时间定义为：

$$
T_{online} = T_{distance} + T_{topk} + T_{voting}
$$

实现中应同时测量连续的外层 `online_total_ms` 和三个阶段时间。两者允许存在极小的函数边界或计时器误差，但必须基本相等。

### 计时排除项

以下内容全部排除在 online timer 之外：

- 数据集读取和 CSV/文本 IO；
- query、database 和 label 的 share 构造；
- Beaver triple、FSS、DCF、mask 等离线材料的生成与加载；
- DCF/FSS 对象或密钥初始化；
- correctness reveal/open；
- 明文正确性验证；
- 日志、CSV 和报告写入。

正确性验证仍然必须执行，但必须在 online timer 停止之后执行。

## 2. 重复实验与汇总

每个 `scheme + dataset + network_env` 组合固定运行 3 次。

正式结果报告：

$$
mean \pm SD,\quad 3\ runs
$$

其中 `SD` 使用三次 trial 的样本标准差。原始 CSV 必须保留每次 trial，不得只保存均值。

三次实验必须保持以下条件一致：

- 相同代码和构建产物；
- 相同数据集、N、K 和特征数；
- 相同网络环境；
- 相同 CPU affinity；
- 相同计时边界；
- 相同通信统计方向。

网络环境必须明确标注，例如：

- `localhost-no-delay`；
- `LAN-rtt-...`；
- `WAN-rtt-...-bandwidth-...`。

## 3. 最终性能总表

KNN 主性能表只保留四个时间字段：

| Scheme | Dataset | N | K | Features | Distance ms | Top-k ms | Voting ms | Online Total ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Kona | Arcene | 199 | 5 | 10000 | - | - | - | - |
| ConstantPP | Arcene | 199 | 5 | 10000 | 321.326 | 548.820 | 3.220 | 873.390 |
| Our Scheme | Arcene | 199 | 5 | 10000 | - | - | - | - |

表中时间统一使用 `mean +/- SD ms`。例如：

```text
873.390 +/- 25.919 ms
```

## 4. 阶段级统计表

Distance、Top-k 和 Voting 三个阶段统一记录以下四类数据：

| Stage | Stage Time ms | Sent Bytes | Transport Rounds | Logical Rounds |
|---|---:|---:|---:|---:|
| Distance | - | - | - | - |
| Top-k | - | - | - | - |
| Voting | - | - | - | - |
| Total | - | - | - | - |

### 4.1 Stage Time

`Stage Time ms` 是该阶段从输入 shares 就绪到输出 shares 生成的在线墙钟时间：

$$
T_D,\quad T_T,\quad T_V
$$

必须满足：

$$
T_{online} \approx T_D + T_T + T_V
$$

不单独把 `comm_ms`、DCF evaluation time 或本地 compute time加入 KNN 主表。

### 4.2 Sent Bytes

统一统计：

$$
\boxed{P0\ actual\ sent\ bytes}
$$

即只统计 P0 实际发出的字节，不包含 P0 接收字节，也不使用 P0/P1 双向字节总和。

总发送量必须满足：

$$
B_{total} = B_D + B_T + B_V
$$

所有方案必须使用相同方向和相同底层 Player 通信统计源。

### 4.3 Transport Rounds

`Transport Rounds` 在本规范中表示 P0 视角下实际发生的底层 transport actions：

- P0 `send()` 一次计 1；
- P0 `receive()` 一次计 1；
- 一次 `send + receive` 双向交互通常计 2。

如果比较器因为 `MAX_COMPARE_CHUNK`、消息大小或内存限制被拆成多个 batch，所有额外 send/receive 都必须计入。不得为了匹配理论轮数而隐藏 chunking。

因此：

$$
TransportRounds \ge LogicalRounds
$$

通常成立，但两者没有固定换算关系。不得默认用 `transport_rounds / 2` 替代 logical rounds。

### 4.4 Logical Rounds

`Logical Rounds` 表示协议实现中有依赖关系的逻辑在线阶段，不受底层消息 chunk 数量影响。

ConstantPP 当前固定为：

| Stage | Logical Rounds |
|---|---:|
| Distance | 1 |
| Top-k | 5 |
| Voting | 4 |
| Total | 10 |

例如 TCGA 的 DCF 比较可能产生 56 个 transport actions，但协议逻辑轮次仍为 10。Transport rounds 反映实际工程通信行为，logical rounds 反映协议结构，两者不得混用。

## 5. 冻结 CSV 字段

KNN 主性能原始 CSV 固定使用以下字段：

```text
scheme
dataset
network_env

n
k
features

trial

distance_ms
topk_ms
voting_ms
online_total_ms

distance_sent_bytes
topk_sent_bytes
voting_sent_bytes
total_sent_bytes

distance_transport_rounds
topk_transport_rounds
voting_transport_rounds
total_transport_rounds

distance_logical_rounds
topk_logical_rounds
voting_logical_rounds
total_logical_rounds

correctness
```

`correctness` 只记录 PASS/FAIL，不把 reveal 和明文检查耗时计入 online time。

## 6. 主表禁止混入的字段

以下指标可以存在于模块 benchmark 或诊断日志中，但不进入 KNN 主性能表：

```text
comm_ms
compute_ms
dcf_evaluate_calls
dcf_evaluate_ms
comparator_count
depth
pcr_comparators
dcf_comparators
pcr_stages
dcf_stages
read_ms
setup_ms
key_init_ms
```

它们用于解释性能或定位问题，不属于冻结后的主比较口径。

## 7. 验收检查

每次正式实验结束后必须检查：

- 三次 trial 均为 `correctness=PASS`；
- `online_total_ms` 与三个阶段时间之和基本一致；
- 总 sent bytes 等于三个阶段之和；
- 总 transport rounds 等于三个阶段之和；
- 总 logical rounds 等于三个阶段之和；
- timer 开始前 share 和离线材料已经就绪；
- timer 停止后才执行 reveal、明文验证和日志输出；
- 原始 trial CSV 和汇总结果同时保存；
- 网络条件和 CPU affinity 已记录。

## 8. 可复用实验 Prompt

```text
按照 docs/Final_Benchmark_Protocol_v1.md 执行 KNN 性能实验。

严格只统计 online phase：从秘密共享输入和离线材料就绪开始，到最终结果 share 生成结束。排除数据读取、share 构造、离线材料生成/加载、DCF/FSS 初始化、correctness reveal、明文验证和日志输出。

将在线时间拆为：
1. distance_ms
2. topk_ms
3. voting_ms
4. online_total_ms

对 Distance、Top-k、Voting 分别记录：
- stage time ms
- P0 actual sent bytes
- P0 transport actions（每次 send 或 receive 计 1）
- logical rounds

每个 scheme + dataset + network_env 运行 3 次，保存每次 trial，并报告 mean +/- sample SD。检查阶段时间、发送字节、transport rounds 和 logical rounds 分别能够与总计相加闭合。

主性能 CSV 只能使用规范中冻结的字段。额外诊断指标可以单独输出，但不得混入 KNN 主表。
```
