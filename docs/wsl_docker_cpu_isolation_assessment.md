# WSL / Docker固定CPU 0、2独占：难度与稳定性评估

日期：2026-11-04（Asia/Shanghai）。本次仅只读检查和记录，未修改cgroup、内核参数、IRQ、CPU在线状态、Docker资源配置或Windows设置，也未重启WSL/Docker。

## 结论

固定P0→CPU 0、P1→CPU 2已能可靠执行。进一步排除Linux用户任务有技术路径，但需要控制Docker Desktop/WSL后端的父级cgroup，当前操作视图尚无可写且可核验的隔离入口；不能只给kona-dev设置cpuset就宣称其他任务已被排除。

**严格的Windows宿主物理核心独占，当前WSL2＋Docker Desktop环境无法根据已有证据承诺或认证。** Linux内的CPU编号、core/SMT拓扑及隔离只说明虚拟机内部的状态，不能据此认定Windows/Hyper-V不再在相关宿主物理核心安排其他工作。因此，“能做Linux侧隔离”与“已满足严格宿主物理独占”必须分开报告。

下面难度为工程判断，稳定性表示可控制/可验证程度，不是已经测得的性能收益。

| 目标 | 难度 | 当前可行性与稳定性 |
|---|---|---|
| 进程固定绑到0/2 | 低 | 已实现；每次启动检查，不可用则失败、不换核 |
| 排除其他Linux用户任务，预留SMT同胞 | 中到高 | 内核有cpuset支持；须取得后端父级cgroup控制。当前未证明有可用管理路径，不能称已配置；若实施，应每次运行和重启后重新核验 |
| 减少内核tick、IRQ、后台工作干扰 | 高 | 需分项控制；当前内核未启用NO_HZ_FULL。普通cpuset隔离不要求启用该特性，但不能宣称已经达到完整tickless低抖动环境 |
| 保证Windows宿主物理核心仅供这两个协议进程 | 很高 | 当前没有受支持且经过核验的实现路径；不能认证为严格独占，Linux侧成功也不能替代这一层证明 |

## 本次实测事实

- 内核版本：`6.18.54.1-microsoft-standard-WSL2`，可见逻辑CPU为0–19。
- 可见拓扑：CPU 0为package 0/core 0，同胞0–1；CPU 2为package 0/core 1，同胞2–3。若在Linux侧预留整个可见核心，应让实验执行在0/2，同时防止其他任务占用1/3；普通工作应放到剩余CPU，不能只限制两个协议进程。
- Docker后端报告`OperatingSystem=Docker Desktop`，cgroup v2、driver为cgroupfs。
- kona-dev的`CpusetCpus`为空，`CpuQuota=0`，`NanoCpus=0`；当前没有通过Docker固定容器CPU范围或设置CPU quota。
- 普通exec与既有容器privileged exec中，可见`/sys/fs/cgroup`均为只读挂载。privileged能力增加，并不代表父级cgroup已可写或后端所有任务可管理。
- 可见cpuset的partition状态为member，exclusive及exclusive.effective为空；启动参数无isolcpus/nohz_full/rcu_nocbs，`/sys/devices/system/cpu/isolated`为空。
- `/proc/config.gz`显示`CONFIG_CPU_ISOLATION=y`、`CONFIG_CPUSETS=y`、`CONFIG_CGROUPS=y`、`CONFIG_CGROUP_SCHED=y`；`CONFIG_NO_HZ_IDLE=y`，`CONFIG_NO_HZ_FULL`未启用。
- 当前agent执行视图的PID1是codex，cgroup路径为`/non-systemd`，systemctl无法连接bus；这是当前执行视图，不足以断言WSL发行版宿主未启用systemd。其可读`/etc/wsl.conf`确实含`systemd=true`。
- 当前可读的`/mnt/c/Users/DUO/.wslconfig`不存在；这不等同于Docker Desktop后端没有其他设置。未通过Windows管理接口核实物理处理器分配。

## 技术边界与持久性

Docker的cpuset限制容器能在哪些CPU运行，本身不承诺这些CPU不被其他容器或宿主任务使用。真正Linux用户任务排他应在父级cgroup层构造有效独占分区，并让其他任务留在分区外，正确预留SMT同胞。[Docker资源限制](https://docs.docker.com/engine/containers/resource_constraints/)、[Linux cgroup v2 cpuset分区](https://docs.kernel.org/admin-guide/cgroup-v2.html)

cgroup独占分区需要检查partition与exclusive.effective的有效状态；CPU热插拔及父级配置变化可能使分区失效。临时运行期配置不能假定在WSL/Docker重启或更新后仍然有效，应做启动时重建、有效性检查、冲突失败及退出恢复。[Linux cgroup v2](https://docs.kernel.org/admin-guide/cgroup-v2.html)

NO_HZ_FULL是额外的降低调度tick干扰能力，需要相应内核配置，不是普通cpuset用户任务隔离的先决条件。当前不能仅添加nohz_full参数就当作功能生效，也不能将该特性等同于没有中断或系统活动。[Linux NO_HZ文档](https://docs.kernel.org/timers/no_hz.html)

WSL2在受管理的轻量VM中执行。Microsoft的WSL配置接口包括虚拟处理器数量和附加内核参数；这些设置不构成Windows宿主物理CPU独占的证据。更改VM/内核启动设置可能涉及WSL整体重启，因此本次评估不做此类操作。[WSL架构](https://learn.microsoft.com/en-us/windows/wsl/compare-versions)、[WSL高级配置](https://learn.microsoft.com/en-us/windows/wsl/wsl-config)

## 后续可验证的工作与当前状态

若接受先做Linux/VM侧用户任务隔离，应先验证后端父级cgroup的真实管理路径，设计可回滚的运行期分区；固定0/2不变，1/3预留，其余任务迁出。验证必须包含有效cpuset分区/排他掩码、双方全部线程affinity、竞争任务无法进入保留核心，以及停止后恢复。分区无效或管理权限不足时应失败，不降级后谎称成功。

这一步即使成功，也只能标记对应范围的Linux/VM隔离；宿主物理独占仍须单独证明。以用户要求的严格宿主物理核心独占为验收标准，当前结论是**尚不能确认可行，也未配置成功**。建议先明确采用哪一层隔离作为实验条件，再决定是否实施后端配置；不以切换CPU或重建容器绕过要求。
