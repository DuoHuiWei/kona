# Kona PCR 与 DCF Benchmark 汇总

本文汇总当前工作区中已经实际跑过的 `PCR` 与 `DCF` 比较 benchmark 结果，并说明当前工程化状态。

## 1. 测试对象

- `PCR`：`Machines/kona-pcr-compare.hpp`
  - 当前为 Kona 风格的 `compare_in_vec` 可替换模块。
  - 输出为 `Z2<64>` 算术比较位份额。
  - 内部已经包含批量 `B2A`。
- `DCF`：`Machines/kona-dcf-compare.hpp`
  - 当前为独立抽出的高性能 DCF comparator 模块。
  - 不直接修改 `Machines/kona.cpp` 正式路径。
  - 已做：
    - key/r 预加载
    - 独立模块化
    - batch evaluate 入口
    - fast evaluate 内核（`uint64_t + bit + 16-byte seed`）
- 对比脚本：`Machines/kona-pcr-dcf-bench.cpp`
- 自动运行脚本：`Scripts/run-pcr-dcf-lan-bench.sh`

## 2. 网络配置

### 2.1 无传输延迟

- 本机默认 loopback，无额外 `tc netem` 延迟/限速。

### 2.2 LAN

- 脚本：`LAN.sh`
- 当前设置：
  - 带宽：`1 Gbps`
  - 单程延迟：`0.5 ms`
  - 近似 RTT：`1 ms`

### 2.3 WAN

- 脚本：`WAN.sh`
- 当前设置：
  - 带宽：`40 Mbps`
  - 单程延迟：`20 ms`
  - 近似 RTT：`40 ms`

## 3. 容器内网络整形后的有效结果

这里的 `LAN/WAN` 结果是后来**重新在容器内部的 `lo` 上应用 `tc/netem` 后**得到的有效数据。

重要说明：

```text
早期那组 LAN/WAN 结果是在宿主机 WSL 的 lo 上做 tc，
而 benchmark 实际跑在 kona-dev 容器内部 localhost 通信。
因此早期 LAN/WAN 延迟大概率没有真正作用到容器内通信路径上。
```

当前这里保留的是：

```text
直接在 kona-dev 容器内部 lo 上应用网络整形后
重新跑出的有效结果
```

### 3.1 LAN: 1 Gbps + 0.5 ms one-way

| 规模 | 方案 | 总时间(ms) | ns/compare | 发送字节(bytes) | 传输轮次(transport_rounds) | 逻辑轮次(logical_rounds) | 核心比较轮次(core_compare_rounds) | DCF evaluate 次数 | DCF evaluate 时间(ms) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1000 | PCR | 4.54735 | 4547.35 | 70000 | 16 | 8 | 7 | 0 | 0 |
| 1000 | DCF | 18.7077 | 18707.7 | 8000 | 2 | 1 | 1 | 2000 | 18.5755 |
| 10000 | PCR | 14.8646 | 1486.46 | 700000 | 16 | 8 | 7 | 0 | 0 |
| 10000 | DCF | 187.36 | 18736.0 | 80000 | 2 | 1 | 1 | 20000 | 184.5 |

### 3.2 WAN: 40 Mbps + 20 ms one-way

| 规模 | 方案 | 总时间(ms) | ns/compare | 发送字节(bytes) | 传输轮次(transport_rounds) | 逻辑轮次(logical_rounds) | 核心比较轮次(core_compare_rounds) | DCF evaluate 次数 | DCF evaluate 时间(ms) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1000 | PCR | 160.874 | 160874.0 | 70000 | 16 | 8 | 7 | 0 | 0 |
| 1000 | DCF | 34.6513 | 34651.3 | 8000 | 2 | 1 | 1 | 2000 | 22.0025 |
| 10000 | PCR | 357.035 | 35703.5 | 700000 | 16 | 8 | 7 | 0 | 0 |
| 10000 | DCF | 206.82 | 20682.0 | 80000 | 2 | 1 | 1 | 20000 | 187.666 |

## 4. 无传输延迟历史结果

这里汇总的是此前在“未施加 `LAN.sh` / `WAN.sh` 网络整形”的本机环境下，已经实际跑过的历史数据。注意其中 `DCF` 曾经历多个优化阶段，因此不同规模对应的 DCF 内核版本并不完全一致。

### 4.1 早期无传输延迟结果（旧版独立 DCF benchmark）

| 规模 | 方案 | 总时间(ms) | ns/compare | 发送字节(bytes) | 轮次(rounds) | 备注 |
|---|---:|---:|---:|---:|---:|---|
| 1000 | PCR | 0.595501 | 595.501 | 70000 | 未记录 | 早期对照 |
| 1000 | DCF | 137.748 | 137748 | 8000 | 未记录 | 最早版 benchmark，含重复文件读取 |
| 20000 | PCR | 15.8106 | 790.531 | 1400000 | 未记录 | 无 netem |
| 20000 | DCF | 471.396 | 23569.8 | 160000 | 未记录 | 已去除重复文件读取，但未启用 fast evaluate |

### 4.2 无传输延迟小规模阶段性优化结果

这些数字用于展示 DCF 独立模块的工程优化收益，不与上面四档完整结果直接混表。

| DCF 版本 | 规模 | 总时间(ms) | 说明 |
|---|---:|---:|---|
| 最早版单独 benchmark | 1000 | 137.748 | 每次 evaluate 重新读文件 |
| 独立模块 + key/r 预加载 | 1000 | 56.5412 | 明显提速 |
| 独立模块 + fast evaluate 内核 | 1000 | 20.1704 | 当前最强小规模结果 |

## 5. 结果解读

### 5.1 轮次口径说明

当前 benchmark 中的轮次分为三种口径：

- `transport_rounds`
  - 统计底层 `send/receive` 事件数
  - 例如一次双方交互通常会体现为 `2`
- `logical_rounds`
  - 把一次双方交互视为 `1` 轮
  - 近似等于 `transport_rounds / 2`
- `core_compare_rounds`
  - 只看比较协议核心层数
  - 当前 `PCR` 记为 `7`
  - 当前 `DCF` 记为 `1`

因此，之前文档中直接把 `16` 写成 PCR 的轮次是会误导的。更准确的说法应当是：

```text
PCR:
  transport_rounds = 16
  logical_rounds   = 8
  core_compare_rounds = 7
```

### 5.2 PCR 的特征

- `PCR` 的本地计算很快。
- `PCR` 的通信量明显更大。
- `PCR` 的核心比较轮次当前按协议层记为 `7`。
- 如果把最终 `B2A` 也算进整条比较器的逻辑交互，则当前实验口径更接近 `8` 轮。

### 5.3 DCF 的特征

- `DCF` 的通信量小很多。
- `DCF` 的 `transport_rounds` 当前为 `2`，对应 `logical_rounds = 1`。
- 但 `DCF` 的时间几乎全部花在 `evaluate` 内核中。
- 从表格可以看出，`DCF evaluate 时间` 基本贴近总时间，说明外围工程损耗已经比较小。

### 5.4 LAN/WAN 变化趋势

- `PCR` 在 WAN 下会因为轮次更多而被额外放大，但当前 `40 Mbps + 20 ms` 下仍然保持明显优势。
- `DCF` 在 LAN 与 WAN 下都主要受本地计算支配，因此网络变化对其影响没有 PCR 那么敏感。

## 6. 当前工程化程度

### 6.1 PCR 模块

- 已达到“可独立 benchmark、可作为 Kona compare 后端候选”的工程状态。
- 已具备：
  - 独立模块
  - Kona 风格接口
  - 批量 bitpack
  - 批量 `B2A`
  - `compare_in_vec` 替代形状
- 但仍属于实验路径，尚未直接替换 `Machines/kona.cpp` 正式比较器。

### 6.2 DCF 模块

- 已达到“独立模块 + benchmark 可复用”的工程状态。
- 已具备：
  - 独立模块 `kona-dcf-compare.hpp`
  - key/r 预加载
  - batch evaluate 入口
  - fast evaluate 内核
- 当前仍未直接替换 `Machines/kona.cpp` 中原始 DCF compare 路径。

### 6.3 网络实验脚本

- `LAN.sh` 与 `WAN.sh` 已能表达目标网络设定。
- `Scripts/run-pcr-dcf-lan-bench.sh` 已能自动跑四档规模。
- 但网络整形依赖本机 `sudo tc`，因此：
  - 不是无人工参与
  - 需要手动先应用网络配置

## 7. 工程结论

- 如果目标是“作为 Kona compare 后端候选，追求端到端时间”，当前 `PCR` 明显优于 `DCF`。
- 如果目标是“低通信量、低轮次”的协议方向，`DCF` 仍有结构性优势。
- 但从当前实际实现看，`DCF` 的本地 `evaluate` 成本仍然过高，即使做了工程优化，依然显著慢于 `PCR`。
- 因此，现阶段更现实的结论是：
  - `PCR` 更适合当前工程落地；
  - `DCF` 若要继续竞争，需要继续深挖 evaluate 内核，而不是只做外围小修。
