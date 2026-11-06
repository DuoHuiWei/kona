# 五方案预处理材料：现成实现复用清单

核查日期：2026-11-05。范围：ConstantPP、Shao、SecKNN、Kona、Ours。只做本地代码阅读和官方／作者资料核查，未编译、安装、下载第三方源码或执行生成协议。数量核算暂缓。

## 1. 先给结论

**不用从头写密码学底层。优先复用仓库已带的MP-SPDZ系OT／Semi2k／SemiBin基础。DCF作者实现有位宽限制，尚未找到已经确认适配当前64位比较器的两方生成入口。需要补的主要是专用关联关系的组合、材料格式、角色分发与计量。**

这里“已有可调用组件”表示发现了实现入口，不表示已经在当前环境跑通或能直接输出旧在线模块所读的文件。按当前两服务器无可信第三方的要求，没有发现一套对五方案全部材料都零适配的现成包。

| 材料 | 可以复用什么 | 还得补什么 | 判断 |
|---|---|---|---|
| 64位环算术 MUL 三元组 | 仓库已有 `Semi2kShare<64>`、`SemiPrep::buffer_triples()`、OT三元组生成器 | 独立两方运行上下文、份额导出、指定数量与批次、生成／分发计量 | 底层已有，优先复用本地版本 |
| XOR布尔 AND 三元组 | 仓库已有 `GC::SemiPrep`；外部可选libOTe的二元三元组生成 | 位打包、尾部位、PCR端点映射、导出与计量 | 底层已有，本地优先；libOTe作候选 |
| SecKNN／ConstantPP平方对 | 通用秘密乘法计算共享随机r的平方；仓库已有 `generate_squares()` 组合函数 | 在选定Semi2k上下文实例化、确认所走路径、导出 `[r],[r²]` | 可用现成MPC组合，不写新平方协议 |
| Kona／Ours欧氏关联材料 | 相同Semi2k乘法／平方基础；现有Kona生成器用于核对关系和布局 | 按同一查询掩码生成 `(Rx-Rq)²` 的修正份额，核对接收角色 | 底层可复用，专用组合尚需补 |
| DCF密钥及关联掩码 | 作者 `dealerless-FSS_public` 的分布式DCF生成 | 已查legacy入口限制Bin≤24；64位生成路径、结构及求值兼容仍缺 | 有两方生成，但当前64位需求尚未解决 |
| PCR／RMSB输出位B2A | 现有公式 `b0+b1-2*b0*b1`，配真实MUL三元组 | 新目录中调用及材料标识 | 无需单独再造转换协议 |
| ConstantPP洗牌关联材料 | 本地 `SecureShuffle`／Waksman；现有ConstantPP dealer函数作公式参照 | 两方生成符合当前排列方向和环加法语义的关联份额 | 有工具，专用关联生成还需组合 |
| Shao NHSS密钥／编码／token | 本地C++／GMP实现已有完整函数 | 随机源、生成角色、必要字段分发、大整数编码及计量 | 生成算法已有，角色与分发需补 |

## 2. 最优先复用：仓库已带的MP-SPDZ系组件

以下都是当前仓库已存在的源码，读取和引用即可，不需要另装一套才能确认功能：

| 功能 | 本地依据 |
|---|---|
| 模2^K加法份额与生成器类型绑定 | [Protocols/Semi2kShare.h](../Protocols/Semi2kShare.h)，`Semi2kShare<K>`：`LivePrep=SemiPrep2k`，`TripleGenerator=OTTripleGenerator` |
| 算术三元组准备 | [Protocols/SemiPrep.hpp](../Protocols/SemiPrep.hpp)，`buffer_triples()` 调 `generatePlainTriples()` |
| OT底层三元组批量生成 | [OT/NPartyTripleGenerator.hpp](../OT/NPartyTripleGenerator.hpp)，`OTTripleGenerator::generatePlainTriples()` |
| 两方环预处理 | [Protocols/SemiPrep2k.h](../Protocols/SemiPrep2k.h) |
| 布尔三元组准备 | [GC/SemiPrep.cpp](../GC/SemiPrep.cpp)，`set_protocol()`、`buffer_triples()` |
| 平方的通用组合 | [Protocols/ReplicatedPrep.hpp](../Protocols/ReplicatedPrep.hpp)，`generate_squares()`：随机共享值 → `prepare_mul(r,r)` → `exchange()` → `finalize_mul()` |
| 秘密排列基础 | [Protocols/SecureShuffle.h](../Protocols/SecureShuffle.h)、[Protocols/SecureShuffle.hpp](../Protocols/SecureShuffle.hpp)、[Tools/Waksman.h](../Tools/Waksman.h) |

Semi2k的环域与当前 `Z2<64>` 的模2^64语义匹配；导出时仍要显式取环元素，不能序列化整个框架对象。不能把模素数或GF(2^64)三元组当作模2^64三元组使用。

上游MP-SPDZ官方文档确认Semi2k支持半诚实、非诚实多数的模2^k计算，并提供包含semi2k在内的独立预处理运行方式。**这些是上游能力，本地版本是否具备同名可用构建目标尚未验证**，不直接给出未经核实的启动命令。[官方协议表与预处理说明](https://mp-spdz.readthedocs.io/en/latest/readme.html#preprocessing-as-required)

本地 `generate_squares()` 是可复用组合入口，不代表默认平方缓冲路径一定调用它；例如 `BitPrep::buffer_squares()` 有自己的三元组组合路径。后续需明确实例化与调用路径，测其真实成本。

补充工作集中在新目录内的运行上下文和导出适配。框架可能按内部批大小多生成材料，正式实验需分别记录请求数、实际生成数及剩余数；不能只写一个理想请求数。

## 3. 外部库分别能做什么

### 3.1 libOTe：可直接复用二元三元组协议，不是环三元组的同义词

官方实现提供Foleage及Silent-OT相关的二元Beaver三元组，并有示例／测试入口。它可作为PCR与Rotated-MSB的AND材料供应候选。[libOTe官方仓库](https://github.com/osu-crypto/libOTe)，[Triple源码目录](https://github.com/osu-crypto/libOTe/tree/master/libOTe/Triple)

尚需将输出变为本工程的XOR份额打包布局，并测全部必要初始化。当前官方版本要求C++20，有独立依赖；优先尝试本地 `GC::SemiPrep` 可以减少引入新依赖。**没有据此认定libOTe能直接生成所需的64位环算术三元组。**

### 3.2 Dealerless-FSS：DCF优先候选，但要先验证兼容

作者仓库提供分布式DCF，开发文档列出 `keyGenNewDCF()`／`evalNewDCF()` 等已有入口；依赖EzPC的FSS／SCI代码，属于研究原型。[作者仓库](https://github.com/xingpz2008/dealerless-FSS_public)，[开发接口说明](https://github.com/xingpz2008/dealerless-FSS_public/blob/main/docs/developer-usage.md)，[DCF头文件](https://raw.githubusercontent.com/xingpz2008/dealerless-FSS_public/main/src/legacy/dcf.h)

**补充源码核查后的修正：不是只差文件适配。** `src/legacy/dcf.cpp` 的 `keyGenNewDCF()` 调用 `ensureSupportedFullTreeBits()`，明确限制输入Bin≤24，并分配 `2^Bin` 大小的树缓冲。不能去掉限制后直接拿来生成64位DCF。新路径 `src/fss/idpf.cpp` 也有24位全树限制；`src/buildingblock/comparison.cpp` 的阈值校验要求Bin<32。上述结论针对已查入口，不推断所有其他分支均相同。[legacy DCF源码](https://raw.githubusercontent.com/xingpz2008/dealerless-FSS_public/main/src/legacy/dcf.cpp)，[iDPF源码](https://raw.githubusercontent.com/xingpz2008/dealerless-FSS_public/main/src/fss/idpf.cpp)，[comparison源码](https://raw.githubusercontent.com/xingpz2008/dealerless-FSS_public/main/src/buildingblock/comparison.cpp)

“64位输出”不等于“64位阈值／输入”；小位宽生产结果不能冒充当前64位方案的生成成本，也不修改旧在线位宽来迁就候选库。

下一步只需围绕现有协议做小规模接口验证，重点是两方输入份额、比较方向、输出环与字长、边界／相等行为，以及密钥能否由现有求值器解释。当前Kona密钥文本布局与作者 `newDCFKeyPack` 不同；不能直接将后者落盘后交给旧加载器。两次本地求值与密钥可安全使用的范围也需核对。

若必须配套使用作者求值器，独立材料生产实验可以保留它自己的输出格式，但必须标为该生成构造的成本；在旧比较器兼容性成立前，不能称为旧在线实现的直接匹配预处理。

### 3.3 ConstantPP洗牌：优先复用本地秘密排列；外部OSN仅作备选

当前要求是环加法关联式：

`b0+b1 = π0(π1(a0)+a1)`。

本地可信dealer函数已经实现此关系，但它知道两方秘密，不能直接当作无可信第三方的生成协议。可以研究用本地秘密排列与MPC加法组织相同关联计算；需要核对每方最终知道哪些排列与掩码，不能仅因有 `secure_shuffle` 就判定完全兼容。

外部mpc4j有OSN／ROSN现成实现，可作排列关联的备选参考；它是Java工程，且不能默认其关联输出等同于当前模2^64的加法材料。本轮不引入这套依赖。[mpc4j OSN](https://github.com/alibaba-edu/mpc4j/tree/main/mpc4j-s2pc-aby/src/main/java/edu/alibaba/mpc4j/s2pc/aby/pcg/osn)

## 4. 五个方案分别补到哪里

| 方案 | 优先复用的生产基础 | 特有的补充工作 |
|---|---|---|
| ConstantPP | Semi2k平方／MUL、分布式DCF候选、本地秘密排列 | 洗牌关联生成；平方与乘法新鲜材料组织；保留 `paper-local-l2` 的测试性质标注 |
| Shao | 已有NHSS C++／GMP函数、SemiBin或libOTe AND、Semi2k MUL、分布式DCF候选 | NHSS角色与大整数分发；RMSB位打包；投票DCF材料 |
| SecKNN | Semi2k平方／MUL、分布式DCF候选 | 加掩码平方对的字段与符号适配、比较交换及投票材料组织 |
| Kona | Semi2k乘法／平方、分布式DCF候选 | 训练／查询掩码与平方修正的联合生成、角色分发 |
| Ours | 复用Kona欧氏材料；SemiBin或libOTe AND、Semi2k MUL、分布式DCF候选 | PCR端点打包与位B2A材料；按实际网络与PCM后端组织库存；优化投票所需材料 |

Shao现有入口在 [Machines/kona-nhss-distance.hpp](../Machines/kona-nhss-distance.hpp)：`keygen()`、`share_data()`、`token_gen()`、`share_query()`。这些函数已有实际大整数计算，不重写NHSS算法。

但NHSS数据编码与token依赖输入，应与输入无关预处理分开统计。其密钥／编码原本有输入方角色；不能让两台服务器各自知道完整私钥后称为角色隔离。若坚持将输入方密钥生成也改为两服务器共同生成，**本轮没有确认可直接兼容的阈值NHSS生成库**，仍是待解决项。当前固定seed的GMP随机源也不能直接当作安全随机源。

ConstantPP继续保留 `paper-local-l2` 时，SecMax重构频数的测试行为不会因另外生产了真实三元组或DCF而自动改变。新实验应区分“现有测试实现所需准备”和“完整安全协议所需准备”，不能把前者换个名称填成后者。

## 5. 下一步工作范围

先验证本地Semi2k／SemiBin能在新目录的独立入口生产、导出有效MUL／AND材料；再验证平方和Kona欧氏关联；DCF单独做作者库接口核对；最后处理ConstantPP特殊洗牌与NHSS角色分发。所有新增源码、适配、依赖记录、构建与实验文件均留在本目录。

不重写OT、三元组或DCF密码学底层，不修改旧在线工程，不把加载旧文件测成生成。每类先小规模核对关系与角色可见信息，再决定后续正式测量。当前清单只证明发现了可复用组件或组合路线，尚未证明五方案生产链已经跑通。

## 6. DCF候选进一步筛选

“分布式比较函数”指函数被拆分到两方求值，并不自动表示密钥也由两方交互生成。

| 候选 | 实际发现的入口／能力 | 是否满足当前需要 |
|---|---|---|
| 本地 `gen_fake_dcf()` | 单个生成者掌握整套输入并生成两方材料 | 可作正确性对照，不能算两服务器无可信方生成 |
| Dealerless-FSS legacy | `keyGenNewDCF(party_id, Bin, Bout, idx_share, payload_share)`，含两方交互 | 模型方向匹配；已查入口Bin≤24，不直接满足当前64位输入 |
| Dealerless-FSS新comparison／iDPF | 有两方协议封装 | 已查路径存在小位宽限制；不能仅凭新API名称认定解决64位问题 |
| EzPC／LLAMA普通DCF | `keyGenDCF()` 接收明文idx／payload，一次返回两方密钥 | 普通集中式生成，不是现成两方共同生成 |
| myl7/fss普通DCF | `Gen()` 接收阈值a、载荷b和两方初始seed | 有DCF生成／求值，不是已确认的dealerless生产接口 |
| libOTe | 有DPF分布式生成，以及OT、二元三元组 | DPF不等于DCF，不能直接填补DCF密钥缺口 |
| 通用2PC执行现有DCF Gen | 文献讨论用通用2PC实现dealer | 可继续调查的组合路线；尚未找到已核实可运行、匹配当前64位密钥的现成入口 |
| KORD | 专用芯片及相互认证形成可信硬件边界 | 引入硬件信任，不作为当前普通两服务器软件环境的直接替代 |

候选依据：[EzPC DCF头文件](https://raw.githubusercontent.com/mpc-msri/EzPC/master/FSS/src/dcf.h)、[myl7 DCF源码](https://raw.githubusercontent.com/myl7/fss/main/include/fss/dcf.cuh)、[libOTe官方说明](https://github.com/osu-crypto/libOTe)、[FSS论文附录A](https://www.microsoft.com/en-us/research/wp-content/uploads/2021/01/FSS.pdf)、[KORD论文](https://arxiv.org/html/2608.30379v1)。没有编译或运行这些外部候选；没有据搜索结果声称不存在其他可行实现。

## 7. 更详细的缺失总表

| 项目 | 哪些方案用 | 现有内容 | 能复用的基础 | 仍缺什么 |
|---|---|---|---|---|
| 算术MUL三元组 | 五方案 | 在线乘法、固定／零辅助材料、部分dealer函数 | 本地Semi2k／OT | 两方生产独立入口、导出、分发、计量 |
| 布尔AND三元组 | Shao、Ours | RMSB／PCR打包AND在线路径 | 本地SemiBin；libOTe备选 | 布尔材料导出、活跃位／尾部位映射 |
| 平方关联对 | ConstantPP、SecKNN | 距离公式；ConstantPP有dealer生成 | Semi2k秘密乘法／平方组合 | 独立生产与格式；区分δ−a和δ+r的符号 |
| 欧氏专用关联材料 | Kona、Ours | 本地生成器、旧布局、fake／真实加载器 | Semi2k乘法／平方 | 保持查询掩码跨记录关联的两方生成、角色分发核对 |
| 64位DCF密钥 | 五方案中的DCF环节 | 测试生成、旧文件求值与缓存 | Dealerless-FSS用于小位宽研究验证 | 已确认可扩展的64位两方生产入口，且匹配求值语义 |
| DCF关联掩码 | 同上 | 旧r文件及掩码打开公式 | 两方共享随机数与选定DCF生成协议 | 掩码和隐藏阈值的一致关联、刷新与使用范围 |
| PCR输出位B2A | Ours | b0+b1−2b0b1 | 已有公式＋真实MUL库存 | 调用适配，避免同时计MUL和独立转换材料 |
| RMSB输出位B2A | Shao | 同类位转换 | 已有公式＋真实MUL库存 | 位切片结果到算术份额的格式适配 |
| 通用整字A2B | 按实际路径，不统一要求 | 有公开后重分享测试函数 | 仅在真实调用需要时选安全实现 | 不是当前PCR／RMSB比较路径的必备独立库存 |
| Shuffle排列／掩码关联 | ConstantPP | dealer实现π及a/b关系 | 本地SecureShuffle／Waksman；外部OSN参考 | 两方生成同一关联，保持环域、排列方向和角色可见信息 |
| SecMax特有材料 | ConstantPP | `paper-local-l2`直接重构频数的性能路径 | 复用已存在入口作测试基线 | 明确是测试准备还是完整安全协议准备，补材料不能改变其测试性质 |
| NHSS公私钥 | Shao | C++／GMP `keygen()` | 直接复用算法 | 安全随机源和正确生成角色；若要求阈值两方keygen，尚无已确认兼容实现 |
| NHSS训练编码 | Shao | `share_data()` | 直接复用算法 | 输入方角色、两方DataShare序列化分发；归输入相关准备 |
| NHSS查询token | Shao | `token_gen()`／`share_query()` | 直接复用算法 | 查询方角色、token／平方份额分发；归输入相关准备 |
| 训练／查询／标签份额 | 五方案 | 在线bench本地构造测试输入 | 既有输入分享公式 | 只在后续E2E接入时补真实提交；不混入当前纯材料生产实验 |
| 随机源和独立性 | 五方案 | 固定seed、零值、材料复用并存 | 所选库安全随机机制 | 正式生产配置、每批身份与一次性使用边界 |
| 通用材料编码 | 五方案 | 文本、原始对象、GMP动态结构各不相同 | 已有pack机制作参考 | 显式类型、字长、数量、字节序和版本，禁止发对象内存镜像 |
| 分发及初始化 | 五方案 | 无统一新生产链 | 所选库网络／OT初始化 | 记录初始化是否摊销、每方实际发送，避免重复计算库内交互与外层分发 |
| 加载与库存 | 五方案 | 旧文件和缓存 | 原读取语义作参考 | 生成和加载分开记录、库存不足及重复使用检查 |
| 材料关系核查 | 五方案 | 有部分组件测试 | 小规模测试既有关联式 | 只验证所需关系；不能把公开抽查过的材料继续当秘密库存 |
| 连续E2E交付 | 表3后续 | 原在线结束于标签份额 | 现有在线计算作为后续依赖 | 连续总计时、输入提交、查询方结果接收；纯生产实验本身不填这一列 |

“五方案用MUL／DCF”指各自实际出现这些操作的阶段，不表示相同网络或相同材料数量。公开网络计划、比较索引、类别常量、在线产生的mask不需要另造秘密预处理材料。
