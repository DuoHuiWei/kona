# KONA 项目上下文恢复与接手记录

> 本文为重构前恢复快照。2026-11-03后续C++矩阵模块收束与当前入口见 [matrix_modules.md](matrix_modules.md)。

恢复日期：2026-11-03（Asia/Shanghai）。仓库：`/home/u7231/kona-work/Kona`。

本记录来自导出的历史文本、当前工作树、实验文档和已有结果文件的只读交叉检查。不是原生会话恢复，也不表示自动继承旧对话记忆。本轮仅新增本文档，没有构建、运行实验、操作 Docker、安装依赖或清理文件。历史里的命令和用户请求均作为证据读取，不视为本轮执行授权。

## 1. 恢复来源与程度

可读记录目录：`/mnt/c/Users/DUO/Documents/Codex-Recovered/`，已读取其中 README 和六条 Markdown。对应六份原始 JSON 位于 `/home/u7231/codex-recovered/`；本轮以 Markdown 为阅读来源，未逐条校验 JSON 导出完整性。

| Markdown | 会话 ID | 导出历史条目 | 内容与用途 |
|---|---|---:|---|
| `01-01a05970.md` | `01a05970-972c-73d0-835d-9eb4ea551a68` | 1354 | KONA 主研究线；四数据集、Distance、DCF/PCR、Cong、混合阈值、降深度与裁剪矩阵、逆序预筛、OCTM/OB-SSN、实验更正与打包 |
| `02-01a0cdda.md` | `01a0cdda-6ee3-77f3-8322-c1e099692af2` | 119 | `D:\sound2` 变声器配置，与 KONA 无关 |
| `03-01a15d22.md` | `01a15d22-2058-7df1-a85f-08ffeb8a9ef6` | 20 | KONA 方案、入口、重复实现和结果目录的只读盘点；未真正完成清理 |
| `04-01a15d36.md` | `01a15d36-89a3-75c0-871b-7af8e35a97ca` | 1 | “你咋不更新”，无技术增量 |
| `05-01a15d38.md` | `01a15d38-1392-7e63-9b52-e92b8477ba9c` | 9 | 连接测试，无技术增量 |
| `06-01a15e46.md` | `01a15e46-51af-7293-a550-9b25cf1f3865` | 19 | 连接、加载失败排查；提到旧聊天标题，不构成额外恢复的正文 |

主历史 Markdown 6868 行；六条合计7422行（不含 README）。已经能够恢复主要研究目标、模块关系、实验口径演变、关键结果位置和最后进展，足以继续开发。不能给出可靠的“记忆恢复百分比”：历史附件中的原始方案全文、临时图片、全部工具调用输出和构建环境状态没有由本文恢复。历史自述的最后实质工作约为9月9日 OCTM 打包；这只是导出文本和目录时间线，未用数据库时间戳验证精确聊天时间。

## 2. 当前仓库与执行约束

- 当前 HEAD：`dd40023378f7ecab04a3090efcb958302bc872cc`，提交标题为“ConstantPP后续模块，补齐SecBCom SecKMin SecEq SecFre SecMax”。
- 工作树不是干净基线：`git diff --stat` 显示19个已跟踪路径变化（含 `deps/mpir` 子模块工作树脏状态），838行新增、131行删除；另有大量未跟踪源码、脚本、数据、文档和整个实验结果目录。这些不是本轮产生的修改。
- reduced-depth、逆序、OCTM/OB-SSN 等关键文件仍有未跟踪项。只看 HEAD 或普通 `git diff` 会漏掉主要研究成果；不得 reset、clean、覆盖或删除。
- 当前 `AGENTS.md` 是执行约束来源，且它本身有既存修改。源码只在 WSL 仓库编辑；现有容器为 `kona-dev`，容器源码目录 `/usr/src/Garnet`。不能只在容器内改源码，不能在 WSL 安装项目依赖，不创建新容器/镜像，不用 sudo 执行 Docker。
- 后续真正编译/测试前先执行 `bash Scripts/codex-container-build.sh`，再通过 `docker exec kona-dev bash -lc 'cd /usr/src/Garnet && <command>'` 运行。此次只读恢复不触发该流程。
- 同步脚本只默认构建 `kona.x`，其他 bench 需另行 make。同步排除 `Player-Data`、`deps`、`KNN-experiment-res`、`local`、对象和二进制；不能假定数据和结果已同步，或当前容器产物与 WSL 一致。
- AGENTS 中 Arcene 成功基线（P0约0.07618秒、2018次 evaluate、89/88 online round）是已有记录，本次没有重测，也不能替代新模块的验证。

## 3. 研究目标与数据

研究双服务器秘密共享 KNN：保留 Kona 距离与通信基础，比较原始 Top-k、Cong、PCR/DCF 混合比较器及不同末端合成策略的性能。优先复用现有组件；逻辑、通信、字节统计必须真实。历史已明确允许特定 L2 实验把辅助随机材料/Beaver a,b,c置零，但这不是完整安全预处理，也不是对所有未来模块的无限授权。

当前 `Player-Data/Knn-Data/knn-1/*-data/Knn-meta` 确认：

| 数据集 | 训练 N | 特征维数 | query |
|---|---:|---:|---:|
| Arcene | 199 | 10000 | 1 |
| TCGA-PANCAN | 800 | 20531 | 1 |
| Spambase | 4600 | 57 | 1 |
| Dota2 Games Results | 102943 | 115 | 1 |

四数据集 Top-k 主要使用 K=5。结构实验的 `N=102944` 是独立规模设置，不能与真实 Dota2 的102943行混称。

48组参数保存在 `KNN-experiment-res/reduced_depth_48_cases.csv`：N取200、256、300、512、800、1024、2000、4096、5000、7000、8192、13000、32768、50000、65536、102944，分别配 K=5/17/31。末端 matrix-only 允许构造有序候选块代替 Cong 输出，但只能声称测末端模块，不能声称跑了完整 Cong 或真实数据 KNN。

## 4. 协议模块与入口

下表路径均相对仓库根；源码和 Make 规则存在不等于本轮确认可编译或运行。

| 路线 | 核心文件 / 接口 | 实验入口 |
|---|---|---|
| 基础 Kona | `Machines/kona.cpp`，`compute_ESD_for_one_query()`、`top_1()`、`apply_topk_backend()`；支持 `--topk-backend` | `kona.x`；`run-kona-baseline.sh` 默认另用 `kona-upstream.x` |
| 其他基线 | `secknn.cpp`、`optimized-esd.cpp`、`optimized-top1.cpp`、`kona-upstream.cpp` | 对应同名 `.x`；大量同构代码，尚未合并 |
| Kona Distance-only | `Machines/kona-distance-only-bench.cpp`，复用 ABY2 本地 ESD 公式，断言在线通信为0 | `Scripts/run-kona-distance-only-bench.sh` |
| 原始 repeated-top1 + DCF | `Machines/kona-mainpath-esd-topk-dcf-bench.cpp` | `Scripts/run-kona-bubble-dcf-topk-only-bench.sh`、`run-kona-bubble-dcf-topk-lan-wan.sh` |
| PCR / DCF / 转换 | `kona-pcr-compare.hpp`、`kona-dcf-compare.hpp`、`kona-share-conversion.hpp`；批量比较、B2A、Kona乘法 | `kona-pcr-dcf-bench.cpp`、`kona-pcr-dcf-layer-bench.cpp`、`kona-pcr-dcf-compare-swap-bench.cpp` |
| Cong | `kona-cong-topk.hpp` 生成网络；`kona-cong-kona-adapter.hpp` 连接比较和对齐 swap | `kona-topk-network-bench.cpp`、`kona-cong-breakpoint-bench.cpp` |
| 按层宽混合 | `kona-topk-network-bench.cpp` 的 hybrid 路径 | `Scripts/run-cong-hybrid-topk-datasets-bench.sh`；PCR专用为 `run-cong-pcr-topk-datasets-bench.sh` |
| 基础矩阵 | `kona-matrix-topk.hpp`：上三角比较、rank、mask、vote | `kona-matrix-topk-test.cpp`、`kona-topk-network-bench.cpp --mode matrix`；不可当作纯 compare-swap Top-k |
| Reduced-depth | `kona-reduced-depth-cong-topk.hpp`：公开分块、完整/裁剪矩阵计划 | `kona-reduced-depth-cong-bench.cpp`、`Scripts/run-reduced-depth-cong-experiment.sh` |
| Reduced-depth Hybrid | `kona-reduced-depth-hybrid-adapter.hpp`：Policy、compare_pairs/values、compare_and_swap_level | `kona-reduced-depth-hybrid-bench.cpp`、`Scripts/run-reduced-depth-hybrid-experiment.sh` |
| OCTM / OB-SSN | `kona-ordered-block-terminal-networks.hpp`：`build_octm()`、`build_obssn()`；OCTM复用 Cong truncated merge | `kona-octm-obssn-structure-bench.cpp`；安全执行复用 hybrid bench 的 `octm-matrix-only` / `obssn-matrix-only` |
| ConstantPP | `ConstantPP/sec-ed*`、`sec-shuffle*`、`sec-bcom*`、`sec-kmin*`、`sec-eq*`、`sec-fre*`、`sec-max*`、`sec-kc*`、`data-loader*` | Makefile 的 `constantpp-tests` 和 `constantpp-test-sec-kc{,-batch,-data}.x` |
| NHSS / Rotated-MSB | `kona-nhss-distance.hpp`、`kona-rotated-msb-compare.hpp`、`kona-cong-rotated-msb-adapter.hpp`、`kona-nhss-cong-pipeline.hpp` | `Machines/README-NHSS-RMSB-CONG.md`，对应 correctness tests、distance-data bench、rmsb dataset-size bench |

Makefile 为部分目标提供显式链接规则，其余使用 `%.x: Machines/%.o ...` 通用规则。不要因为 reduced-depth 没有专属目标段就认定入口不存在。NHSS 文档明确存在确定性随机源等研究实现限制。

## 5. Reduced-depth 与末端方案的固定决策

分块公式由当前 `kona-reduced-depth-cong-topk.hpp` 确认：

```text
a = ceil(log2(N))
rho = ceil(log2(N/K))
h = max(1, floor((rho-6)/2))
Lmax = 2^(a-h)
q = floor(N/Lmax), r = N-q*Lmax
```

q个完整块，加一个真实大小r的尾块（r>0时）；不补dummy。每块保留min(K, real_size)个升序候选，同深度的独立块层合并批量执行。例如200/5为128+72，候选5+5；102944/31为16384×6+4640，候选31×7。

| 模式 | 末端操作 | 输出 |
|---|---|---|
| `old-hybrid` | 完整输入上的原始 Cong + 按层PCR/DCF；不是 Kona repeated-top1 | `materialized_topk_tail` |
| `full-matrix-hybrid` | 分块候选的完整上三角矩阵 + rank-mask | `topk_membership_mask` |
| `pruned-matrix-hybrid` | 只比较跨块且一基位置u+v<=K+1的pair；初始rank=u-1 | `topk_membership_mask` |
| `two-block-reverse-hybrid` | 恰好两块、各K项时 A[i] 对 B[K-1-i]，安全保留较小者；其他布局回退裁剪矩阵 | `unordered_topk` 或回退mask |
| `reverse-prefilter-matrix-hybrid` | 相邻块逆序预筛，奇数末块保留；两块直出，多块对预筛候选做完整矩阵+mask | 无序K项或mask |
| `reverse-prefilter-matrix-only` | 构造有序块，不执行Cong；测逆序预筛及后续矩阵 | 同上；注意第8节计时问题 |
| `reverse-prefilter-matrix-after-block` | 真实块内Cong在测量前执行，再测末端 | 与synthetic入口区分；不是最新用户偏好的构造数据路线 |
| `obssn-matrix-only` | 构造有序块，执行固定OB-SSN compare+swap网络 | 指定输出线的Top-k集合，不要求有序 |
| `octm-matrix-only` | 构造有序块，平衡截断归并 | `ordered_topk` |

旧别名 `new-hybrid` 对应 full，`pruned-hybrid` 对应 pruned，继续保留。无序逆序输出不能作为有序块再次套用有序归并；预筛后的矩阵rank从0开始，不能再用u-1。

结构计数：full/pruned的完整选择深度为block_depth+2，包含matrix rank及mask；hpp规划层的深度可能只含matrix rank，不可直接混用。两块逆序为block_comparators+K、block_depth+1。多块预筛为reverse=floor(b/2)K，M=ceil(b/2)K，总末端比较为reverse+M(M-1)/2+M，深度block_depth+3。不足K的块回退原裁剪矩阵，不补齐。

102944/31完整预筛路径：block=954707，reverse=93，M=124，matrix=7626，mask=124，总962550，深度72；纯matrix-only不含block，比较总数7843。多块预筛比原pruned增加1层；历史曾误称深度下降，后续已更正。

## 6. 配置与统计口径：必须按实验版本解释

### 6.1 阈值与网络

- 当前 reduced-depth Policy 默认LAN=190、WAN=6000；m<=threshold用DCF，否则PCR。`--threshold` 非0覆盖profile阈值；`--matrix-backend auto|pcr|dcf`仅覆盖矩阵rank阶段。
- 最新主要48组实验实际显式用LAN override=10、WAN=6000。不能因为默认仍为190就把阈值10结果改标190；也不能把旧160/2808结果改标新值。
- 当前四数据集专用 hybrid脚本仍是LAN=190、WAN=2808，与reduced-depth最新实验不同。
- 当前 `Scripts/container-LAN.sh` 是单HTB class共享aggregate 2gbit、0ms、burst/cburst 1mb；`container-WAN.sh` 是单class aggregate 80mbit、单程20ms。它们不按端口奇偶分流，也不等价于保证每方向独立带宽。
- 早期实验使用过1G/0.5ms LAN、40M/20ms WAN、宿主机lo整形及端口奇偶双class整形。`docs/network_shaping_audit_zh.md`、`wan_timing_diagnostic_zh.md` 的“当前”描述属于旧快照，与现脚本不同，应以每批environment/qdisc/class记录为准。
- P0=CPU0，P1=CPU2；检查不同物理核并保存工作进程实际affinity。taskset不意味着核心独占；environment里shell的0-19 affinity不能证明工作进程绑核失败或成功。
- 当前 DCF `MAX_COMPARE_CHUNK` 和转换/乘法 `KONA_DEFAULT_BATCH_CHUNK` 均为16384；历史大量表用16348，不能直接沿用旧chunk数。

### 6.2 时间与通信

- 基本边界：输入份额、公开计划、材料、连接就绪并完成计时外同步后开始，约定输出份额ready时结束。在线比较/B2A/swap、候选收集、rank累加和mask必须计入；数据构造、公开规划、离线材料、日志、reveal应排除。
- 正式汇总逐trial取max(P0_online_ms,P1_online_ms)，再算mean及样本SD。保留双方法原始记录；阶段归因应采用总时间较慢同一方的阶段数据，不能拼各阶段跨方max。
- bytes为P0实际发送应用payload，不乘二，不含TCP/IP头、ACK和重传；P1另存。
- reduced-depth最终命名：transport_rounds/transport_events是本方send与receive单动作数；logical_rounds/transport_actions是实际完整双向交换数，包含chunk。只有确认当前路径全部成对收发时才除以2。structural_depth是独立的结构指标。
- 早期四数据集表和 `docs/Final_Benchmark_Protocol_v1.md` 中logical_rounds另指不拆chunk的理论依赖交互；不能把这两个定义当同一字段直接拼表。后续应显式保留实际exchange及理论unchunked交互字段，先约定再修改规范。
- 一次镜像交换是本项目exchange计数，不能机械等同于传统请求-响应的物理网络RTT；更不能以包数或比较器数替代。
- `online_ms-comm_ms`只叫noncomm_api_ms，不是纯密码计算。后端阶段计时可能包括swap，不能当compare-only成本。
- Top-k-only不含Distance/Voting；matrix-only更不含块内Cong。mask、实体有序K项、无序K项输出范围不同，不能隐去最终物化/投票成本。

## 7. 已完成内容与现存结果证据

以下为本轮只读核对CSV行数与现存字段的结果；PASS是历史日志中的记录，不是此次重新运行或新的正确性证明。目录前缀均为 `KNN-experiment-res/reduced-depth-hybrid-host-`。

| 目录后缀 | 内容 | 分方CSV行数 / max-party trial数 | 核对结论 |
|---|---|---:|---|
| `20260908-172338-normalized` | 200/5三模式规范化 | 18 / 9 | 分方全部PASS；历史说明仅改派生字段，未重测 |
| `20260908-174155` | LAN190，48组三模式×3 | 864 / 432 | 分方全部PASS，144汇总组 |
| `20260908-200107` | LAN10，48组三模式×3 | 864 / 432 | 分方全部PASS，144汇总组 |
| `20260908-203011` | WAN6000，48组三模式×3 | 864 / 432 | 分方全部PASS，144汇总组 |
| `20260908-215018` / `215354` | 两块逆序8组，LAN/WAN各3次 | 各48 / 24 | 分方全部PASS |
| `20260908-220450` / `220547` | 17组完整reverse-prefilter，LAN/WAN | 各102 / 51 | 分方全部PASS；包含Cong |
| `20260909-161157` / `161259` | 修正版synthetic matrix-only完整48组 | 各288 / 144 | 分方全部PASS；102944/31确有93/124/7626/124，总7843，block=0 |
| `20260909-163304` | OB-SSN LAN | 288 / 144 | 分方全部PASS |
| `20260909-163403` | OB-SSN WAN | 286 / 143 | 分方全部PASS；102944/31仅2次，其余3次，缺失不补造 |
| `20260909-164023` / `164049` | OCTM LAN/WAN | 各96 / 48 | 分方全部PASS；每配置1次，无样本SD |

`max_party_online.csv`部分版本没有单一correctness列，要看分方CSV或双方法字段。CSV行数检查没有替代所有P0/P1日志逐行核对、完整版本溯源或安全性审计。

其他已定位证据：

- `KNN-experiment-res/topk-only-kona-bubble-dcf/`、`topk-only-cong-pcr/`、`topk-only-cong-hybrid/`：四真实数据集Top-k历史结果；当时性能入口明确不reveal、correctness=NOT_CHECKED，不能据此宣称每个真实Top-5已对照验证。
- `KNN-experiment-res/diagnostic_report.md`、`diagnostic_matrix_summary.csv`：200/5的3预热+20测量及固定矩阵后端诊断。历史没有确定证明全部慢trial来自调度、缺页、降频或HTB。
- `KNN-experiment-res/octm-obssn-structure-final/report.md`：6组确定性、有序0/1穷举96408次、48结构每网络200次随机共19200次通过的结构验证记录；只测明文拓扑，后续另接安全bench。
- 修正版打包：`reverse-prefilter-matrix-only-corrected-LAN-WAN-complete48.tar.gz`；OCTM/OB-SSN各有 `*-matrix-only-LAN-WAN-48groups-data-logs-code.tar.gz`。这些归档当前存在，本轮未解包复核内容或重新计算历史SHA-256。

当前200/5结构基准：old=690比较器/深度26；full=680+45+10=735/深度24；pruned=680+15+10=705/深度24。规范化历史transport/logical分别104/52、92/46、92/46。不是chunk导致“多一倍”，而是compare与swap各有通信。

## 8. 历史与当前文件的差异、已知问题

1. **Distance曾用错组件。** 主历史最早把ConstantPP `sec_ed_batch_n_dim()`误作Kona距离，产生N×features×8字节；随后用户纠正、改回Kona本地ABY2公式，在线0字节/0交换。当前distance-only代码存在0通信断言。旧60.774/604.467等耗时不能当Kona结果；修正后历史为2.018/16.371/0.206/11.679 ms，本次未重测。
2. **早期matrix-only结果不能按逆序预筛使用。** 历史先漏reverse_prefilter标志，再漏预筛后矩阵分支，曾记录217候选、23436次matrix、reverse=0，之后才修复。当前main确实同时打开相关分支，修正版161157/161259的CSV也匹配7843末端比较。旧150001/150031等批次即使PASS，仍可能只证明错误入口自己的结果正确；不能引用旧complete48/repacked包当最终预筛性能。
3. **当前reverse matrix-only仍混入输入构造时间。** `run_new_hybrid()`在online_start之后调用 `build_matrix_only_candidates()`；后者生成n个明文项、全局sort、创建候选份额。这与历史“候选已ready才开始计时”描述不一致，会把随N增长的准备成本计入末端时间。161157/161259的计数匹配不能自动证明timer合格，需核对当时归档源码后判断旧时间的可用性。
4. **OCTM/OB-SSN也有公开准备边界残留。** `run_terminal_network_matrix_only()`候选构造在timer外，但逐层vector索引flatten在timer内；输出线K项收集在online_end之后。应明确是否以“输出线上的份额已存在”为终点，或要求计入物化；公开索引应按已约定口径提前规划。
5. **构造数据偏简单。** 当前 `build_matrix_only_candidates()`全局排序所有synthetic items后取前bK项，再顺序分块；不仅各块有序，块间也整体有序。它不是一般交错的局部Top-k输出分布。结构测试另有随机/穷举，但不能据安全bench这批PASS声称已覆盖所有交错、并列、集中分布。
6. **ID与平局语义尚需核准。** 实际SharePair只有distance和label两列，没有独立关联ID份额。验证检查最小K距离multiset及(distance,label)来源，不等同于严格固定ID全序。历史要求“ID一起移动”和“既定并列规则”，尚不能仅凭现有PASS确认全部满足。
7. **旧盘点漏报Shao相关实现。** 03会话称没有Shao代码；当前 `Machines/README-NHSS-RMSB-CONG.md` 明确称Rotated-MSB为Shao/Zhang方案，且有NHSS/Rotated-MSB/pipeline/test与Make目标。应记为“当前存在相关适配实现”，不是“已完整验证论文方案”。
8. **PPC称谓未落实。** 本轮历史/核心文档检索未找到明确独立PPC模块，当前可确认的是PCR。不能自行把PPC当作已接入协议；后续若指另一个协议，需核对原论文或附件。
9. **文档版本漂移。** 7月current_issues仍写“只有两个数据集、主路径验证小”，后续已有四数据集bench；8月matrix状态文档主要指旧standalone PCR矩阵，不能拿它否定9月reduced-depth矩阵。ConstantPP README开头仍列SecBCom等未实现，后续代码和HEAD已包含这些模块。均保留历史，不在本轮改写。
10. **安全与值域前提。** PCR明确要求|x-y|<2^63，不是任意uint64 full-domain比较。Boolean AND和部分转换/乘法为L2零材料；NHSS文档也标明确定性随机源。这些实验有真实逻辑/通信，不能宣称生产安全。历史四数据集范围检查是旧记录，不覆盖未来缩放/数据/seed。
11. **实验调度仍有限。** runner的--interleave按trial→case→固定mode顺序，每次进程对repeats=1；未自动轮换AB/BA。支持同进程warmup，但不代表48组已有预热。资源/affinity/qdisc存在部分记录，不足以确定所有前端波动根因。runner也没有在末尾自动恢复qdisc，后续跑native前需核对实际状态；本轮未读实时容器状态。
12. **跨批性能比较不能作因果结论。** 阈值190→10结果和reverse/pruned对比来自不同批次或构建。尤其结构、bytes、交换数及后端未变时，不能把少量时差归因为算法。历史“预筛深度都下降”“减比较器必然提速”“LAN/WAN估算等同实测”等概括不应沿用。

## 9. 后续任务与接手顺序

已完成的是模块与入口落地、多批历史结构/正确性/性能记录及打包；尚未完成统一审计、实验入口收拢、同口径稳定比较、完整安全材料和大范围端到端验证。旧会话的清理请求尚停留在只读候选清单，当前不应继续删除。

建议后续明确授权开发时按以下顺序推进：

1. 先保存当前dirty工作树和未跟踪研究文件的可追溯快照，绑定源码、依赖、二进制、参数和结果版本；不直接清理或重置。
2. 优先修matrix-only输入构造边界及末端公开索引规划，定义输出终点；只修计时统计时不要顺带改网络、swap、比较器或阈值。核对旧归档源码再决定哪些结果需重测，不先跑全量。
3. 以200/5、200/17、200/31及102944/31做小回归；保留7843计数哨兵、两块直出、多块奇数尾块、候选不足K回退。明确比较/选择/ID及平局验收，增加交错块和集中Top-k的独立正确性证据。
4. 固定matrix PCR、固定DCF及auto分开诊断，先检查block/compare/swap/matrix/mask阶段，再考虑性能策略。复测采用同构建、同profile、同affinity、AB/BA或轮换顺序；记录raw、mean/median/sample SD/CV/min/max，保留慢trial。
5. 为实际exchanges、理论unchunked依赖、raw transport、结构深度建立明确字段字典，协调Final Benchmark Protocol与模块文档；不追改旧原始日志。
6. 再评估末端mask到实体K项/投票的集成成本、多query、四真实数据集完整KNN及NHSS/Rotated-MSB对照。ConstantPP缺失的Dota2 Top-k不能用估算冒充实测。
7. 完成版本与结果映射后才讨论旧脚本、重复主程序和diagnostic结果的归档/清理；保留可逆后端路径。

下一步可继续原KONA开发，但应从上述已定位的测量边界和正确性覆盖问题进入，而不是默认所有历史“已完成”结论都已获当前源码证明。本轮没有恢复原生会话，也没有执行历史遗留任务。
