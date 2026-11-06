# 新增网络档位实际吞吐验证

> 以下为历史实测。当前版本见 [network_profiles.md](network_profiles.md)，新版已于2026-11-03独立验证，结果见该配置文档末尾；不将本页旧表用于声明新版profile性能。

测试脚本：`Scripts/measure-container-profile-throughput.py`。

测试方法：每个方向发送 `64 MiB`，吞吐使用接收端完成全部数据的时间计算；双向测试使用两个连续目标端口，同时发送；RTT 使用 TCP_NODELAY echo 的 10 次中位数。所有测试都在现有 `kona-dev` 容器内完成。

## 结果

| profile | 目标带宽/方向 | 单向接收吞吐 | 双向方向 A | 双向方向 B | 双向总吞吐 | RTT中位数 |
|---|---:|---:|---:|---:|---:|---:|
| Super-DC | 10 Gbps | 10.582 / 10.570 Gbps | 10.074 Gbps | 9.817 Gbps | 19.891 Gbps | 0.170 ms |
| Mid-LAN | 500 Mbps | 499.923 / 499.454 Mbps | 497.839 Mbps | 499.341 Mbps | 997.180 Mbps | 4.121 ms |
| Mid-WAN | 100 Mbps | 99.642 / 99.374 Mbps | 99.304 Mbps | 99.445 Mbps | 198.749 Mbps | 16.177 ms |

## qdisc 结构

三个 profile 都使用当前 Kona 端口奇偶性方向分类：

```text
偶数目标端口 -> class 1:10
奇数目标端口 -> class 1:20
```

profile 配置为：

```text
Super-DC: root 20 Gbps, 两个 10 Gbps 子类, netem 0.05 ms
Mid-LAN:  root 1 Gbps,  两个 500 Mbps 子类, netem 2 ms
Mid-WAN:  root 200 Mbps, 两个 100 Mbps 子类, netem 8 ms
```

最终实测 qdisc class 均有对应发送计数，且 fallback class `1:30` 没有承载测试流量。Super-DC 的高速 HTB class 仍会出现 quantum warning，原因是当前 root/class 速率很高，需要单独选择更合适的 `r2q`；这不影响本次目标带宽验证，但 Super-DC 的微小在线操作仍需谨慎解释。

## 结论

- Mid-LAN 和 Mid-WAN 的实际接收吞吐分别稳定在目标值附近，双向总吞吐约为目标带宽的两倍。
- Super-DC 的双向总吞吐约 `19.891 Gbps`，接近目标 `20 Gbps`；短流单向值略高主要来自有限测试窗口和初始发送行为。
- RTT 实测分别接近理论 RTT：`0.10 ms`、`4 ms`、`16 ms`。
- 这些吞吐结果使用接收端完成时间，不使用发送端 `send()` 返回时间，因此不包含发送 socket buffer 造成的表观虚高。
