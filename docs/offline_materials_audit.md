# 五方案预处理材料：公式、协议、格式与实现缺口

核查日期：2026-11-04。以当前仓库代码为准，不代表论文完整复现或安全证明。

本文服务于 Arcene（训练 N=199、d=10,000、k=5）表3：离线、在线、连续端到端开销。本文仅整理实现，不修改协议、不生成材料、不运行实验。原有性能实验及其测试材料口径保持不变。

## 1. 先区分四种不同的东西

| 名称 | 例子 | 是否属于秘密预处理材料 |
|---|---|---|
| 关联随机材料 | 平方对、乘法三元组、布尔三元组、DCF密钥与掩码 | 是，需要说明生成者、关联关系、接收者及消费方式 |
| 输入编码 | 数据库秘密份额、查询份额、NHSS数据编码与查询token | 是输入准备／提交；依赖具体输入，不能全归入输入无关离线 |
| 公开执行计划 | Cong层、比较索引、分块边界、矩阵配对、chunk划分 | 不是秘密材料；可计本地规划开销，但应单列 |
| 本地加载／缓存 | 读旧密钥、解析文件、分配vector、构造DCF缓存 | 不是本次密钥生成或网络分发 |

“有生成函数”“当前bench调用了生成函数”“材料经过网络分发”“在线正确消费材料”是四个独立条件。满足前一个不代表满足后几个。

## 2. 通用符号、协议和格式

### 2.1 三种份额

- 算术份额：R=Z/(2^64)，[x]_0+[x]_1=x mod 2^64。代码为 `Z2<64>`；ConstantPP别名为 `Ring`。
- 布尔份额：<x>_0 XOR <x>_1=x。可为单比特，或打包在 `uint64_t` 中；每一位是独立布尔通道。
- NHSS减法份额：x_1-x_0=x。部分关系在整数上成立，平方／最终距离在2^64环上使用；不能直接当作算术加法份额。

在线环元素通过既有 `pack/unpack` 和 `octetStream` 传输，当前实验按每个64位环元素8 B计载荷。内存中的C++结构大小、历史文件大小、实际网络字节是不同指标。

### 2.2 平方关联对：c=a²

材料为 ([a]_p,[c]_p)，两方重构满足 c=a²。可信dealer可采样a，计算c，再独立随机分享a和c。

计算秘密差值δ的平方：

1. 本地得到 [e]_p=[δ]_p-[a]_p。
2. 交换并公开 e=δ-a。
3. 本地输出 [δ²]_p=[c]_p+2e[a]_p+ι_p e²，其中ι_0+ι_1=1。

所有独立平方可以并行公开e。对于一般安全使用，须为不同秘密操作配置合适的独立材料，不能仅凭c=a²成立就宣称反复使用安全；同一a被复用时，公开e的差会暴露δ的差。

当前ConstantPP结构 `SecEdPartyMaterial { Ring a_share; Ring c_share; }` 每方只有两个环元素，即16 B裸字段；没有已经统一落地的离线分发消息格式。当前接口复用同一对，不能把16 B误当成完整安全距离预处理所需总量。

### 2.3 算术Beaver三元组：c=ab

材料为 ([a]_p,[b]_p,[c]_p)，满足 c=ab。在线公开 e=x-a、f=y-b，输出：

`[xy]_p = [c]_p + e[b]_p + f[a]_p + ι_p e f`。

单个乘法每方材料裸字段为3×8=24 B；公开e/f每方发送16 B。独立乘法可一批交换。批处理减少轮次，不自动减少所需独立三元组数量。

ConstantPP已有 `BeaverTripleParty` 和可信dealer生成函数；其他L2乘法多把a/b/c直接写成0，没有材料输入参数及消费游标。要使用生成材料，需要接入这些接口，而不是只运行一个生成bench。

### 2.4 布尔Beaver三元组：C=A AND B

在XOR份额中采样A/B，形成C=A AND B并分享。在线公开 D=X XOR A、E=Y XOR B，输出：

`<X AND Y>_p = <C>_p XOR (D AND <B>_p) XOR (E AND <A>_p) XOR ι_p(D AND E)`。

打包实现可把64个逻辑AND通道放入一个字。完整64位三元组每方裸字段24 B；PCR部分层只保留活跃端点比特，不能机械按完整字数量估算最小分发字节。

当前PCR／Rotated-MSB调用零三元组版本：交换X/Y的XOR份额并计算重构后的AND。字节和运算存在，但输入被公开，不能作为有隐私的三元组消费实现。

### 2.5 DCF密钥及关联掩码

DCF把带阈值α和输出系数β的比较函数拆成两方求值密钥。这里的抽象功能可写成 `Eval(k0,t)+Eval(k1,t)=β·1{t<α}`；具体大于／小于、符号修正与相等规则，以当前比较器实现为准。仓库比较器对掩码差值及其2^63平移各求值一次，组合为64位比较结果，不能把抽象式直接替代其符号处理。

当前共享后端：`Machines/kona-dcf-compare.hpp::Compare64<64>`，读取两方各自的k/r文件，并缓存求值。旧SecKNN还保留逐次求值读取文件的实现。

现有测试生成器：`Machines/kona-dcf-test-material.hpp::gen_fake_dcf(beta,n)`。n=64时：

| 文件 | 当前内容与顺序 |
|---|---|
| `Player-Data/k0`、`k1` | 空格分隔的十进制大整数：初始seed；63层，每层scw、vcw、tcw0、tcw1；末尾cw。每文件254个整数token |
| `Player-Data/r0`、`r1` | α的两方加法份额，生成时两份和为α；加载进64位环时按该环解释 |
| `Player-Data/r2` | 生成器还写出与r0相同的值；不能据此给普通在线服务器额外分发双方秘密 |

以上是文本格式，不是固定大小二进制密钥包；seed转换、截断和节点解释须保持与现有加载器一致。文本字节数不能按“254×8”代替。生成器输出在 `Player-Data/` 根目录；加载器优先找 `Player-Data/2-fss/` 再找根目录。`kona-dcf-test-setup.cpp`检查目录与生成函数实际输出目录不完全一致，接入前需明确输出路径，避免读取旧文件。

当前性能运行复用既有测试密钥，尚未建立按操作的独立密钥／掩码库存、分发格式和生命周期。一次生成耗时不能直接乘比较数作为实测离线成本；密钥复用策略也不能直接当作完整安全策略。

### 2.6 份额转换不只是生成三元组

`Machines/kona-share-conversion.hpp` 中部分 `*_l2` 转换明确执行“公开原份额的值→重新分享”，例如 `additive_to_xor_batch_l2`、`xor_to_additive_batch_l2`。这不是带随机掩码的安全转换。

若要有隐私的A2B/B2A，需要选定并接入相应协议，例如使用关联算术／布尔随机比特或安全加法电路；这只是可选实现方向，仓库尚未提供可直接替换的完整材料供应路径。不能把某一种候选材料的大小当成当前方案已测量的数据。

## 3. ConstantPP：逐阶段材料清单

主要文件：`ConstantPP/constantpp-types.hpp`、`sec-ed.cpp`、`sec-ed-batch.cpp`、`sec-shuffle.cpp`、`sec-mul.cpp`、`sec-max.cpp`、`secmax-local-fss-l2.hpp`，实际组织入口为 `Machines/knn-bench.cpp::constantpp_engine`。

### 3.1 距离：平方对

算法为第2.2节，累加所有坐标得到平方欧氏距离；`sec_ed_batch_n_dim`把Nd个e一次交换，在线每方8Nd B、1个逻辑轮次。

- 已有生产：`dealer_generate_sec_ed_material(PRNG&)`，采样a、计算a²并随机拆分。
- 格式：`SecEdDealerMaterial.party[0/1]`，每方两个Ring。
- 当前bench：P0取a0=3、c0=20；P1取a1=4、c1=29，因此a=7、c=49。不是每次随机dealer生成。
- 当前兼容接口在所有坐标复用同一平方对；代码注释明确不等同于更强的逐坐标预处理版本。
- 缺失：材料分发、按操作分配与消费；若追求安全独立材料，还需调整当前仅接收一个平方对的接口。

### 3.2 Shuffle：秘密排列＋相关掩码向量

每方结构：`ShufflePartyMaterial { permutation; a_share[column][row]; b_share[column][row]; }`。距离、标签两列共用本方排列，每列有各自掩码。

已有 `dealer_generate_shuffle_material(n,num_columns,prng)`：生成π0、π1及各方a，按代码的置换方向建立：

`b0+b1 = π0(π1(a0)+a1)`。

这是该shuffle需要的关联关系，不是普通ab三元组。在线P0发送被a0掩码的列，P1施加π1及a1后返回，P0再施加π0及b0修正；另一方保留对应b1份额。具体符号以 `sec_shuffle_columns` 为准。

当前bench使用恒等排列及全零a/b。生成器已经存在，但未接入当前正式入口。每方两列时有4N个Ring和N个排列索引；后者内存类型为 `size_t`，网络应先确定固定宽度编码，不能直接以sizeof(struct)计分发量。当前没有统一分发编码。

### 3.3 排名、阈值、频数：DCF材料

排名只比较上三角，共N(N−1)/2对；Arcene为19,701对。阈值另比较N=199次。`SecBComUpperTrianglePlan`仅保存公开(i,j)，不是DCF预处理密钥。

频数模块也使用既有DCF比较路径；目前共用测试密钥后端。需要按实际调用区分排名／阈值／频数材料，不能因为公开计划文件叫material就将其当作秘密材料库存。

### 3.4 SecMax与末尾标签选择

- 普通乘法生成器已有：`dealer_generate_beaver_triple(PRNG&)`，满足c=ab并随机分享。
- 消费接口已有 `sec_mul_vector_reuse` 和 `sec_mul_vector_fresh`；当前SecMax调用reuse版，复用同一个三元组。
- 当前bench三元组：P0为(2,3,11)，P1为(3,4,24)，重构(5,7,35)。关系成立，但为固定且复用的测试材料。
- `paper-local-l2`将k个频数直接交换重构，再本地用DCF缓存求排名；加载r0、r1得到公开α。它有真实k×8 B/方交换，但没有论文逐比较独立掩码／密钥预处理。
- 因此继续保留 `paper-local-l2` 时只能如实测“该测试实现的材料准备”。补一次随机三元组生成不会把整个SecMax变为安全论文实现。

## 4. SecKNN：逐阶段材料清单

主要在线距离：`Machines/secknn.cpp::KNN_party_SecKNN::compute_ESD_for_one_query`；当前入口为 `knn-bench.cpp::secknn_engine`。

### 4.1 距离：加掩码平方对

这里用加号版本：公开z=δ+r，准备[r]和[s]，其中s=r²，输出：

`[δ²]_p = -2z[r]_p + [s]_p + ι_p z²`。

与ConstantPP的δ−a版本只差掩码符号，不能混写纠正项。全部Nd个z一次打包交换，单方8Nd B、1个逻辑轮次。

当前函数把本地r和r_square直接初始化为0，不接收随机材料。需要补的是平方关联材料的输入、按坐标消费及分发；不能只给0换一个随机常数，否则平方关系或隐私仍可能不成立。

### 4.2 Top-k与投票

按层批量比较仍使用原SecKNN DCF求值逻辑；secret swap和投票频数相等性组合调用原算术乘法，a=b=c=0。材料类型为：DCF密钥／掩码、算术Beaver三元组。

两列swap常用一次选择位乘两列差值；当前为兼容原实现还可能计算重复方向项。库存须按真实乘法调用／向量长度统计，不能只按“一个比较交换一个三元组”估计。

目前无统一离线库存结构及消费接口，DCF文件复用旧材料。需要接入第2节材料，维持既有层级和在线消息边界；安全独立性不能只由最终答案正确推出。

## 5. Kona：距离专用相关材料，与普通三元组分开

当前性能路径为批量DCF KONA；`Machines/knn-bench.cpp`包含的基线路径依赖 `Machines/kona-upstream.cpp`。不要把下述距离专用材料误称为通用(a,b,ab)三元组。

### 5.1 ABY2形式的输入与平方修正

设查询掩码为Rq=Rq0+Rq1，训练坐标掩码为Rx=Rx0+Rx1。预处理生成：

`T0+T1=(Rq−Rx)²`。

在当前实现的“公开掩码值、私有掩码份额”表示下，δ=x−q，公开差值为u=δ+(Rx−Rq)，本方掩码差份额为r_p=Rx_p−Rq_p。注意修正平方使用(Rq−Rx)²或(Rx−Rq)²相同，但交叉项的符号不能换。在线本地计算：

`[δ²]_p = -2u r_p + T_p + ι_p u²`。

因此距离计算本身可零服务器间在线通信；代价被移到相关材料和输入分享。不能由距离在线0 B推断准备与输入提交也0 B。

### 5.2 已有生产、磁盘格式及当前消费

`kona-preprocess-bench.cpp::generate_triples_save_file_optimized`已生成上述相关平方修正，输出二进制文件：

| 文件 | 按写入代码的顺序 |
|---|---|
| `P0-Train-Triples` | N×d个训练掩码份额Rx0，逐行逐坐标 |
| `P1-Train-Triples` | N×d个训练掩码份额Rx1，同顺序 |
| `P0-Test-Triples` | 先d个Rq0，再N×d个T0 |
| `P1-Test-Triples` | 先d个Rq0，再d个Rq1，再N×d个T1 |

路径为 `Player-Data/Knn-Data/<dataset>-data/`。文件使用 `reinterpret_cast` 写入 `sizeof(Z2<K>)` 的原始对象字节，不是自描述、跨平台网络格式。P1文件额外写入Rq0是实际代码行为；其知情范围和读取契约需核对，不能擅自删掉或按对称布局解释。

现有 `fake_load_triples()`的实际读取代码被注释，主要分配零数组；另有 `load_triples()`保留实际文件读取实现，布局包含P1查询文件的两段前缀。性能入口调用的是fake路径，并用 `prepare_local_aby2_inputs()`在本地准备零辅助材料输入，没有启用该随机文件加载链。

因此必须核对生成布局、双方应持有的信息、加载器、ABY2输入提交这四者后才能接通。不能只统计四个文件生成时间便称为完整离线及E2E成本。

### 5.3 比较交换与投票

需要DCF密钥／掩码和算术乘法三元组。当前批量DCF缓存旧密钥，swap／频数组合等仍使用测试乘法材料。距离预处理文件不包含这些材料，也不能替代它们。

## 6. Ours：Kona距离＋分块Cong／剪裁矩阵＋mask投票

主要模块：`kona-reduced-depth-cong-topk.hpp`、`kona-reduced-depth-hybrid-adapter.hpp`、`kona-pcr-compare.hpp`、`kona-share-conversion.hpp`、`kona-mask-vote.hpp`。

| 环节 | 所需秘密材料／协议 | 当前状态 |
|---|---|---|
| 距离 | 第5节Kona相关平方材料及ABY2输入准备 | 零辅助材料测试输入 |
| 小层DCF | 第2.5节密钥与掩码 | 加载并复用测试密钥 |
| 大层PCR | 打包布尔AND三元组；实际涉及的A2B/B2A协议 | AND为零三元组，部分转换是公开再分享 |
| Cong secret swap | 算术乘法三元组 | L2零三元组 |
| 剪裁矩阵比较 | 按实际后端选择DCF或PCR所需材料 | 随当前比较器分支复用测试实现 |
| 计数／秘密mask | DCF比较、算术乘法三元组，用mask屏蔽未选候选 | 当前优化后的批量DCF＋L2乘法 |
| 获胜类别选择 | 批量DCF及实际秘密选择所用乘法材料 | 同上 |

公开类别计数可表示为 `count_c=Σ_i mask_i·1{label_i=c}`；当前优化利用CDF边界比较减少比较数，详细流程见 `hybrid_vote_baseline.md`。mask是在线选择输出，不是应由dealer预先知道的候选选择明文；预处理负责提供其后乘法所需独立材料。

分块数、候选数、每层宽度、矩阵模式以及比较器阈值决定材料消耗。应遍历实际计划产生材料需求清单；不能用N²统一替代。公开网络规划不应计作秘密材料分发。

本方案缺口至少包括：Kona距离材料接入、布尔三元组供应、转换协议接入、算术三元组供应、DCF材料生命周期。不是仅缺一个“总预处理函数”。

## 7. Shao：NHSS编码材料＋Rotated-MSB网络＋DCF投票

主要文件：`kona-nhss-distance.hpp`、`kona-nhss-cong-pipeline.hpp`、`kona-cong-rotated-msb-adapter.hpp`、`kona-rotated-msb-compare.hpp`。当前网络所有层均为Rotated-MSB，不采用WAN6000的混合切换阈值。

### 7.1 NHSS密钥：与输入无关

以下大写M表示NHSS模数，避免与训练条数N混淆；代码成员仍命名 `PublicKey::N`。

`keygen(lambda,seed)`生成lambda位素数M，采样g'∈Z_M*：

`g=(g')^(M−1) mod M²`，采样e∈Z_M*，`pk=g^e mod M²`。

内存格式：`PublicKey { mpz_class N,N2,g,pk; }`、`SecretKey { mpz_class e; }`。已有实际大整数生成操作。当前bench使用固定seed、可复现GMP随机数，且两方各自调用keygen；这不是独立dealer只向各方分发必要材料的过程，也不能宣称服务器只知道各自应持有的秘密。

### 7.2 数据库编码：依赖训练输入

每个坐标x的两方 `DataShare`：

- `uint64_t x_share`，整数关系x1−x0=x；
- `mpz_class s_share`，整数关系s1−s0=e·x；
- `uint64_t x2_share`，环上关系x2_1−x2_0=x²。

生成函数为 `share_data(x,sec,rng)`。每条记录还带一个加法共享标签。训练N条、d维时共Nd个DataShare/方；这些属于数据编码／提交，不是与输入无关的随机三元组。

### 7.3 查询token：依赖查询输入

对查询坐标y采样r，生成：

`a=g^r mod M²`，`b=pk^r·(1+M)^y mod M²`。

内存格式：`Token { mpz_class a,b; }`；`QueryShare { uint64_t y2_share; Token token; }`，其中y2_1−y2_0=y²，双方持有同一token。由 `token_gen/share_query`生成。

在线服务器本地计算 `h_p=b^(x_p)·a^(−s_p) mod M²`，经当前DDlog取得乘积份额，再算 `δ_p=x2_p+y2_p−2z_p`；转换到加法份额时P0取负、P1取正。Nd个坐标求和，无服务器间距离在线消息。

现有struct含GMP动态对象，不能直接发sizeof(struct)。尚需定义大整数的长度前缀、符号、字节序、字段顺序，再实际序列化和发送。当前独立本地构造编码的开销不能当作已测NHSS分发通信；编码字节也不能按每坐标8 B估计。

### 7.4 后续安全计算材料

Rotated-MSB复用PCR的打包零三元组AND助手；输出适配及swap涉及L2转换／算术乘法。投票使用现有批量DCF类别计数和最大值路径。NHSS密钥不能替代这些布尔三元组、转换材料、算术三元组或DCF密钥。

因此即使NHSS生成／编码已执行，Shao整个分类器的离线协议仍未补齐。

## 8. 生成能力的准确状态汇总

| 材料 | 是否已有生产代码 | 是否已接入当前完整性能bench的生成与网络分发 |
|---|---|---|
| ConstantPP平方对 | 有，可信dealer函数 | 否，使用固定材料 |
| ConstantPP shuffle | 有，可信dealer函数 | 否，使用恒等／零材料 |
| 算术Beaver三元组 | 有，ConstantPP可信dealer函数；另有暴露a/b的测试生成bench | 否；不能把测试生成bench当安全两方预处理 |
| DCF密钥／掩码 | 有，历史测试生成器 | 否，当前复用磁盘密钥；生命周期未接入 |
| Kona距离相关平方材料 | 有，本地生成并落盘 | 否，当前活动路径使用fake加载／零材料 |
| SecKNN距离平方材料 | 可借鉴平方对生成；原距离函数没有材料输入 | 否，需要适配消费接口 |
| PCR／RMSB布尔三元组 | 已有零三元组在线形状；本次未发现与当前消费接口接通的随机生产路径 | 否 |
| 安全份额转换材料／协议 | 当前主要为L2公开重分享助手 | 否，需要选定并实现协议，不能只补随机数 |
| NHSS密钥、数据编码、查询token | 有，当前完整Shao路径实际生成 | 本地生成有；分发与隔离角色的完整路径没有 |

可信dealer知道全套材料；这是一个明确的信任模型，不等于两个服务器无可信方的安全预处理。仓库通用MPC基础设施存在不代表已接入这些定制模块。若采用dealer，应独立记录dealer生成耗时、dealer发送和两服务器接收；不能把dealer本地生成无网络误记为“分发0 B”。

## 9. 表3接入与测量规范

### 9.1 三个计时边界

总计时器起点：本次材料生产开始。终点：查询方收到并重构预测标签。全过程连续，不通过三个阶段统计值相加代替。

内部依次记录：

1. 材料生成、序列化、实际分发、服务器加载（分别留子指标）。
2. 输入编码与提交，包括训练／查询／标签的必要分享或NHSS token生成分发。
3. 在线距离、Top-k、投票，结束于标签份额。
4. 标签份额交付给指定查询方、查询方重构。

同时记录总E2E，注明进程启动、建连、读数据、公开规划是否位于总范围。若这些预先就绪，应明确前置；不能遗漏其中某段后仍称完整流程实测。

### 9.2 表列与补充字段

表3保留用户指定六列。每方的离线／在线发送量单独记录后取较大值；若dealer／输入方／查询方独立存在，补充其发送／接收字节及方向，避免服务器发送列隐藏分发成本。网络使用同一WAN配置，不能默认只有P0↔P1链路受限而其他角色无限带宽。

完整时间优先由覆盖角色间因果链的总计时器测量；若采用多个角色，各方本地时钟不能未经同步直接相减。输入提交及结果交付在E2E内，但不应偷偷挪入“在线距离＋Top-k＋投票”阶段。

材料缓存需注明一次性、每查询、每数据库或跨查询可复用；首次生成成本与摊销成本分列。不能给某方案免费复用数据库编码，却让另一方案每次重做全部准备后直接声称算法优劣。

### 9.3 每次材料生成应保存的清单（待实现接口，不是现有格式）

建议清单字段：scheme、run_id、N/d/k、类别数、比较器分支、材料类型、满足的关系、生成角色、接收角色、元素数、环／模数位宽、序列化版本、实际载荷字节、生成／分发／加载耗时、一次性消费范围、是否固定seed／零材料／复用。清单不得包含密钥明文。

材料数据包需显式字段编码，不能发送带指针／vector／GMP对象的原始C++struct。安全库存需要按实际操作消费并检测耗尽；不能通过循环复用同一条三元组来掩盖数量不足。

### 9.4 当前可填写与不可填写

- 现有数据可继续填已测在线时间和通信，维持原测试实现标注。
- 当前setup可另测，但名称应为“测试材料／缓存／计划准备”，不能代替安全离线生成与分发。
- 当前尚无五方案同口径、完整连续E2E结果；不填推算值冒充实测。
- 若坚持ConstantPP `paper-local-l2`及其他零材料路径，整张表应明确为这些测试实现的准备／在线／交付对比；不能改标题后视为完整安全协议离线成本。

## 10. 后续实现顺序

1. 先确定表3采用测试实现成本还是完整安全预处理成本，以及是否允许可信dealer。
2. 从上述公式构建每个方案实际材料需求表，将公开计划与秘密材料分开。
3. 优先接已有ConstantPP dealer函数、Kona距离相关生成器、NHSS生成器；同时核对在线消费和知情范围。
4. 补分发编码、实际通信、输入提交与结果交付。
5. 对布尔三元组、安全转换、DCF密钥生命周期等缺失协议单独实现和验证，不能用本地随机数生成替代。
6. 在现有一个C++ bench、一个组织脚本中增加连续计时范围；先小规模检查关系、材料消费和结果，再跑Arcene正式三次。

上述步骤是待办，不表示已获准更改在线算法或启动新批次。本文不覆盖／重写旧结果。

## 11. 源码核对入口

| 内容 | 文件 |
|---|---|
| 五方案实际实验组织、固定材料构造 | [knn-bench.cpp](/home/u7231/kona-work/Kona/Machines/knn-bench.cpp) |
| ConstantPP材料结构 | [constantpp-types.hpp](/home/u7231/kona-work/Kona/ConstantPP/constantpp-types.hpp) |
| 平方材料生产／批量消费 | [sec-ed.cpp](/home/u7231/kona-work/Kona/ConstantPP/sec-ed.cpp)、[sec-ed-batch.cpp](/home/u7231/kona-work/Kona/ConstantPP/sec-ed-batch.cpp) |
| Shuffle生产与消费 | [sec-shuffle.cpp](/home/u7231/kona-work/Kona/ConstantPP/sec-shuffle.cpp) |
| 算术三元组生产、fresh／reuse消费 | [sec-mul.cpp](/home/u7231/kona-work/Kona/ConstantPP/sec-mul.cpp) |
| ConstantPP测试SecMax | [secmax-local-fss-l2.hpp](/home/u7231/kona-work/Kona/ConstantPP/secmax-local-fss-l2.hpp) |
| DCF测试密钥生产／加载求值 | [kona-dcf-test-material.hpp](/home/u7231/kona-work/Kona/Machines/kona-dcf-test-material.hpp)、[kona-dcf-compare.hpp](/home/u7231/kona-work/Kona/Machines/kona-dcf-compare.hpp) |
| SecKNN距离、swap及投票原函数 | [secknn.cpp](/home/u7231/kona-work/Kona/Machines/secknn.cpp) |
| Kona距离、输入分享及两种加载器 | [kona-upstream.cpp](/home/u7231/kona-work/Kona/Machines/kona-upstream.cpp) |
| Kona专用材料生成与文件布局 | [kona-preprocess-bench.cpp](/home/u7231/kona-work/Kona/Machines/kona-preprocess-bench.cpp) |
| PCR布尔AND／L2转换 | [kona-pcr-compare.hpp](/home/u7231/kona-work/Kona/Machines/kona-pcr-compare.hpp)、[kona-share-conversion.hpp](/home/u7231/kona-work/Kona/Machines/kona-share-conversion.hpp) |
| NHSS全部字段与算法 | [kona-nhss-distance.hpp](/home/u7231/kona-work/Kona/Machines/kona-nhss-distance.hpp) |
| Shao Rotated-MSB | [kona-rotated-msb-compare.hpp](/home/u7231/kona-work/Kona/Machines/kona-rotated-msb-compare.hpp) |
| Ours投票 | [kona-mask-vote.hpp](/home/u7231/kona-work/Kona/Machines/kona-mask-vote.hpp)、[投票基准文档](/home/u7231/kona-work/Kona/docs/hybrid_vote_baseline.md) |
