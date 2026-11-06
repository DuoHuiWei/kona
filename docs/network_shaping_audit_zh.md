# LAN/WAN 网络仿真审计与对称带宽结果

> 以下正文为历史配置与实测快照。当前唯一配置入口、5个profile及统一调度参数见 [network_profiles.md](network_profiles.md)。旧入口已归档删除；本文历史20ms WAN结果不能改标为新版10ms。

## 1. 结论

旧配置在单个 `lo` 根 qdisc 上使用一个 `tbf rate 40mbit`，两个方向共享同一个 40 Mbps 队列。它不是“P0->P1 40 Mbps、P1->P0 40 Mbps”。

当前配置改为一个 80 Mbps 根 HTB 和两个独立的 40 Mbps 子类；Kona 两方端口连续，因此通过目标端口奇偶性把两个方向分到不同子类。每个子类独立挂自己的 netem 延迟。

这是针对当前 Kona 双进程端口规则的方向隔离：

```text
P0/P1 listen ports = port_base + party_id
even destination port -> class 1:10
odd destination port  -> class 1:20
```

容器不能创建 network namespace：`ip netns add` 因 `mount --make-shared /run/netns` 权限失败，`unshare -n` 也失败。因此当前实现使用端口分类的双向 HTB，而不是 veth/netns。它对 Kona 实验流量是可验证的，但不是任意 localhost 应用的通用方向隔离。

## 2. 脚本

- WAN：`Scripts/container-WAN.sh`
- LAN：`Scripts/container-LAN.sh`
- WAN 明确入口：`Scripts/WAN-bidirectional-40mbps-20ms.sh`
- LAN 明确入口：`Scripts/LAN-bidirectional-1gbps-0.5ms.sh`
- compare+swap runner：`Scripts/run-pcr-dcf-compare-swap-network-bench.sh`

旧的 `Scripts/wan.sh` 是历史实验脚本，不被当前 runner 使用，不能作为当前 LAN/WAN 口径。

## 3. qdisc 配置

WAN：

```text
root HTB: 80 Mbps
class 1:10: 40 Mbps + netem 20 ms
class 1:20: 40 Mbps + netem 20 ms
```

LAN：

```text
root HTB: 2 Gbps
class 1:10: 1 Gbps + netem 0.5 ms
class 1:20: 1 Gbps + netem 0.5 ms
```

根类不是简单把共享限速改成 80 Mbps；两个方向分别进入两个独立的 40 Mbps/1 Gbps 子类。

## 4. 旧 WAN 配置的实际验证

旧配置：单个 `lo` 根 `tbf rate 40mbit`。

```text
P0->P1 单向：48.738 Mbps
P1->P0 单向：48.770 Mbps
双向同时：方向 A 21.855 Mbps，方向 B 21.855 Mbps
双向总吞吐：43.710 Mbps
RTT 中位数：40.207 ms
```

因此双向同时发送时，每方向约 22 Mbps，证明两个方向共享约 40 Mbps 队列。

## 5. 新 WAN 配置的实际验证

新配置：两个独立 40 Mbps HTB 子类。

```text
单向奇数端口：50.953 Mbps
单向偶数端口：44.725 Mbps
双向同时：方向 A 44.331 Mbps，方向 B 44.334 Mbps
双向总吞吐：88.665 Mbps
RTT 中位数：40.225 ms
```

两个 HTB 子类均有独立发送计数，双向同时传输时每个方向仍接近 40 Mbps，总吞吐接近 80 Mbps。LAN 对称配置也通过了同样测试：双向同时每方向约 1.087 Gbps，总吞吐约 2.175 Gbps，RTT 中位数约 1.082 ms。

## 6. 新对称 WAN 下 PCR/DCF compare-only

调用：`Machines/kona-pcr-dcf-bench.cpp`。

口径：真实 online compare-only；PCR 在该 benchmark 内 B2A 不拆 chunk；DCF 使用当前正式路径。P0/P1 绑定 CPU0/CPU2，`-r 3`，key init 和 correctness 不计入 online。

| m | PCR online(ms) | DCF online(ms) | PCR/DCF | PCR logical/transport | DCF logical/transport |
|---:|---:|---:|---:|---:|---:|
| 15000 | 311.041 | 321.147 | PCR 快 1.033x | 8/16 | 1/2 |
| 20000 | 412.998 | 438.838 | PCR 快 1.063x | 8/16 | 2/4 |
| 22000 | 448.186 | 479.960 | PCR 快 1.071x | 8/16 | 2/4 |
| 25000 | 518.249 | 558.042 | PCR 快 1.077x | 8/16 | 2/4 |

对应单次发送量：

```text
PCR = 70m bytes
DCF = 8m bytes
```

在新的每方向独立 40 Mbps 条件下，四个规模的 compare-only 结果均为 PCR 更快。这个结果不能和旧共享 40 Mbps qdisc 下的历史数据直接混合。

## 7. 结果使用规则

- 当前未来 LAN/WAN 实验应先使用 `container-LAN.sh` 或 `container-WAN.sh` 的新对称配置。
- 运行前必须保证两个 party 使用连续端口 `port_base` 与 `port_base+1`。
- 历史单根 qdisc 结果保留用于“共享带宽”对照，不得标为每方向独立带宽。
- compare-only、compare+swap、完整 Top-k 仍需分别标注，不能只根据 m 合并阈值。
- 当前配置是 Kona 端口级方向隔离；若未来需要与任意 localhost 流量隔离，应使用具备 namespace/veth 权限的环境重新实现。
