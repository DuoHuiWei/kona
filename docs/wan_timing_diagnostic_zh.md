# 对称 WAN PCR/DCF Compare-only 计时诊断

## 1. 当前实验条件

目标配置：

```text
P0 -> P1: 40 Mbps
P1 -> P0: 40 Mbps
单程 delay: 20 ms
理论 RTT: 40 ms
P0=CPU0, P1=CPU2
```

Kona 两个 party 在同一容器内通过 `127.0.0.1` 通信。当前容器没有 network namespace；`ip netns list` 为空，`unshare -n` 失败。因此当前双向隔离方案不是 veth/netns，而是 `lo` 上的端口奇偶性分类。

## 2. qdisc 审计

当前 WAN qdisc：

```text
root HTB: 80 Mbit, default class 1:30
class 1:10: 40 Mbit + netem delay 20 ms
class 1:20: 40 Mbit + netem delay 20 ms
class 1:30: 80 Mbit fallback
```

过滤器按目标端口最低位分类：

```text
even destination port -> 1:10
odd destination port  -> 1:20
```

Kona 的 party 监听端口为 `port_base + party_id`，因此两个方向会进入不同子类。每个包只命中一个 filter 和一个 netem 子队列，没有证据表明同一个包经过两个 `20 ms` netem。

详细参数：

```text
HTB r2q       = 10
class burst   = 1600 bytes
class quantum = 200000 bytes
netem limit   = 1000 packets
kernel config HZ = 250
getconf CLK_TCK = 100 (容器用户空间值)
```

当前配置运行时会出现 `quantum ... is big` 警告。这是调度参数需要后续优化的信号，但不是本轮协议结论的依据。

## 3. 实际网络验证

旧的单根 `tbf rate 40mbit`：

```text
单向 A: 48.738 Mbps
单向 B: 48.770 Mbps
双向同时: A=21.855 Mbps, B=21.855 Mbps
双向总吞吐: 43.710 Mbps
RTT 中位数: 40.207 ms
```

结论：旧配置是两个方向共享约 40 Mbps。

新的两个 40 Mbps 子类：

```text
单向奇数端口: 50.953 Mbps
单向偶数端口: 44.725 Mbps
双向同时: A=44.331 Mbps, B=44.334 Mbps
双向总吞吐: 88.665 Mbps
RTT 中位数: 40.225 ms
```

结论：新配置对当前 Kona 端口模式实现了实测的每方向约 40 Mbps；RTT 约为两个方向各一次 `20 ms`。

## 4. PCR/DCF timer 边界

`Machines/kona-pcr-dcf-bench.cpp` 的 online timer：

- 网络连接、DCF 对象/key 初始化、correctness 测试、输入 share 构造均在 timer 外。
- PCR timer 覆盖循环内完整 `pcr_compare_in_vec_l2()` 调用。
- DCF timer 覆盖循环内完整 `compare_in_vec()` 调用。
- 通信统计 before/after 与对应 timer 使用相同边界。
- `send()` 循环直到完整写入；`receive()` 循环直到完整读满。
- `octetStream::Send()` 发送长度和完整 payload，`Player::send_to()` 在调用返回后才增加 sent 统计。

因此当前没有证据表明 timer 在最后一次协议消息完成前提前结束。残余限制是 benchmark 只打印 P0 的 online 时间，未保存 P1 的同一字段，因此没有验证 `max(P0,P1)`；这是统计完整性的后续改进点，不是已证实的漏计。

socket 代码审计：

- 客户端和监听 socket 显式设置 `TCP_NODELAY`。
- `SO_SNDBUF`、`SO_RCVBUF` 没有显式设置，使用系统默认值。
- 没有使用 `MSG_WAITALL`。
- blocking receive 通过循环 `recv()` 完整读满。
- send 使用 `MSG_DONTWAIT` 循环发送，遇到 `EAGAIN` 时会继续自旋。

## 5. qdisc 可加性 microbenchmark

使用四线程同步 barrier，使每个 `rounds=1/2/8` 真正形成独立逻辑交互。比较：

```text
T(rate + delay)
vs
T(rate only) + T(delay only) - T(native)
```

代表结果：

| 每轮消息 | 逻辑轮次 | rate-only(ms) | delay-only(ms) | both(ms) | 可加预测(ms) | 差值 |
|---:|---:|---:|---:|---:|---:|---:|
| 1.05 MB | 1 | 224.882 | 182.166 | 330.726 | 405.521 | -74.795 |
| 1.05 MB | 8 | 1684.597 | 345.567 | 1938.668 | 2030.164 | -91.496 |
| 1.40 MB | 8 | 2245.653 | 385.612 | 2489.489 | 2631.265 | -141.776 |
| 1.75 MB | 8 | 2798.010 | 386.531 | 3030.413 | 3180.541 | -150.128 |

这里 `delay-only` 已改为根 `netem delay 20ms`，没有使用 10 Gbps HTB，避免把 HTB burst 混入纯 delay 基线。原始全部 micro 输出保存于：

`KNN-experiment-res/wan-qdisc-diagnostic-20260902/kona-wan-micro-barrier-20260902.txt`

纯 netem delay-only 的修正版原始输出保存于：

`KNN-experiment-res/wan-qdisc-diagnostic-20260902/kona-wan-delay-only-corrected-20260902.txt`

结论：`rate` 序列化和 `netem delay` 在 qdisc/TCP 管线中发生流水线重叠，不能简单相加。即使只有根 `netem delay 20ms`，大消息完成时间也会超过单个 20ms，说明 loopback packetization、TCP 接收完成条件和队列排空都会影响结果。延迟队列可以在带宽发送下一批数据时并行推进；TCP socket buffer、GSO/loopback packetization 和 HTB 调度也会改变观察到的组合时间。

因此 `sent_bytes/B + logical_rounds*d` 最多是理想化的保守趋势模型，不是当前实现的精确等式。

## 6. DCF chunk 边界

源码当前常量为：

```text
MAX_COMPARE_CHUNK = 16384
```

使用未传入 benchmark-local override 的 `kona-pcr-dcf-layer-bench.x` 重新
实测，循环按 `ceil(m / 16384)` 个 DCF compare chunk 执行：

| m | DCF transport rounds | DCF logical rounds |
|---:|---:|---:|
| 16000 | 2 | 1 |
| 16383 | 2 | 1 |
| 16384 | 2 | 1 |
| 16385 | 4 | 2 |
| 17000 | 4 | 2 |

`16385` 才出现第二 chunk，和 `ceil(m / 16384)` 一致。此前在默认值
仍为 `16348` 时得到的边界日志属于旧口径，不能用于当前实验。

## 7. 对 PCR 理论值与实测值差异的诊断

以最新双方都不拆 chunk 的 `m=15000` 为例，旧的可加模型给出：

```text
PCR = native 6.345 + bytes/B 210 + 8*20 = 376.345 ms
实际对称 WAN PCR = 318.850 ms
DCF = native 287.930 + bytes/B 24 + 1*20 = 331.930 ms
实际对称 WAN DCF = 348.744 ms
```

目前证据排序如下：

1. 主要原因是 `rate` 和 `delay` 的 qdisc/TCP 流水线重叠，`S/B + R*d` 对组合时间重复计算了一部分等待；PCR 的理论加法值比实测高约 `57.5 ms`，DCF 的理论值比实测低约 `16.8 ms`，说明两者的消息分布和排队路径不同。
2. 协议的总字节并不是每个逻辑轮次都发送一个同样大小的大消息；把总 `sent_bytes` 直接当作每轮独立消息也会产生偏差。
3. 新配置 burst 只有 `1600 bytes`，没有旧 TBF 的 `256 KB` 初始 burst，因此“初始大 burst 一次性放过 65 ms 数据”不是当前主要解释。
4. socket buffer、loopback/GSO、HTB quantum 和调度会造成次级偏差。
5. 当前 timer 从完整协议调用返回后结束，没有已确认的尾部发送漏计；但未做额外 barrier，也未采集 P1 timer，因此仍保留低优先级统计风险。

## 8. 当前可信口径

- 真实 WAN online 时间：可信，但只代表当前 qdisc、当前 TCP/loopback 和当前 P0 输出口径。
- `sent_bytes`：是协议层 send payload 统计，不是 TCP wire bytes；不含 TCP/IP 头、ACK 和重传。
- `logical_rounds`：是当前 Player 统计动作除以 2 的工程统计值，应与实际协议交互阶段一起解释。
- `S/B + R*d`：只能作为理想化上界/趋势模型，当前不能作为精确预测公式；最新四档 compare-only 数据中，双方都不拆 chunk 时 PCR/DCF 均由 PCR 更快。
- `2808`、`21249` 等阈值在本诊断完成前不应继续拟合或冻结。

本轮没有修改 PCR、DCF、swap 或 Cong 协议，也没有提交或 push。

## 9. r2q=1000 后的最终诊断复验

为消除 HTB `quantum` 警告，WAN 子类改用 `r2q=1000`，仍固定 burst。四个
规模重新跑了 baseline，并用 benchmark-only `--final-barrier 1` 复跑。

有效 baseline（P0/P1 均正常完成）：

| m | P0 online(ms) | P1 online(ms) | 取 max(ms) |
|---:|---:|---:|---:|
| 15000 | 272.872 | 279.585 | 279.585 |
| 20000 | 333.824 | 339.954 | 339.954 |
| 22000 | 365.265 | 371.971 | 371.971 |
| 25000 | 419.365 | 426.166 | 426.166 |

`m=15000` 的第一次 baseline 试次曾出现一次不可复现的 `SIGABRT/core dump`；
随后按相同条件重试成功，表中使用成功重试值，不使用失败试次。

Barrier 版本（P0/P1 均正常完成）：

| m | P0 barrier(ms) | P1 barrier(ms) | 取 max(ms) | 额外 logical round |
|---:|---:|---:|---:|---:|
| 15000 | 280.216 | 287.154 | 287.154 | 1 |
| 20000 | 343.155 | 349.518 | 349.518 | 1 |
| 22000 | 375.584 | 381.484 | 381.484 | 1 |
| 25000 | 427.283 | 433.706 | 433.706 | 1 |

对 `m=20000..25000`，加入 barrier 后的 `max(P0,P1)` 增量约为
`7.5..9.6 ms`，没有出现额外完整 `20 ms` 的尾部等待。它同时使通信计数
增加 1 个 logical interaction，证明 barrier 本身确实执行了；但增量不是固定
20 ms，仍受当前 qdisc/TCP 排队和重叠影响。

这组结果说明当前 timer 没有明显漏掉一整轮网络完成时间，但正式论文统计仍
建议报告 `max(P0,P1)`，而不是只报告 P0。
