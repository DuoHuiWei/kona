# Cong 分块与矩阵方案公共模块

> 2026-11-06表10定义确认：FullMat使用所有候选的上三角比较，包含同块比较，不是只比较跨块；总比较数为 `C(Bk,2)+Bk`（含排名阈值）。独立B扩展实验中，FullMat／PCM的排名、rank-mask和投票均固定批量DCF；块内Top-k的混合策略不属于该实验计时范围。已有WAN20ms结果只补PCM的B32旧PCR点，其余有效点复用。单点入口：`python3 -u Scripts/run-matrix-block-count.py --blocks 32 --profiles wan --methods PCM --repeats 3`。合并数据见 [表10记录](../KNN-experiment-res/matrix-block-count-20261106T010350Z-391550/table10_wan.md)。

> 2026-11-04更新：当前混合方案投票已切换到 `kona-mask-vote.hpp` 中的 `vote_dcf_optimized()`，包括矩阵诊断、共享前缀及完整KNN路径。计数和选最大均为DCF；原PCR接口保留用于旧对照，下文PCR描述属于此前版本。CDF计数、公开全选/Top-1和只保留赢家的优化详见 [记录规范](bench_recording_protocol.md)。


2026-11-03。协议主体使用 C++；Python 只用于串行启动回归与检查结果。
原始 `kona.cpp`、`kona-upstream.cpp`、ConstantPP 及其原始协议不改。

## 文件边界

| 文件 | 职责 |
|---|---|
| `Machines/kona-cong-topk.hpp` | 原有 Cong 生成器与通用调度，原样复用 |
| `Machines/kona-reduced-depth-cong-topk.hpp` | **统一矩阵模块**：无padding分块、小Cong规划与执行、候选收集、完整/裁剪矩阵配对及rank/mask执行、组合接口；也保留历史逆序/OCTM/OB-SSN路径 |
| `Machines/kona-reduced-depth-hybrid-adapter.hpp` | 原比较器、swap、阈值选择；新增显式全阶段后端 override，默认 AUTO 不变 |
| `Machines/kona-mask-vote.hpp` | 从旧矩阵函数迁出的 PCR 标签票数统计，加最终秘密最大票选择，只返回类别份额 |
| `Machines/kona-matrix-topk.hpp` | 原 standalone tiled PCR矩阵仍保留；其投票统计转调公共函数，不再复制票数逻辑 |
| `Machines/knn-bench.cpp` | 唯一分块矩阵 bench：准备输入、测量、记录、计时外验证 |
| `Scripts/run-knn-experiment.py --scheme hybrid` | 唯一矩阵批量 runner；串行双进程、CPU检查、profile配置、原始与max-party及aggregate CSV |

两个旧 reduced-depth bench `.cpp` 已从源码入口删除，原内容保存在本次回归的source-before目录。
当前构建目标统一为 `knn-bench.x`，旧矩阵别名已移除。
两个旧 `run-reduced-depth-*-experiment.sh` 也改为新 runner 的兼容别名。
旧可执行名仍可 make，旧 pure-mode 名 old/new-pcr/dcf映射至同一个实现。
这意味着重新编译旧名字得到的是新测量契约，不能把它当作旧二进制复现实验。
独立网络结构测试、原始Kona基线、DCF/PCR微基准、历史CSV/日志/压缩包未删除。

## 可独立调用的 C++ 接口

`kona-block-matrix.hpp`已移除，规划与执行统一放在上述一个头文件、同一命名空间`KonaReducedDepthCongTopK`中。
原Cong生成器、比较/阈值适配器、投票模块继续被调用，不复制其实现。

公共模块均不创建网络连接、不生成测试数据、不打印、不reveal、不访问bench全局party。
调用者提供 `RealTwoPartyPlayer`、现有DCF对象、份额与Policy。

```cpp
#include "Machines/kona-reduced-depth-cong-topk.hpp"

using namespace KonaReducedDepthCongTopK;
auto prepared = prepare_reduced_plan(n, k); // 复用既有Cong生成器
// 分块公式不变，真实尾块不补齐。
auto full = prepare_terminal(prepared, Terminal::Full);
auto pruned = prepare_terminal(prepared, Terminal::Pruned);

// 无需任何Cong规划，也可以只构建独立矩阵：
auto matrix = prepare_matrix({k, k, 3}, k, Terminal::Pruned);
// 输入须是按这些公开块大小排列的、每块升序的候选份额。
// full需要候选集合；pruned额外依赖这些块内有序信息。

// 原比较/转换/乘法/swap与默认阈值保持不变。
Policy policy; // LAN190 / WAN6000；threshold_override可显式设为10等
NoObserver observer;
auto work = input_shares; // 恢复缓冲区放在timer外
// 两方计时外同步后：
auto output = execute_topk<64>(work, prepared, pruned, profile,
        policy, dcf, player, observer);
// 或分别调用：
// execute_blocks<64>(work, prepared, profile, policy, dcf, player, observer);
// auto candidates = collect_block_candidates<64>(work, prepared.plan, prepared.block_offsets);
// auto output = execute_terminal<64>(std::move(candidates), matrix, k,
//        profile, policy, dcf, player, observer);
```

`Output<64>`提供候选份额、rank份额、mask份额及output_scope。
Full/Pruned的结果为候选＋秘密membership mask，不自动做oblivious compaction。
无序K项和有序K项分支不伪装成mask，调用者必须保留输出范围。
阶段observer签名为 `(phase, stage_id, comparators, operation)`，必须恰好执行operation一次。
bench用observer获取时间/通信快照；普通调用直接用NoObserver。
完整Top-k包含块内执行、候选收集及末端全部在线操作。

分块：`a=ceil(log2 N)`、`rho=ceil(log2(N/K))`、
`h=max(1,floor((rho-6)/2))`、`Lmax=2^(a-h)`。
2026-11-04新增外层取消条件：按原公式算出候选主块大小后，若
`uint64_t(K) * 4 > uint64_t(Lmax)`，直接调用原 `Cong(N,K)`，只保留一个
包含N条真实记录的块，输出K个有序候选，跳过块间矩阵和rank-mask。
等于四分之一仍分块。分母只用候选主块大小，不用尾块长度或N；Cong内部
递归切分、比较器切换、swap、距离和安全投票均保持原实现。

`NetworkPlan::outer_partition_cancelled`记录选择；`lmax`始终是原公式的
候选大小，取消后实际布局的`q/r`为1/0，唯一块的real/padded size为N。
`prepare_terminal`对取消路径返回零比较的direct计划；独立`prepare_matrix`
接口本身的规则不变。日志新增`candidate_lmax`、`outer_partition_cancelled`、
`execution_path`，阶段名`single_cong`标识这条执行路径。

N=199时候选Lmax=128：K=32仍为128+71，K=33开始走单Cong。
本批K=1/2/3/5/20仍分块，K=100走原Cong；此前历史矩阵结果不重新标记。
对应9组小规模双进程交叉回归及边界、溢出检查见
`KNN-experiment-res/matrix-quarter-cutoff-20261104/README.md`。
原固定分块公式在N=1时产生负指数并拒绝，该限制保留；独立矩阵接口支持单候选。
完整矩阵使用全部i<j、rank初值0；裁剪矩阵使用跨块u+v<=K+1、初值u-1。
200/5仍为128+72、block=680/depth22，full=735/depth24，pruned=705/depth24。
单纯的矩阵末端为full45+10、pruned15+10个比较。

## 可选投票

```cpp
#include "Machines/kona-mask-vote.hpp"
auto winner_share = KonaMaskVote::vote_with_pcr<64>(
        output.candidates, output.mask, public_label_count, player);
```

复用旧 `matrix_topk_vote_with_pcr` 中按公开类别枚举、两次PCR比较判等、
mask×eq及本地求和的原逻辑。新加的最后选择调用已有PCR比较和Kona swap，
相同票数时保留较小公开类别编号。类别必须编码在 `[0,public_label_count)`，mask必须为有效0/1入选位。
实体K项可传公开1的份额mask，无需公开任何秘密mask或标签。
返回值只有一个 `Z2<64>` 类别份额；无reveal。频数接口也可单独调用。
这没有改变原始Kona投票或它的平票规则；也不保证所有旧路径的平票行为相同。
底层仍是历史允许的PCR/乘法L2零材料，不代表完成生产安全预处理或安全审计。

## 唯一实验入口

当前实验只有 `Machines/knn-bench.cpp` 和 `Scripts/run-knn-experiment.py` 两个文件：

```bash
python3 -u Scripts/run-knn-experiment.py --scheme hybrid
```

默认执行Arcene N199、六个k、LAN/WAN各3次的共享前缀分段计划，C++固定绑核0/2。`--scope full`选择真实连续完整KNN。规则与详细统计见 [bench_recording_protocol.md](bench_recording_protocol.md) 的当前入口节。

独立矩阵诊断仍由同一个C++ bench的 `--scheme hybrid --scope topk|terminal` 提供，例如容器内两方分别以 `-p 0` / `-p 1` 启动：

```bash
./knn-bench.x --scheme hybrid --scope topk -p 0 -pn 18000 -n 200 -k 5 \
  -m pruned --comparator hybrid --threshold 10 -r 1
```

`-m full|pruned`仍选择完整/剪裁矩阵，`--comparator hybrid|pcr|dcf`选择后端；`--matrix-backend auto|pcr|dcf`支持末端诊断。此诊断scope使用提前准备好的距离份额，安全投票通过 `--vote-labels` 独立统计，不能当作完整KNN实测。`--scope full`才执行真实距离、Top-k及投票的连续在线流程。

网络配置只由Python组织脚本调用既有统一工具，C++不改qdisc。诊断保留RESULT/VOTE及结构/比较器/字节/轮数；不同scope的时间不得混表。最新汇总时间/字节逐次取双方最大值、轮数保留双方；旧文档的P0统计定义仅适用于历史结果。

## 回归与历史保留

`kona-matrix-plan-test.x`直接调用模块，验证200/5结构、短尾块回退及144个有序二进制块的full/pruned mask一致性。
`Scripts/test-matrix-modules.py`只在容器隔离工作目录运行小规模真实双进程回归，
包括迁移前8例的bytes/rounds/backend/evaluate精确比较、full/pruned、不同后端、
独立terminal、重复值、集中分布、短尾块、k=1、逆序/OCTM/OB-SSN兼容、
数据集fixture、平票及最终类别份额验证。不跑48组或四大数据集。

缺失DCF材料时，可在空的隔离目录运行 `kona-dcf-test-setup.x`；
复用原preprocess中的gen_fake_dcf（已抽成共享header），拒绝覆盖任何已有材料。
该工具不是新的密钥生命周期协议。当前实验仍有材料复用等历史安全限制。

## 本次已执行验证

现有kona-dev构建通过；独立规划144例与32个小规模真实双进程例全部通过。
迁移前8例的结构、bytes、实际通信与后端/evaluate计数逐方精确一致。
另用N=64验证runner的AB/BA、同进程预热排除、P0/P1/max/aggregate、投票独立输出及旧pure模式分组。
完整记录位于 `KNN-experiment-res/matrix-module-refactor-20261103/report.md`。
本轮未跑48组或大数据集，不将小回归时间作为性能提升结论。

### 单文件合并回归

后续按要求把规划与执行合并进 `kona-reduced-depth-cong-topk.hpp`。
`prepare_reduced_plan()`准备公开计划，`prepare_terminal(..., Terminal::Full/Pruned)`选择末端，
`execute_topk()`一次完成分块Cong→候选收集→矩阵排名→秘密mask；
`execute_blocks()`、`collect_block_candidates()`、`execute_terminal()`仍支持分开调用。
投票仍复用独立 `kona-mask-vote.hpp`，由bench单独调用与计时，不混入Top-k主指标。

单文件合并后已重新构建并通过144个规划检查和32个小规模双进程回归；
与合并前64条分方记录的结构、通信、后端/evaluate计数及checksum完全一致。
证据位于 `KNN-experiment-res/matrix-header-merge-20261103/report.md`。
