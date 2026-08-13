## I. 引言

万物互联的蓬勃发展，使得时序数据广泛融合到日常生活的各个方面，包括电子医疗、基于位置的服务以及智能制造等。反过来，开展高质量的时序分析对于推进万物互联的智能化已变得至关重要。例如，通过揭示时间序列内部的模式和关系，测量动态时间规整（dynamic time warping, DTW）距离为模式识别、异常检测和个性化推荐等各种任务提供了关键支持。同时，在实际的万物互联应用中，终端设备通常会生成或采集时序数据，然后将其外包给云服务器并进行融合，以实现更有效的分析。

然而，尽管数据融合显著增强了分析的实用价值，但它也引发了隐私顾虑，因为时序数据可能包含敏感信息（例如远程医疗监控中的个人生物特征数据），且云服务器并非完全可信。为了缓解这些隐私问题，许多研究已被提出，用以实现对加密数据进行时间序列分析。遗憾的是，由于严重的性能问题，现有的方案缺乏实用性。尽管最近的工作在安全时间序列分析的性能上相比早期方案有所提高，但它采用了同态加密来加密时间序列，这限制了其在协议设计中的灵活性，并阻碍了进一步的优化，导致性能问题依然存在。

在这项工作中，我们希望提出新的安全时间序列分析方案，在不牺牲安全性的前提下，通过显著优化计算和通信来解决性能瓶颈。然而，要实现大幅度的优化并同时在计算和通信两方面取得突破，绝非易事。这是因为：

1. 先前的研究已经在提高安全时间序列分析的性能方面做出了诸多努力，几乎耗尽了大多数传统方法，使得进一步的改进面临挑战；
2. 现有的安全计算技术通常要么在计算效率上表现出色，要么在通信性能上表现优异，但要同时取得突破依然困难重重。

为了克服上述挑战，我们开辟了一条新的技术路线图，该路线图涉及策略性地选择合适的密码学原语，并利用混合模型（mixed model）的思想来设计安全时间序列分析方案。具体而言，我们首先将我们目标的时间序列分析——即 DTW 相似度测量——分解为三个关键组件，包括欧氏距离计算、寻找三个元素中的最小值以及 Top-k 选择。然后，我们为这些组件设计了安全协议，旨在保持安全性的同时提高性能。最后，通过集成这些协议，我们构建了用于 DTW 分析的安全且高效的方案。

具体而言：

1. 对于欧氏距离计算，虽然最近的工作优化了通信开销，尤其是通信轮数，但它在每个维度上仍需要 4 轮交互。于是产生了一个问题：是否可能在确保高计算效率的同时，设计出一种非交互式（non-interactive）的安全欧氏距离协议？通过分析欧氏距离计算的特性，我们发现，定制同态秘密共享（homomorphic secret sharing, HSS）使其适配减法秘密共享（subtractive secret sharing），可以完美回答上述问题；
2. 关于计算三元组中的最小值，现有的安全协议完全依赖于单一的密码学原语，这限制了它们优化整体性能的潜力。在这项工作中，我们提出了一种精心集成的方法，以无缝的方式结合了多种密码学原语。通过利用这些原语的优势互补，我们的解决方案实现了计算和通信两方面的提升；
3. 对于 Top-k 选择，现有的安全解决方案往往利用基于最小堆的算法，其比较复杂度为 $O(n \log k)$（其中 n 是所有元素的数量）。我们的解决方案通过引入一种基于树（tree-based）的算法，可以将该复杂度优化至 $O(n)$。此外，我们提出了一个旋转（rotation）概念，并设计了一种基于旋转的混合比较模式，从而将比较复杂度进一步降低到次线性（sublinear）级别。此外，为了增强现有安全 Top-k 算法的安全性，我们使其能够抵抗具有背景知识的对手，同时通过集成我们基于旋转的混合比较模式来提高性能。

综上所述，我们工作的主要贡献如下：

- **第一**，我们通过定制同态秘密共享并将其与减法秘密共享相结合，设计了一种非交互式安全欧氏距离（NISED）协议，在完全消除通信开销的同时，提高了约 22 倍的计算效率。
- **第二**，我们提出了一种安全三元组极小值（STM）协议，该协议能够高效地获取三个元素中的最小值。具体而言，我们引入了一种新颖的转换方法，实现了不同原语的无缝集成，从而同时降低了计算和通信成本。
- **第三**，我们充分利用了基于树的 Top-k 算法的特性，巧妙地引入了旋转的概念，设计了基于旋转的混合比较模式，并最终提出了我们的共享数据快速 Top-k（FTKS）协议，将比较复杂度从 $O(n \log k)$ 降低到次线性，从而带来了 1400 倍的计算性能提升和 78x 的通信量减少。此外，我们通过将基于旋转的混合比较模式集成到分治合并结构（divide-and-merge structure）中，设计了一种高效的不透光（oblivious）Top-k 算法，促进了性能的提升。
- **最后**，我们提出了一种具有极佳性能的实用安全时序分析（PSTSA）方案，以及一种考虑了更强对手的安全增强型时序分析（SETSA）方案。我们进行了形式化安全分析，以确认我们提出的方案能够达到所设计的安全要求。同时，开展了广泛的实验以证明我们的方案显著优于最近的工作，在计算上实现了高达 10 倍的提升，在通信量上节省了约 8.7 倍。

本文的结构组织如下：第二节对模型进行形式化；第三节介绍方案中采用的基础知识；第四节引入设计的构建块（building blocks）；第五节提出具体方案；第六节和第七节分别给出安全分析和性能评估；第八节讨论相关工作后，第九节给出结论。





## II. 模型

本节对本研究的系统模型和安全模型进行形式化定义。

### A. 系统模型

在本研究中，我们考虑一个典型的基于云的场景，它包含四个关键实体：一个组织 O、一系列数据提供方 $\mathcal{P}=\{p_{1},p_{2},...,p_{n}\}$、云服务器 $\{\mathcal{S}_{0},\mathcal{S}_{1}\}$ 以及多个分析人员 $A=\{a_{1},a_{2},\cdot\cdot\cdot\}$。

- **组织 O**：该组织是系统的发起者，旨在收集多源时序数据。例如，某家医院可能希望监测和分析个人的医疗数据，以提供合适的远程治疗。然而，由于信息基础设施和技术的限制，组织 O 往往倾向于借助云计算，让数据提供方将他们的数据上报给云服务器，进而由云服务器向分析人员提供数据服务。

- **数据提供方 P**：数据提供方 $p_{i}\in\mathcal{P}$ 负责收集时序数据，并将三元组 $(id_{i},t_{j},x_{j})$ 上报给云服务器。在此， $id_{i}$（其中 $i\in [1, n]$）是数据提供方的唯一标识符，$t_{j}$ 表示时间点，而 $x_{j}$ 是在时间点 $t_{j}$ 处的感知值。在此之前，数据提供方 $p_{i}$ 应该先向组织 O 进行注册，并获得必要的密钥授权。

- **云服务器 $\{\mathcal{S}_{0},\mathcal{S}_{1}\}$**：受安全两方计算（secure two-party computation）的启发，我们的系统中部署了两台云服务器 $\{\mathcal{S}_{0},\mathcal{S}_{1}\}$。在这里，我们假设两台云服务器在存储和计算方面都非常强大。通常，它们应当承担以下任务：

  1. 接收来自数据提供方的上报时序数据并进行存储；

  2. 在获得分析人员的查询请求后，协同执行时序分析；

  3. 将查询结果返回给相应的分析人员。

     值得注意的是，先前关于安全时序分析的研究同样采用了两台云服务器，以确保功能性的同时兼顾高效率。

- **分析人员 $A=\{a_{1},a_{2},\cdot\cdot\cdot\}$**：当分析人员想要享受时序分析服务时，她/他首先应当向组织 O 进行注册。然后，分析人员可以提交查询请求，以识别存储在云服务器上且呈现特定特征的目标时间序列。

所提出系统的整体工作流程如下：

当数据提供方或分析人员加入系统时，他们必须首先向组织注册，组织随后会发放必要的授权密钥。由于分析人员的密钥与本文提出的方案无关，因此这一过程已在系统模型图中省略。一旦获得授权，数据提供方就可以将感知的时间序列上报给云服务器。同时，当分析人员希望获取时序分析结果时，他们可以向云服务器发送查询请求，云服务器将执行本文提出的方案并将查询结果返回给相应的分析人员。

### B. 安全模型

由于组织 O 是整个系统的发起者，因此通常被认为是可信的。对于数据提供方 P 和分析人员 A，由于已注册的成员没有动机偏离提出的方案，因此他们是诚实的，也就是说，他们会诚实地执行方案。

根据安全云计算中的标准假设，云服务器 $\{\mathcal{S}_{0},\mathcal{S}_{1}\}$ 被认为是半诚实的（semi-honest，又称诚实但好奇），这意味着它们会忠实地执行提出的方案，但会试图从上报的时间序列和查询请求中推窥敏感信息。在不失一般性的情况下，我们假设 $\mathcal{S}_{0}$ 和 $\mathcal{S}_{1}$ 是互不合谋（non-collusive）的，且系统中的任何两个实体之间都不会相互合谋。需要指出的是，本研究聚焦于安全计算，主动攻击（例如恶意对手）超出了本文的研究范围。





## III. 准备工作

### 

在本节中，我们首先介绍一种基于**动态时间规整（Dynamic Time Warping, DTW）\**的代表性时间序列分析算法。然后，我们介绍了减法秘密共享（subtractive secret sharing）和\**同态秘密共享（Homomorphic Secret Sharing, HSS）**，它们在我们提出的方案中作为基础的密码学原语（cryptographic primitives）。

### A. DTW 与我们的时间序列分析

**动态时间规整（DTW）[5]** 是时间序列分析中一种强大的算法，用于测量两个时间序列之间的相似度，即使它们没有对齐（不同步）。由于它在处理非线性畸变以及对时间变化具有鲁棒性等关键特性，已被广泛应用于许多领域，如生物信息学、金融市场和语音识别 [5], [6], [7]。假设有两个时间序列 $\mathbf{x} = \{(t_1, x_1), (t_2, x_2), \dots, (t_\alpha, x_\alpha)\}$ 和

 $\mathbf{y} =$

$\{(t_1, y_1), (t_2, y_2), \dots, (t_\beta, y_\beta)\}$，其中 $\alpha$ 和 $\beta$ 分别表示 $\mathbf{x}$ 和 $\mathbf{y}$ 的长度。DTW 距离定义为 $D_{\text{DTW}}(\mathbf{x}, \mathbf{y})$，其计算过程如下：

- **计算局部距离矩阵 $\mathbf{M}_{loc}$：** 给定 $\mathbf{x}$ 和 $\mathbf{y}$，$\mathbf{M}_{loc}$ 的大小为 $\alpha \times \beta$，其中 $\mathbf{M}_{loc}[u, v]$（$u \in [1, \alpha]$ 且 $v \in [1, \beta]$）表示 $x_u$ 和 $y_v$ 之间的距离。由于通常采用欧氏距离（Euclidean distance），我们有 $\mathbf{M}_{loc}[u, v] = D_{\text{ED}}(x_u, y_v)$，其中 $D_{\text{ED}}(x_u, y_v)$ 表示 $x_u$ 和 $y_v$ 之间的欧氏距离。
- **计算累加距离矩阵 $\mathbf{M}_{cum}$：** 在获得 $\mathbf{M}_{loc}$ 后，大小为 $\alpha \times \beta$ 的矩阵 $\mathbf{M}_{cum}$ 可以通过公式 (1) 进行迭代计算：

$$\mathbf{M}_{cum}[u, v] = \begin{cases} \mathbf{M}_{loc}[u, v] & u = 1, v = 1, \\ \mathbf{M}_{loc}[1, v] + \mathbf{M}_{cum}[1, v - 1] & v > 1, \\ \mathbf{M}_{loc}[u, 1] + \mathbf{M}_{cum}[u - 1, 1] & u > 1, \\ \mathbf{M}_{loc}[u, v] + \text{TripleMin}_{u, v} & \text{otherwise}, \end{cases} \quad (1)$$

其中 $\text{TripleMin}_{u, v} = \min(\mathbf{M}_{cum}[u - 1, v], \mathbf{M}_{cum}[u, v - 1], \mathbf{M}_{cum}[u - 1, v - 1])$ 用于获取三个输入中的最小值。最终，$D_{\text{DTW}}(\mathbf{x}, \mathbf{y}) = \mathbf{M}_{cum}[\alpha, \beta]$。

回顾我们的系统模型（第 II-A 节），云服务器可能会从数据提供方接收一组时间序列 $\{\mathbf{x}_i \mid i \in [1, n]\}$。假设分析人员向云服务器提交了一个查询序列 $\mathbf{y}$。我们在时间序列分析中的目标是在 $\{\text{D}_{\text{DTW}}(\mathbf{x}_i, \mathbf{y}) \mid i \in [1, n]\}$ 中找出最小的 $k$ 个 DTW 距离，这类似于 **$k$ 近邻（$k\text{NN}$）搜索 [**18]，并且在 [12] 中也进行了探讨。

综上所述，我们可以看到该时间序列分析包含**三个核心组件**：

1. 计算欧氏距离；
2. 执行 $\text{TripleMin}_{u, v}$；
3. 寻找前 $k$ 个最小值（top-$k$）。

显然，设计安全的时间序列分析方案的直观想法，就是从为这些核心组件中的每一个设计安全且高效的解决方案开始。值得注意的是，我们提出的方案也适用于其他时间序列分析任务，例如**时间扭曲编辑距离（Time Warp Edit Distance, TWED）[19]**——前提是它们也可以由上述三个组件构建而成。



### A. 动态时间规整

动态时间规整（DTW）是一种著名的算法，用于测量两个时间序列之间的相似度，尤其是当它们的长度不同或者在时间轴上存在非线性扭曲时。

给定两个时间序列，例如 $X=\{x_{1},x_{2},\cdot\cdot\cdot,x_{m}\}$ 和 $Y=\{y_{1},y_{2},\cdot\cdot\cdot,y_{n}\}$，其中 $m$ 和 $n$ 分分别表示序列 $X$ 和 $Y$ 的长度。为了通过动态时间规整（DTW）来衡量这两个时间序列之间的相似度，我们首先需要构建一个大小为 $m\times n$ 的距离矩阵（distance matrix）$M$。在这个矩阵中，元素 $M[i,j]$ 表示两个序列中对应点 $x_{i}$ 和 $y_{j}$ 之间的欧氏距离（Euclidean distance），即：

$$M[i,j]=Dist(x_{i},y_{j})=\sqrt{(x_{i}-y_{j})^{2}}$$

随后，动态时间规整（DTW）算法旨在寻找一条穿过该矩阵 $M$ 的规整路径（warping path）$W=\{w_{1},w_{2},\cdot\cdot\cdot,w_{L}\}$。这条路径由一系列矩阵元素组成，用以定义 $X$ 和 $Y$ 之间的对齐映射关系，其中路径中的第 $k$ 个元素可以表示为 $w_{k}=(i,j)$。为了确保规整路径的有效性，它必须满足以下三个关键的约束条件：

- **边界条件**：规整路径必须从矩阵的左下角开始，并在右上角结束，即 $w_{1}=(1,1)$ 且 $w_{L}=(m,n)$。
- **连续性**：路径中的步进只能移动到相邻的单元格，这意味着对于 $w_{k}=(i,j)$ 和 $w_{k-1}=(i',j')$，必须满足 $i-i'\le 1$ 且 $j-j'\le 1$。
- **单调性**：路径在时间上不能后退，也就是说，必须满足 $i-i'\ge 0$ 且 $j-j'\ge 0$。

在实际计算中，两个时间序列之间的动态时间规整（DTW）距离可以通过基于动态规划（dynamic programming）的迭代公式来高效求解。具体而言，累积距离（cumulative distance）矩阵 $D$ 的每个元素 $D[i,j]$ 可以通过以下方式计算：

$$D[i,j]=M[i,j]+\min(D[i-1,j],D[i,j-1],D[i-1,j-1])$$

其中，边界条件初始化为 $D[1,1]=M[1,1]$。最终，动态时间规整（DTW）的输出结果即为矩阵右上角的值，即 $D[m,n]$。

 

### B. 减法秘密共享

为了简单起见，这里我们重点讨论两方之间的秘密共享（secret sharing） 。受 ABY 框架中算术秘密共享（arithmetic secret sharing）表达方式的启发，我们将减法秘密共享（subtractive secret sharing）描述如下 ：  

- **共享值**：给定一个 $l$ 比特的秘密值 $z$，它可以被随机拆分为两个共享份额（shares） $\langle x\rangle_{0}\in\mathbb{Z}_{2^{l}}$ 和 $\langle x\rangle_{1}\in\mathbb{Z}_{2^{l}}$，满足迭代关系 $\langle x\rangle_{1}-\langle x\rangle_{0}=x \pmod{2^{l}}$ 。在下文中，为了节省空间，我们省略了 $\pmod{2^{l}}$ 。  
- **加法**：给定 $x$ 和 $y$ 的减法共享份额，值 $z = x+y = \langle z\rangle_{1}-\langle z\rangle_{0}$ 可以在本地直接计算 。具体而言，满足 $\langle z\rangle_{b}=\langle x\rangle_{b}+\langle y\rangle_{b}$，其中 $b\in\{0,1\}$ 。  
- **乘法**：给定 $z$ 和 $y$ 的减法共享份额，值 $z = x\cdot y = \langle z\rangle_{1}-\langle z\rangle_{0}$ 可以通过基于同态加密（homomorphic encryption）或基于乘法三元组（Beaver triple）的方法来获得，这两种方法都需要两方之间进行交互 。由于减法秘密共享中的乘法操作与算术秘密共享中的乘法操作非常相似，读者可以参考相关文献以获得详细解释 。  

### C. 同态秘密共享

同态秘密共享（homomorphic secret sharing, HSS）是一种基于减法共享值实现非交互式（non-interactive）乘法的新颖方法 。在这里，我们讨论一种实用的构造方式，它避免了初始设计中暴力破解方法所带来的计算低效问题 。通常，这种构造由以下五个算法组成 ：  

- **HSS.KeyGen($\lambda$)**：在输入安全参数 $\lambda$ 时，密钥生成算法首先选择两个大素数 $p$ 和 $q$，每个素数的长度为 $\lambda$ 比特，使得 $|p|=|q|=\lambda$，然后计算 $N=pq$ 。接下来，它从 $\mathbb{Z}_{N^{2}}^{*}$ 中随机采样 $g^{\prime}$ 和 $e$ 。随后，算法计算 $g=(g^{\prime})^{2N} \pmod{N^{2}}$ 以及公钥 $pk=g^{e} \pmod{N^{2}}$ 。最后，它将私钥设置为 $sk=e$ 。  

- **HSS.Share($x, sk$)**：在输入值 $x\in\mathbb{Z}_{2^{l/2}}$ 和私钥 $sk$ 时，共享算法首先生成 $\langle x\rangle_{1},\langle x\rangle_{0}\in\mathbb{Z}_{2^{l}}$，确保 $\langle x\rangle_{1}-\langle x\rangle_{0}=x$ 且 $\langle x\rangle_{1}>\langle x\rangle_{0}$ 。然后，它计算 $s=sk\cdot x=e\cdot x$ 并将 $s$ 拆分为 $s_{1}$ 和 $s_{0}$，使得 $s_{1}-s_{0}=s$ 且 $s_{1}>s_{0}$ 。值得注意的是，这里的 $s_{b}$（其中 $b\in\{0,1\}$）是在整数域上而不是在 $\mathbb{Z}_{2^{l}}$ 上 。因此，这里没有使用符号 $\langle s\rangle_{b}$ 以强调这一区别 。  

- **HSS.TokenGen($y, pk$)**：在输入值 $y\in\mathbb{Z}_{2^{l/2}}$ 和公钥 $pk$ 时，该算法随机选择 $r\in\mathbb{Z}_{N}$，然后用 $pk$ 对 $y$ 进行加密，得到密文 $[[y]]=(g^{r},pk^{r}(1+N)^{y}) \pmod{N^{2}}$ 。  

- **HSS.LocalCalc($b, \langle x\rangle_{b}, s_{b}, [[y]]$)**：在输入参与方的索引 $b$、共享值 $\{\langle x\rangle_{b},s_{b}\}$ 以及令牌 $[[y]]$ 时，每个参与方可以在本地计算出 $g_{b}=(pk^{r}(1+N)^{y})^{\langle x\rangle_{b}}\cdot (g^{r})^{-s_{b}} \pmod{N^{2}}$ 。  

- **HSS.DDlog($b, g_{b}$)**：在输入 $b$ 和 $g_{b}$ 时，每个参与方计算：

  

  $$\begin{cases}h=g_{b} \pmod N\\ h_{b}^{\prime}=\lfloor g_{b}/N\rfloor\end{cases}$$

  然后，每个参与方可以计算出 $\overline{z_{b}}=h_{b}^{\prime}\cdot h^{-1} \pmod N$ 。  



**正确性证明**：我们说同态秘密共享（HSS）技术是正确的，当且近当满足 $z_{1}-z_{0}=xy \pmod N$ 。 首先，我们证明 $g_{1}=g_{0}(1+N)^{xy} \pmod{N^{2}}$ ：  

$$\frac{g_{1}}{g_{0}}=\frac{(pk^{r}(1+N)^{y})^{\langle x\rangle_{1}-\langle x\rangle_{0}}}{(g^{r})^{s_{1}-s_{0}}}=\frac{g^{erx}(1+N)^{xy}}{g^{rex}}=(1+N)^{xy} \Leftrightarrow g_{1}=g_{0}(1+N)^{xy} \pmod{N^{2}}$$

根据文献 [20] 的引理 3.3，如果 $g_{1}=g_{0}(1+N)^{xy} \pmod{N^{2}}$，则 HSS.DDlog 算法将生成 $z_{b}$，从而确保 $z_{1}-z_{0}=xy \pmod N$ 。详细证明请参见文献 [20] 中的引理 3.3 。  

需要说明的是，上面描述的 HSS 范式是对文献 [20] 中提出原始构造的简化改写 。通过提取其核心思想，我们对其进行了定制，以适应我们特定场景的需求 。  



### 表 I：我们提出的方案中所使用的主要符号

| **符号 (Notation)**       | **定义 (Definition)**                             |
| ------------------------- | ------------------------------------------------- |
| $l$                       | 感知时序数据的最大比特长度                        |
| $\langle \cdot \rangle_b$ | 减法秘密共享的共享份额，$b \in \{0, 1\}$          |
| $[\cdot]_b$               | 布尔秘密共享的共享份额，$b \in \{0, 1\}$          |
| $\mathbf{x}_i$            | 由数据提供方 $p_i$ 收集的时间序列                 |
| $\mathbf{y}$              | 由分析人员提供的时间序列                          |
| $\alpha$                  | 时间序列 $\mathbf{x}_i$ 的长度                    |
| $\beta$                   | 时间序列 $\mathbf{y}$ 的长度                      |
| $\mathrm{M}_{loc,i}$      | $\mathbf{x}_i$ 与 $\mathbf{y}$ 之间的局部距离矩阵 |
| $\mathrm{M}_{cum,i}$      | $\mathbf{x}_i$ 与 $\mathbf{y}$ 之间的累积距离矩阵 |
| $\gamma$                  | 三个元素中的最小值                                |
| $d_i$                     | $\mathbf{x}_i$ 与 $\mathbf{y}$ 之间的 DTW 距离    |
| $h$                       | FTKS 协议中的底层层数                             |
| $\xi$                     | 用于比较模式切换的阈值层                          |
| $\omega$                  | 比较结果，$\omega \in \{0, 1\}$                   |
| $Fset$                    | 收集比较结果的集合                                |
| $Kset$                    | 包含 top-$k$ 元素的集合                           |





这里是论文第四章 **IV. 设计的构建块 (OUR DESIGNED BUILDING BLOCKS)** 的纯中文高保真逐句译文。

# IV. 设计的构建块

正如第三章 A 节所述，我们目标的时间序列分析包含三个主要组件：计算欧氏距离、执行三元组极小值以及寻找 Top-k。为了实现安全且高效的时间序列分析，首先设计相应的安全协议来实现这些组件至关重要。因此，在本节中，我们引入了三个新颖的构建块，即：非交互式安全欧氏距离（NISED）协议、安全三元组极小值（STM）协议以及共享数据快速 Top-k（FTKS）协议。为了清晰和便于查阅，我们在表 I 中定义了本研究中所使用的主要符号和变量。

### A. 非交互式安全欧氏距离 (NISED)

由于计算平方欧氏距离可以在不影响时间序列分析正确性的前提下，避免昂贵的平方根运算，因此在后续讨论中我们将重点关注 $D_{ED}^{2}(x,y)$。在这种背景下，我们有：

$$D_{ED}^{2}(x,y)=(x-y)^{2}=x^{2}+y^{2}-2xy$$

值得注意的是，我们以一维数据为例，这可以很容易地扩展到多维情况。显然，当采用加法秘密共享或加法同态加密（HE）时，计算 $D_{ED}^{2}(x,y)$ 的主要难点在于计算乘法项 $xy$。先前的研究通过采用交互式解决方案解决了这一问题，例如文献 [11] 中基于 Beaver 乘法三元组的方法和文献 [12] 中基于同态加密的方法。然而，它们会带来巨大的通信开销。

在这项工作中，我们通过简化和优化同态秘密共享（HSS）技术，设计了一种非交互式安全欧氏距离（NISED）协议。为了实现这一目标，我们引入了一种新的同态秘密共享构造，简称为 NHSS。与第三章 C 节中描述的简化 HSS 类似，NHSS 也包含五个算法。其中，`NHSS.Share()`、`NHSS.TokenGen()` 和 `NHSS.LocalCalc()` 与简化的 HSS 技术相比保持不变。因此，我们的讨论重点放在 `NHSS.KeyGen()` 和 `NHSS.DDlog()` 算法上，其详细内容如下：

- **NHSS.KeyGen($\lambda$)**：给定安全参数 $\lambda$，密钥生成算法选择一个具有 $\lambda$ 比特的大素数 $N$。在从 $\mathbb{Z}_{N}^{*}$ 中随机采样 $g'$ 之后，它计算 $g \equiv (g^{\prime})^{N-1} \pmod{N^{2}}$。然后，算法选择一个私钥 $sk=e\in\mathbb{Z}_{N}^{*}$ 并计算公钥 $pk=g^{e} \pmod{N^{2}}$。
- **NHSS.DDlog($b, g_{b}$)**：给定参与方的索引 $b$ 和 $g_{b}$，每个参与方可以简单地计算出 $z_{b}=h_{b}^{\prime} \pmod N$，其中 $h_{b}^{\prime}=\lfloor g_{b}/N\rfloor$。

**正确性证明**：我们说 NHSS 是正确的，当且仅当 `NHSS.DDlog()` 与 `HSS.DDlog()` 等价。

假设在 `NHSS.DDlog()` 中存在像 `HSS.DDlog()` 中一样的 $h=g_{b} \pmod N$，我们有：

$$h=(pk^{r}(1+N)^{y})^{\langle x\rangle_{b}}\cdot(g^{r})^{-s_{b}} \pmod N = g^{er\langle x\rangle_{b}}(1+N)^{y\langle x\rangle_{b}}\cdot g^{-rs_{b}} \pmod N$$

通过将 $g'$ 代入上述公式，我们可以验证出 $h \equiv 1 \pmod N$。因此，在 NHSS 构造中 $z_{b}=h_{b}^{\prime} \cdot 1^{-1} \pmod N = h_{b}^{\prime} \pmod N$，这与 `HSS.DDlog()` 是等价的。

现在，我们准备给出基于 NHSS 构造的 NISED 协议。假设每个服务器 $\mathcal{S}_{b}$ 都持有 $\{\langle x\rangle_{b},s_{b},\langle x^{2}\rangle_{b},\langle y^{2}\rangle_{b},[[y]]\}$，其中 $x,y\in\mathbb{Z}_{2^{l/2}}$ 且 $b\in\{0,1\}$。在此设置下，$\mathcal{S}_{b}$ 执行以下步骤来计算 $\langle\delta\rangle_{b}$，以确保 $\langle\delta\rangle_{1}-\langle\delta\rangle_{0}=D_{ED}^{2}(x,y)$：

- **步骤 1**：$\mathcal{S}_{b}$ 调用 `NHSS.LocalCalc()` 来获取 $g_{b}$，即 $g_{b}\leftarrow \mathrm{NHSS.LocalCalc}(b,\langle x\rangle_{b},s_{b},[[y]])$。

- **步骤 2**：凭借 $g_{b}$，$\mathcal{S}_{b}$ 调用 `NHSS.DDlog()` 来获得 $z_{b}\leftarrow \mathrm{NHSS.DDlog}(b, g_{b})$。然后，由于 $x,y\in\mathbb{Z}_{2^{l/2}}$，$\mathcal{S}_{b}$ 可以直接将 $z_{b}$ 转换为减法共享份额 $\langle z\rangle_{b}$。值得注意的是，正如文献 [20] 中所证明的，转换错误的发生概率极小，可以忽略不计。

- **步骤 3**：凭借 $\{\langle x^{2}\rangle_{b},\langle y^{2}\rangle_{b},\langle z\rangle_{b}\}$，$\mathcal{S}_{b}$ 可以在本地计算出：

  $$\langle\delta\rangle_{b}=\langle x^{2}\rangle_{b}+\langle y^{2}\rangle_{b}-2\langle z\rangle_{b}$$

**正确性证明**：显而易见，我们有：

$$\langle\delta\rangle_{1}-\langle\delta\rangle_{0}=(\langle x^{2}\rangle_{1}+\langle y^{2}\rangle_{1}-2\langle z\rangle_{1}) - (\langle x^{2}\rangle_{0}+\langle y^{2}\rangle_{0}-2\langle z\rangle_{0})$$

$$=\langle x^{2}\rangle_{1}-\langle x^{2}\rangle_{0}+\langle y^{2}\rangle_{1}-\langle y^{2}\rangle_{0}-2(\langle z\rangle_{1}-\langle z\rangle_{0}) = x^{2}+y^{2}-2xy=D_{ED}^{2}(x,y)$$

**评注**：在我们的 NISED 协议中，我们遵循混合模型的技术路线，在保持高效率的同时实现了零交互。此外，我们简化并优化了 HSS 的构造，从而带来了额外的性能提升。关于安全性，第六章 A 节将提供形式化分析，证明该协议能够抵抗半诚实的对手。

### B. 安全三元组极小值 (STM)

STM 协议旨在安全地确定三个元素 $\{x,y,w\}$ 中的最小值 $\gamma$，使得 $\gamma=\min(x,y,w)$。同时，该协议要求执行者（即云服务器）对输入值 $\{x,y,w\}$ 和输出值 $\gamma$ 一无所知，从而保护了“哪一个元素是最小值”的机密性。虽然先前的解决方案满足上述安全要求，但它们通常在计算或通信成本方面表现出低效。最近的工作 [12] 提出了一种基于同态加密（HE）的解决方案，该方案可以显著减少文献 [11] 中所需的通信轮数，从而降低时间延迟。然而，由于全程使用同态加密，它在计算成本上面临性能问题。

在这项工作中，我们将充分利用秘密共享和同态加密各自的特点并将它们混合，以实现平衡的性能。最终，与最近的工作 [12] 相比，我们的 STM 协议可以在不增加通信轮数的前提下，极大地优化计算成本和通信量。

为了获取最小值，典型的方法是先计算前两个元素的最小值，然后将该结果与第三个元素进行比较，最终得到全局最小值。我们将该方法形式化如下：

$$z = \theta(x-y) + y$$

$$\gamma = \eta(z-w) + w$$

其中，

$$\theta = \begin{cases} 1 & \text{如果 } x < y \\ 0 & \text{否则} \end{cases}$$

$$\eta = \begin{cases} 1 & \text{如果 } z < w \\ 0 & \text{否则} \end{cases}$$

为了实现上述过程，我们的核心思想是采用加法同态加密（例如 Paillier）并结合文献 [12] 中的比较解决方案来获取 $\theta$，因为我们发现这可以避免通信轮数的爆炸性增长，同时保持可接受的计算成本。然后，我们集成了文献 [11] 中基于 Beaver 三元组的方法来计算 $\theta$ 与 $(x-y)$ 的乘法，从而利用其优异的计算性能。然而，要完美地混合这两条技术路线以达到效率优化并不容易。这是因为我们需要将同态加密的评估结果无缝转换为秘密共享的格式，并防止性能下降。

为了解决这一问题，我们提出了 STM 协议，该协议可以巧妙地混合这两种技术，并在三个共享值 $\{\langle x\rangle_{b},\langle y\rangle_{b},\langle w\rangle_{b} \mid b\in\{0,1\}\}$ 之中高效地获得满足 $\gamma=\min(x,y,w)$ 的共享极小值 $\langle\gamma\rangle_{b}$。假设每个服务器 $\mathcal{S}_{b}$ 持有 $\{\langle x\rangle_{b},\langle y\rangle_{b},\langle w\rangle_{b}\}$，我们的 STM 协议工作流程如下：

- **步骤 1**：$\mathcal{S}_{1}$ 首先在本地计算 $\langle\sigma\rangle_{1}=\langle x\rangle_{1}-\langle y\rangle_{1}$。在生成同态加密的密钥对 $(pk_{1},sk_{1})$ 之后，$\mathcal{S}_{1}$ 将 $\langle\sigma\rangle_{1}$ 加密为 $E(\langle\sigma\rangle_{1})$ 并将该密文发送给 $\mathcal{S}_{0}$。在这里，我们用 $E(\cdot)$ 表示使用加法同态加密得到的密文。

- **步骤 2**：一旦接收到 $E(\langle\sigma\rangle_{1})$，$\mathcal{S}_{0}$ 在本地计算 $\langle\sigma\rangle_{0}=\langle y\rangle_{0}-\langle x\rangle_{0}$，然后利用加法同态性质计算出 $E(\sigma)$。在这里，$\sigma = \langle\sigma\rangle_{1} - (-\langle\sigma\rangle_{0}) = \langle\sigma\rangle_{1} + \langle\sigma\rangle_{0} = x-y$。在随机选择一个硬币翻转状态 $f\in\{0,1\}$ 以及满足 $\{r_{1},r_{2} \mid N/2-r_{1}<r_{2}<N/2\}$ 的两个值**（其中 $N=pq$ 是 Paillier 加密的参数）**之后，$\mathcal{S}_{0}$ 计算 $E(\phi)$：如果 $f=0$，则 $\phi=r_{1}\cdot(\sigma+1)+r_{2}$；如果 $f=1$，则 $\phi=r_{1}\cdot(-\sigma)+r_{2}$。接下来，$\mathcal{S}_{0}$ 将 $E(\phi)$ 传输给 $\mathcal{S}_{1}$。与此同时，$\mathcal{S}_{0}$ 设置 $\langle\theta\rangle_{0}=f$。

- **步骤 3**：在收到 $E(\phi)$ 时，$\mathcal{S}_{1}$ 使用 $sk_{1}$ 进行解密恢复。然后，$\mathcal{S}_{1}$ 检查是否满足 $\phi>N/2$。如果是，它设置 $\langle\theta\rangle_{1}=0$；否则设置 $\langle\theta\rangle_{1}=1$。接下来，$\mathcal{S}_{1}$ 和 $\mathcal{S}_{0}$ 共同协作来计算 $\theta$ 和 $\sigma$ 的乘法。然而，当 $\langle\theta\rangle_{0}=1$ 且 $x\ge y$ 时，$\langle\theta\rangle_{1}-\langle\theta\rangle_{0}=-1$ 而不是 $1$。为了处理这种情况，我们有以下推导：

  $$\theta\cdot(x-y) \Leftrightarrow (\langle\theta\rangle_{1}+\langle\theta\rangle_{0}-2\langle\theta\rangle_{1}\langle\theta\rangle_{0})\cdot(\langle\sigma\rangle_{1}+\langle\sigma\rangle_{0})$$

  $$=(\langle\theta\rangle_{1}+\langle\theta\rangle_{0})\cdot(\langle\sigma\rangle_{1}+\langle\sigma\rangle_{0})-2(\langle\theta\rangle_{1}\langle\theta\rangle_{0}\langle\sigma\rangle_{1}+\langle\theta\rangle_{1}\langle\theta\rangle_{0}\langle\sigma\rangle_{0})$$

  令 $\Delta_{1}=\langle\theta\rangle_{1}\langle\sigma\rangle_{1}$，$\Delta_{0}=\langle\theta\rangle_{0}\langle\sigma\rangle_{0}$，则上式等价于：

  $$=(\langle\theta\rangle_{1}+\langle\theta\rangle_{0})\cdot(\langle\sigma\rangle_{1}+\langle\sigma\rangle_{0})-2(\Delta_{1}\langle\theta\rangle_{0}+\Delta_{0}\langle\theta\rangle_{1})$$

  $$=(\langle\theta\rangle_{1}+\langle\theta\rangle_{0})\cdot(\langle\sigma\rangle_{1}+\langle\sigma\rangle_{0})-2(\Delta_{1}+\Delta_{0})\cdot(\langle\theta\rangle_{1}+\langle\theta\rangle_{0}) + 2(\Delta_{1}\langle\theta\rangle_{1}+\Delta_{0}\langle\theta\rangle_{0})$$

  由于每个服务器 $\mathcal{S}_{b}$ 可以在本地计算 $\Delta_{b}$ 和 $\Delta_{b}\langle\theta\rangle_{b}$，我们成功地将 $\theta\cdot(x-y)$ 的计算转换为了两次基于 Beaver 三元组的乘法。一次是计算 $(\langle\theta\rangle_{1}+\langle\theta\rangle_{0})\cdot(\langle\sigma\rangle_{1}+\langle\sigma\rangle_{0})$，另一次是计算 $(\Delta_{1}+\Delta_{0})\cdot(\langle\theta\rangle_{1}+\langle\theta\rangle_{0})$。值得注意的是，由于这两次乘法可以同时并行处理，因此它仅需要 2 轮交互和 6 个元素的通信开销。为了便于描述，我们假设 $\mathcal{S}_{b}$ 获得的 $(\langle\theta\rangle_{1}+\langle\theta\rangle_{0})\cdot(\langle\sigma\rangle_{1}+\langle\sigma\rangle_{0})$ 的加法份额为 $\rho_{b}$，获得的 $(\Delta_{1}+\Delta_{0})\cdot(\langle\theta\rangle_{1}+\langle\theta\rangle_{0})$ 的加法份额为 $\hat{\rho}_{b}$。

- **步骤 4**：凭借 $\{\langle\theta\rangle_{b},\Delta_{b},\rho_{b},\hat{\rho}_{b}\}$，$\mathcal{S}_{b}$ 在本地计算 $res_{b} = \rho_{b}-2\hat{\rho}_{b}+2\Delta_{b}\langle\theta\rangle_{b}$。然后，$\mathcal{S}_{1}$ 直接得到 $\langle z\rangle_{1}=res_{1}+\langle y\rangle_{1}$。对于 $\mathcal{S}_{0}$，它设置 $\langle z\rangle_{0}=-res_{0}+\langle y\rangle_{0}$。在这种情况下，我们可以将结果转换回减法秘密共享的格式，即 $z=\langle z\rangle_{1}-\langle z\rangle_{0}$。随后，$\mathcal{S}_{b}$ 可以使用 $\{\langle z\rangle_{b},\langle w\rangle_{b}\}$ 作为输入，再次调用上述步骤（步骤 1-4）以获得 $\langle\gamma\rangle_{b}$。

**正确性证明**：我们说我们的 STM 协议是正确的，如果满足 $\langle\gamma\rangle_{1}-\langle\gamma\rangle_{0}=\gamma=\min(x,y,w)$。由于计算 $\langle\gamma\rangle_{b}$ 遵循与计算 $z$ 相同的步骤，因此我们 STM 协议的正确性等价于证明 $\langle z\rangle_{1}-\langle z\rangle_{0}=z=\min(x,y)$。

1. 我们首先讨论 $\langle\theta\rangle_{0}=0$ 的情况：如果 $x<y$，我们有 $\phi<N/2$。因此，$\langle\theta\rangle_{1} = 1$ 且 $\langle\theta\rangle_{1}-\langle\theta\rangle_{0}=1$。根据等式 $res_{b} = \rho_{b}-2\hat{\rho}_{b}+2\Delta_{b}\langle\theta\rangle_{b}$，我们知道 $res_{1}+res_{0}=1\cdot(x-y)=x-y$。由于 $\langle z\rangle_{1}=res_{1}+\langle y\rangle_{1}$ 且 $\langle z\rangle_{0}=-res_{0}+\langle y\rangle_{0}$，我们有 $\langle z\rangle_{1}-\langle z\rangle_{0}=res_{1}+\langle y\rangle_{1}+res_{0}-\langle y\rangle_{0}=x-y+y=x$。如果 $x\ge y$，我们有 $\phi>N/2$。在这种背景下，$\langle\theta\rangle_{1}=0$ 且 $\langle\theta\rangle_{1}-\langle\theta\rangle_{0}=0$。这确保了 $res_{1}+res_{0}=0$。因此，$\langle z\rangle_{1}-\langle z\rangle_{0}=y$。结果表明，在 $\langle\theta\rangle_{0}=0$ 的情况下，满足 $z=\min(x,y)$。
2. 同理，我们可以证明在 $\langle\theta\rangle_{0}=1$ 的情况下，同样满足 $z=\min(x,y)$。

**评注**：为了安全地获取三个元素中的最小值，最近的工作 [12] 需要传输 16个 同态密文并进行 8 轮通信。相比之下，我们的 STM 协议仅涉及 4个 同态密文和 8 轮通信，并且与基于同态加密的方法相比，基于 Beaver 三元组的乘法提供了卓越的计算效率。值得注意的是，尽管我们的 STM 协议需要传输额外的 12 个元素，但这些操作都是在 $\mathbb{Z}_{2^{l}}$ 上进行的，其数据体量显著小于密文大小。详细的性能评估请参见第七章 A 节。

![image-20260618132434032](C:\Users\77231\AppData\Roaming\Typora\typora-user-images\image-20260618132434032.png)



 

### C. 共享数据快速 Top-k (FTKS)

由于 Top-k 算法具有广泛的应用场景，研究安全且高效的 Top-k 算法一直以来都吸引了极大的关注。然而，现有的方案主要依赖于最小堆（min-heap）算法，该算法需要进行 $O(n \log k)$ 次比较（其中 $n$ 是所有元素的数量），以确保在密文上进行 Top-k 选择的效率。这自然提出了一个关键问题：“是否可能拥有一种具有更低复杂度的安全 Top-k 算法？”

在这项工作中，受 Dodgson 方法的启发，我们设计了一种基于树的安全 Top-k 算法，它将比较复杂度从 $O(n \log k)$ 降低到了 $O(n-k+(k-1)\log(n-k+1))$（请注意，为了呈现我们算法的细节，我们没有将其简化为 $O(n)$）。此外，我们观察到我们设计中的绝大多数比较都发生在树结构的构建过程中，而安全比较协议的通信开销是主要的性能瓶颈。为了解决这个问题，我们创新性地引入了“旋转”概念，并设计了一种基于旋转的混合比较模式，从而显著降低了通信开销。通过将这些思想无缝地集成到共享数据中，我们提出了我们的 FTKS 协议。

#### 1) 基于树的安全 Top-k 算法

假设有一个数据集 $\{d_{i} \mid i\in[1,n], d_{i}\in\mathbb{Z}_{2^{l}}\}$，每个元素 $d_{i}$ 被共享并分发给 $\{\mathcal{S}_{1},\mathcal{S}_{0}\}$，其中 $\mathcal{S}_{1}$ 持有 $\langle d_{i}\rangle_{1}$，而 $\mathcal{S}_{0}$ 持有 $\langle d_{i}\rangle_{0}$。通常，安全 Top-k 算法旨在识别该数据集中最小（或最大）的 $k$ 个元素，并确保最终的 Top-k 元素保持共享份额的格式。在这里，我们仅讨论寻找最小 $k$ 个元素的过程，需要指出的是，寻找最大 $k$ 个元素的过程遵循类似的步骤。

![image-20260615160806373](C:\Users\77231\AppData\Roaming\Typora\typora-user-images\image-20260615160806373.png)

我们的安全 Top-k 算法的核心思想是构建一棵满二叉树，并通过调用安全比较协议逐步推选出极小值。通过重复 $k-1$ 次来更新树中的极小值路径，我们就可以获得所有的 Top-k 元素。具体而言，我们基于树的安全 Top-k 算法包含以下四个组件，并在算法 1 中阐述了其细节：

- **准备集合**：每个参与方 $\mathcal{S}_{b}$ 通过保留 $\{d_{i} \mid i\in[1,n]\}$ 的最后 $k-1$ 个元素，生成一个新集合 $Rset_{b}=\{\langle m_{\iota}\rangle_{b} \mid \iota\in[1,k-1]\}$，即 $\langle m_{i}\rangle_{b}=\langle d_{i+n-k+1}\rangle_{b}$。然后，$\mathcal{S}_{b}$ 生成另一个集合 $Nset_{b}=\{\langle m_{i}\rangle_{b} \mid i\in[1,n']\}$，其中 $n'=2^{\lceil \log(n-k+1)\rceil}$，$\langle m_{i\in[1,n-k+1]}\rangle_{b}=\langle d_{i\in[1,n-k+1]}\rangle_{b}$ 且 $\langle m_{i\in(n-k+1,n']}\rangle_{b}=\langle MAX\rangle_{b}$。在这里，$\langle MAX\rangle_{b}$ 是该值域中的最大值，并在事前由 $\mathcal{S}_{1}$ 和 $\mathcal{S}_{0}$ 共同共享。
- **构建最小树**：每个参与方 $\mathcal{S}_{b}$ 构建一棵满二叉树来寻找共享极小值，具体细节见函数 1。在该函数中，`SecComparison()` 协议用于比较两个共享值，并输出 $\omega$ 以指示第一个共享值是否小于第二个共享值。如果是，则 $\omega=1$，否则 $\omega=0$。值得注意的是，$\omega$ 会向双方公开。关于 `SecComparison()` 的具体实现，我们将在后续部分进行讨论。
- **寻找路径**：在构建好最小树后，根节点必定是 Top-k 元素之一。然后，我们以自底向上的方式寻找覆盖该极小值的路径，如函数 2 所示。
- **更新路径**：在算法 1 中，第 8-21 行实现了路径更新过程。其核心思想是用保留的元素替换极小值路径，然后将该保留元素与其兄弟节点的值进行比较。如果保留的元素较小，则父节点将用它来更新；否则，父节点将用相应的兄弟节点值来更新。显而易见，在更新路径之后，根节点依然会是 Top-k 元素之一。一旦所有保留的元素都被更新完毕，该算法就成功识别出了所有的 Top-k 元素。

#### 2) 基于旋转的混合比较模式

现在，我们深入探讨算法 1 和函数 1 中所使用的 `SecComparison()` 协议的具体实现。通常，有两种常见的方法来实现共享数据上的安全比较：

1. 基于最高有效位（MSB）的方法 [11]；
2. 基于同态加密（HE）的方法 [12]（注意，这里的基于 HE 的比较采用了原始的 Paillier 加密，而不是文献 [12] 中的门限版本）。

经过仔细研究，我们发现虽然基于 MSB 的方法在计算上更有效率，但它需要较多的交互轮数。更关键的是，这些轮数无法并行化，这在现实应用中会显著降低性能。因此，文献 [12] 采用了基于 HE 的方法，通过减少通信轮数，从而在广域网环境下实现了更优的安全时间序列分析性能。然而，基于 MSB 的方法实际上在计算量和通信量方面表现更好。因此产生了一个疑问：“我们能否设计一种新方法，在所有指标上都实现更好的性能？”

得益于基于树的 Top-k 算法，我们提出了一种基于旋转的混合比较模式，它充分利用了上述两种方法的特点，从而在显著降低计算开销的同时，也大幅减少了通信开销。如前所述，我们基于树的安全 Top-k 算法可以达到 $O(n-k+(k-1)\log(n-k+1))$ 的比较复杂度。当数据规模 $n$ 较大时，它会主导其他各项，这主要是由于构建最小树的复杂度所致。此外，我们观察到树的较低层（靠近叶子节点层）涉及了更大数量的比较，但这些比较可以很好地并行化。为了利用这一特点，我们的想法是“旋转”这些层的比较，然后设计一种旋转 MSB 比较方法来减少比较的总次数，从而在计算和通信两方面实现极高的效率。对于较高层，由于并行化的效果较差，因此采用基于 HE 的比较来最小化通信轮数，从而在实际应用中进一步提升效率。借助这种混合思想，我们可以显著提高基于树的安全 Top-k 算法的性能。

![image-20260615161010803](C:\Users\77231\AppData\Roaming\Typora\typora-user-images\image-20260615161010803.png)

综上所述，我们基于旋转的混合比较模式由两部分组成：旋转（Rotation）与决策（Decision）。

- **旋转**：为了便于描述，我们以第 $h$ 层为例（其中 $h=\log n'$）来阐述旋转的概念。如图 2 所示，原始方法需要进行 $n'/2$ 次比较。通过设计旋转 MSB 比较，单次比较就可以取代该层内的所有比较，这等效于将比较复杂度从 $O(n)$ 降低到了次线性级别。旋转 MSB 比较的详细步骤在算法 2 中给出。值得注意的是，在该算法中，`AND`（与）门操作是使用 Beaver 三元组风格的方法实现的。由于双方都需要确定选择哪些子节点，因此它们通过交换 $Fset_{b}$ 来重建全局的 $Fset$。在下文中，我们使用符号 $[\cdot]_{b}$ 来表示布尔共享份额。

- **决策**：如上所述，旋转 MSB 比较能让较低层显著受益，而基于 HE 的方法由于通信轮数较少，更适合较高层。这自然引出了一个问题：我们如何决定在两者的哪一层进行切换？我们的想法是考虑到更少的通信轮数，并提供以下指南来进行决策：

  $$\frac{n^{\prime}}{2^{h-\xi}} > (l-2)\cdot 6 + 4 \Rightarrow \xi > h - \left\lceil \log\left(\frac{n^{\prime}}{6l-8}\right) \right\rceil$$

  其中，$\xi$ 是执行模式切换的阈值层。也就是说，如果层数大于 $h - \left\lceil \log\left(\frac{n^{\prime}}{6l-8}\right) \right\rceil$，则采用旋转 MSB 比较；否则，使用基于 HE 的比较。值得注意的是，每个 `AND` 操作需要两轮交互，而重建 $Fset$ 同样需要两轮交互。因此，我们的旋转 MSB 比较在每一层涉及 $(l-2)\cdot 6 + 4$ 轮通信。

因此，我们提出了函数 `BuildMinTree()` 的更新版本，在函数 3 中将其称为 `NewBuildMinTree()`，以形式化上述构建最小树的过程。在该函数中，`Reconstruct()` 表示两方通过交换各自的份额来恢复秘密。由于 `RotatedMSBComparison()` 和 `HE-basedComparison()` 均输出布尔共享份额，因此恢复操作是通过异或（XOR）运行的，例如 `Reconstruct`($[\omega_{i}]_{0}, [\omega_{i}]_{1}$) 计算出 $\omega_{i}=[\omega_{i}]_{0}\oplus[\omega_{i}]_{1}$。



#### ![image-20260615161027868](C:\Users\77231\AppData\Roaming\Typora\typora-user-images\image-20260615161027868.png)3) 我们的 FTKS 协议

通过将基于旋转的混合比较模式融入到我们基于树的安全 Top-k 算法中，我们给出了共享数据快速 Top-k（FTKS）协议，实现了显著的性能提升。详细的性能评估可以在第七章 A 节中找到。该协议的实现非常直接：只需将算法 1 中的第 4 行替换为 `NewBuildMinTree()` 函数，并保持其他行保持不变即可。

  

# V. 提出的方案

凭借上述准备就绪的构建块，我们构建了我们的安全时间序列分析方案 。首先，我们引入了一种实用的安全时间序列分析方案，简称为 PSTSA，其重点是在不牺牲安全性的前提下显著提高性能 。然后，我们提出了一种安全增强型的时间序列分析方案，命名为 SETSA，该方案专为存在更强对手的场景而定制 。  

### A. 我们的 PSTSA 方案

我们的 PSTSA 方案旨在提高时间序列分析的计算和通信效率 。同时，它应当保持与现有方案 [12] 相同的安全级别，有效保护数据提供方 $\mathcal{P}$ 的外包时间序列以及分析人员 $A$ 提交的查询请求 。通常，我们的 PSTSA 方案包含以下四个阶段 ：  

- **初始化**：在初始化阶段，组织 O 调用 `NHSS.KeyGen()` 算法来生成密钥对 $(sk=e, pk=g^{e} \pmod{N^{2}})$ 。随后，将 $sk$ 授权给已注册的数据提供方 $\mathcal{P}$，并公开发布 $pk$ 。  
- **数据上传**：利用授权的密钥 $sk$，数据提供方 $p_i$ 通过以下步骤处理收集到的时间序列 $\mathbf{x}_{i}=\{(t_{1},x_{1}),(t_{2},x_{2}),...,(t_{\alpha},x_{\alpha})\}$ ：  
  - **步骤 1**：$p_i$ 针对每个时间点调用 `NHSS.Share`($x_j, sk$)（其中 $j\in[1,\alpha]$），生成 $(\langle x_{j}\rangle_{1},\langle x_{j}\rangle_{0})$ 以及 $(s_{j,1},s_{j,0})$，其中 $s_{j}=sk\cdot x_{j}$ 。  
  - **步骤 2**：$p_i$ 计算 $x_{j}^{2}$ 并利用减法秘密共享对其进行拆分，生成 $(\langle x_{j}^{2}\rangle_{1},\langle x_{j}^{2}\rangle_{0})$ 。  
  - **步骤 3**：$p_i$ 定义 $x_{i,b}=\{id_{i},t_{j},\langle x_{j}\rangle_{b},\langle x_{j}^{2}\rangle_{b},s_{j,b} \mid j\in[1,\alpha], b\in\{0,1\}\}$ 。在这里，$id_{i}$ 是 $p_{i}$ 的标识符 。然后，$p_i$ 将 $x_{i,b}$ 发送给云服务器 $\mathcal{S}_{b}$ 。  
- **令牌生成**：假设分析人员持有一个时间序列 $\mathbf{y}=\{(t_{1},y_{1}),(t_{2},y_{2}),...,(t_{\beta},y_{\beta})\}$ 。首先，分析人员可以调用 `NHSS.TokenGen`($y_{j},pk$) 来获取针对 $j=1,2,...,\beta$ 的令牌 $[[y_{j}]]$ 。然后，生成 $(\langle y_{j}^{2}\rangle_{1},\langle y_{j}^{2}\rangle_{0})$ 。此后，将 $\{t_{j},\langle y_{j}^{2}\rangle_{b},[[y_{j}]] \mid j\in[1,\beta]\}$ 传输给 $\mathcal{S}_{b}$ 。  
- **数据分析**：由于我们的系统中存在多个数据提供方，即 $\mathcal{P}=\{p_{1},p_{2},...,p_{n}\}$，因此每个云服务器 $\mathcal{S}_{b}$ 都会累积一个时间序列集合 $\{x_{i,b} \mid i\in[1,n]\}$ 。在接收到令牌 $\{t_{j},\langle y_{j}^{2}\rangle_{b},[[y_{j}]] \mid j\in[1,\beta]\}$ 后，$\mathcal{S}_{b}$ 通过执行以下步骤，识别出与接收到的令牌具有最短 DTW 距离的 $k$ 个累积时间序列 ：  
  - **步骤 1：计算共享的局部距离矩阵 $\mathrm{M}_{loc,i,b}$**。对于每个时间序列 $x_{i,b}$，$\mathcal{S}_{b}$ 采用我们的 NISED 协议获取共享的局部距离矩阵 $\mathrm{M}_{loc,i,b}$，其输入为：$x_{i,b}$ 以及 $\{ \langle y_{j}^{2}\rangle_{b},[[y_{j}]] \mid j\in[1,\beta]\}$ 。值得注意的是，在此步骤中 $\mathcal{S}_{1}$ 和 $\mathcal{S}_{0}$ 之间不会发生任何交互 。  
  - **步骤 2：计算共享的累积距离矩阵 $\mathrm{M}_{cum,i,b}$**。凭借 $\mathrm{M}_{loc,i,b}$，$\mathcal{S}_{1}$ 和 $\mathcal{S}_{0}$ 共同协作来获取 $\mathrm{M}_{cum,i,b}$ 。根据公式 (1)，我们知道存在两类操作：加法和 $TripleMin_{u,v}$ 。由于当 $\mathrm{M}_{loc,i}$ 通过减法秘密共享进行共享时，加法操作可以在本地完成，因此计算 $\mathrm{M}_{cum,i,b}$ 的主要难点在于获取 $\langle\gamma_{u,v}\rangle_{b}$ ，其中我们将 $\gamma_{u,v}$ 定义为 $TripleMin_{u,v}$ 。显然，我们的 STM 协议可以用于此处来计算 $\langle\gamma_{u,v}\rangle_{b}$ 。然后，凭借 $\mathrm{M}_{loc,i,b}$ 和 $\langle\gamma_{u,v}\rangle_{b}$，$\mathcal{S}_{b}$ 可以按照公式 (1) 在本地计算出 $\mathrm{M}_{cum,i,b}$ 。  
  - **步骤 3：选择 Top-$k$ DTW 距离**。在获得 $\mathrm{M}_{cum,i,b}$ 后，$\mathcal{S}_{b}$ 可以通过设置 $\langle d_{i}\rangle_{b} \leftarrow \mathrm{M}_{cum,i,b}[\alpha][\beta]$，轻松获得 $\mathbf{x}_{i}$ 与 $\mathbf{y}$ 之间的 DTW 距离 。为了简单起见，我们将 $D_{DTW}(x_{i},y)_{b}$界定为 $\langle d_{i}\rangle_{b}$ 。现在，$\mathcal{S}_{b}$ 可以利用我们的 FTKS 协议，以 $\{\langle d_{i}\rangle_{b} \mid i\in[1,n]\}$ 作为输入，获取包含 Top-$k$ 共享 DTW 距离的集合 $Kset_{b}$ 。最终，$\mathcal{S}_{b}$ 将 $Kset_{b}$ 返回给相应的分析人员 。  

### B. 我们的 SETSA 方案

在 STDA [12] 中，发起的形式化安全证明表明，即使在识别 Top-$k$ 距离的过程中泄露了比较结果，外包的时间序列和查询令牌对于半诚实的对手而言也是安全的 。出于实用性考虑，我们的 PSTSA 方案同样公开了这类信息 。然而，在某些场景中，可能存在更强的对手，即对手拥有一些关于时间序列或查询令牌的背景知识 ，并且他们可能会利用公开的比较结果来推断其他的时间序列或查询令牌 。因此，我们提出了一种安全增强型的时间序列分析（SETSA）方案来实现不透光（oblivious）的时间序列分析，即让对手对比较结果一无所知 。  

![image-20260615161424061](C:\Users\77231\AppData\Roaming\Typora\typora-user-images\image-20260615161424061.png)

在我们的 PSTSA 方案中，比较结果是在识别 Top-$k$ 距离的过程中公开的 。因此，一个直观的想法是设计一种不透光的 Top-$k$ 算法来隐匿比较结果 。当然，这并不是一个新的课题，学术界已经提出了大量的方案来实现不透光的 Top-$k$ 算法 。最近，Cong 等人 [15] 提出了一种最先进的不透光 Top-$k$ 算法，其复杂度为 $O(n \log^{2} k)$ 。同时，我们观察到，通过对比较操作进行精心重排，我们基于旋转的混合比较模式可以无缝地集成到该算法中 。因此，我们 SETSA 方案的主要目标是设计一种在共享数据上更高效的不透光 Top-$k$ 算法 。  

现在，我们通过阐明我们的不透光 Top-$k$ 算法与 FTKS 协议之间的大主要区别来对其进行介绍 。这些区别包括：  

1. 在获得比较结果（即 $Fset_b$）后，不透光 Top-$k$ 算法不会通过交换份额来重建全局的 $Fset$ 。相反，它确保比较结果对于任何一方都保持私密 ；  

2. 该算法采用“先比较后交换”（compare-then-exchange）的方法来计算较小值和较大值，而不是直接根据恢复的标志位进行选择 。通常，每个标志位 $[\omega_{i}]_{b}\in Fset_{b}$ 首先被转换为减法秘密共享份额 $\langle\omega_{i}\rangle_{b}$ 。然后，“先比较后交换”操作可以表示为 ：  

   $$\min(\langle m_{2i-1}\rangle_{b}, \langle m_{2i}\rangle_{b}) = (\langle m_{2i-1}\rangle_{b} - \langle m_{2i}\rangle_{b}) \cdot \langle\omega_{i}\rangle_{b} + \langle m_{2i}\rangle_{b}$$

   $$\max(\langle m_{2i-1}\rangle_{b}, \langle m_{2i}\rangle_{b}) = (\langle m_{2i}\rangle_{b} - \langle m_{2i-1}\rangle_{b}) \cdot \langle\omega_{i}\rangle_{b} + \langle m_{2i-1}\rangle_{b}$$

3. 我们的不透光 Top-$k$ 算法利用分治合并（divide-and-merge）结构来计算 Top-$k$ 元素，如图 3 所示 。在这种结构中，两个主要组件是排序（sorting）和 $k$ 次比较（在文献 [15] 中被称为 $k$-merge） 。显然，两者都可以通过“先比较后交换”的方法以不透光的方式实现 。  

具体而言，$\mathcal{S}_{b}$ 可以使用旋转 MSB 比较或基于 HE 的比较来获取共享的标志位集合 $Fset_{b}$ 。此后，$\mathcal{S}_{b}$ 采用公式 (5) 来计算极小值或极大值，以构建不透光排序和 $k$-merge，最终通过集成分治合并结构来实现不透光 Top-$k$ 算法 。  

通过将 PSTSA 方案中的 FTKS 协议替换为该不透光 Top-$k$ 算法，即可构建出我们的 SETSA 方案 。这种替换确保了在 Top-$k$ 选择过程中不再泄露任何比较结果，从而提供了增强的安全保护 。因此，我们的 SETSA 方案实现了安全的时序分析，使得云服务器在执行分析时无法推断出任何有用的信息 。  

这里是论文第六章 **VI. 安全分析 (SECURITY ANALYSIS)** 的纯中文高保真逐句译文。

# VI. 安全分析

在本节中，我们形式化地证明我们提出的方案能够满足预期的安全要求 。具体而言，我们首先证明所提出的构建块是安全的 。然后，我们利用复合定理（composition theorem）来分析所提出方案的安全性 。在进行具体安全分析之前，我们引入了一种基于模拟的范式（simulation-based paradigm），该范式提供了一种标准的“现实世界-理想世界”方法来阐明两方计算的安全性 。  

- 

  **现实世界（Realworld）**：现实世界通常定义为 $Real_{\Pi,\mathcal{A}}(x)$，表示在存在破坏了 $\mathcal{S}_{b}$ 的对手 $\mathcal{A}_{b}$ 且给定输入 $x$ 的情况下，执行协议 $\Pi$ 。令 $view_{\Pi}(x)$ 表示在执行协议 $\Pi$ 期间 $\mathcal{A}_{b}$ 的观测值 。那么，我们有 $Real_{\Pi,\mathcal{A}}(x)=view_{\Pi}(x)$ 。  

- 

  **理想世界（Idealworld）**：理想世界通常定义为 $Ideal_{\mathcal{F},Sim}(x)$，表示模拟器 $Sim_{b}$ 接收来自参与方 $\mathcal{S}_{b}$ 的所有输入并计算理想功能 $\mathcal{F}$ 。令 $Sim_{b}(x,f(x))$ 表示模拟器 $Sim_b$ 的视图（view），其中 $f(x)\in\mathcal{F}$ 。那么，我们有 $Ideal_{\mathcal{F},Sim}(x)=Sim_{b}(x,f(x))$ 。  

> **定义 1（对抗半诚实对手的安全性）**：一个协议 $\Pi$ 被称为在存在半诚实对手的情况下安全地计算了功能 $\mathcal{F}$，如果对于一个半诚实对手 $\mathcal{A}_{b}$ 和一个模拟器 $Sim_{b}$，$\mathcal{A}_{b}$ 在 $\Pi$ 的现实执行中的视图与它在使用 $Sim_{b}$ 的理想执行中的视图在计算上是不可区分的，即 $Real_{\Pi,\mathcal{A}}(x)\approx Ideal_{\mathcal{F},Sim}(x)$，其中 $\approx$ 表示计算上的不可区分性 。  

### A. 我们构建块的安全性

在本小节中，我们证明我们设计的构建块能够抵抗半诚实的对手 。  



**定理 1**：如果 NISED 协议在执行期间的视图（现实世界中的视图）与模拟器生成的视图（理想世界中的视图）不可区分，则该协议是安全的 。  

- **证明**：首先，我们展示如何利用随机选择的输入 $\{x^{\prime},y^{\prime}\}$ 构造模拟器 $Sim_b$：
  1. 接收 $\{\langle x^{\prime}\rangle_{b},s_{b}^{\prime},\langle(x^{\prime})^{2}\rangle_{b},\langle(y^{\prime})^{2}\rangle_{b},[[y^{\prime}]]\}$ ；  
  2. 根据 NISED 协议的功能在本地获取 $\{g_{b}^{\prime},z_{b}^{\prime},\langle\delta^{\prime}\rangle_{b}\}$ 。  
- 由于该协议中没有交互，因此 $Sim_{b}((x^{\prime},y^{\prime}),f(x^{\prime},y^{\prime}))$ 的视图等同于 $\{\langle x^{\prime}\rangle_{b},s_{b}^{\prime},\langle(x^{\prime})^{2}\rangle_{b},\langle(y^{\prime})^{2}\rangle_{b},[[y^{\prime}]],g_{b}^{\prime},z_{b}^{\prime},\langle\delta^{\prime}\rangle_{b}\}$ 。  
- 在现实世界中，根据我们的 NISED 协议，其视图为 $\{\langle x\rangle_{b},s_{b},\langle x^{2}\rangle_{b},\langle y^{2}\rangle_{b},[[y]],g_{b},z_{b},\langle\delta\rangle_{b}\}$ 。  
- 显同易见，区分理想世界和现实世界被归结为破解减法秘密共享和同态秘密共享 。然而，这两种技术的安全性已在文献 [20] 中得到了形式化证明 。因此，我们的 NISED 协议是安全的 。  



**定理 2**：如果 STM 协议在执行期间的视图（现实世界中的视图）与模拟器生成的视图（理想世界中的视图）不可区分，则该协议是安全的 。  

- 

  **证明**：在这里， $Sim_{1}$ 和 $Sim_{0}$ 具有不同的方法来实现 STM 协议的功能 。  

- 对于 $Sim_{1}$，其运行流程如下：

  1. 接收 $\{\langle x^{\prime}\rangle_{1},\langle y^{\prime}\rangle_{1},\langle w^{\prime}\rangle_{1}\}$ ；  
  2. 获取 $\phi^{\prime}$ ；  
  3. 通过基于 Beaver 三元组的乘法计算 $\{\rho_{1}^{\prime},\hat{\rho}_{1}^{\prime}\}$ ；  
  4. 在本地计算 $\{res_{1}^{\prime},\langle z^{\prime}\rangle_{1}\}$ 。  

- 因此，$Sim_{1}((x^{\prime},y^{\prime},w^{\prime}),f(x^{\prime},y^{\prime},w^{\prime}))$ 获得了三类视图：

  1. 

     $\{\langle x^{\prime}\rangle_{1}, \langle y^{\prime}\rangle_{1}, \langle w^{\prime}\rangle_{1}, res_{1}^{\prime}, \langle z^{\prime}\rangle_{1}\}$ ；  

  2. 

     $\{\rho_{1}^{\prime},\hat{\rho}_{1}^{\prime}\}$ ；  

  3. 

     $\phi^{\prime}$ 。  

- 在现实世界中，其视图也可以分为三类：

  1. 

     $\{\langle x\rangle_{1},\langle y\rangle_{1},\langle w\rangle_{1},res_{1},\langle z\rangle_{1}\}$ ；  

  2. 

     $\{\rho_{1},\hat{\rho}_{1}\}$ ；  

  3. 

     $\phi$ 。  

- 显而易见：

  1. 减法秘密共享的安全性保证了第一类视图是不可区分的 ；  
  2. 对于第二类视图，基于 Beaver 三元组的乘法确保了理想视图与现实视图是不可区分的 ；  
  3. 对于第三类视图，由于 $\phi$ 已被 $\{r_{1},r_{2},f\}$ 随机化，因此区分 $\phi^{\prime}$ 与 $\phi$ 等同于区分不同的随机值 。  

- 因此，我们的 STM 协议能够抵抗半诚实的对手 $\mathcal{S}_1$ 。  

- 类似地， $Sim_{0}$ 也拥有三类视图：

  1. 

     $\{\langle x^{\prime}\rangle_{0},\langle y^{\prime}\rangle_{0},\langle w^{\prime}\rangle_{0}, res_{0}^{\prime}, \langle z^{\prime}\rangle_{0}\}$ ；  

  2. 

     $\{\rho_{0}^{\prime},\hat{\rho}_{0}^{\prime}\}$ ；  

  3. 

     $E(\langle\sigma^{\prime}\rangle_{1})$ 。  

- 在现实世界中，其视图为：

  1. 

     $\{\langle x\rangle_{0},\langle y\rangle_{0},\langle w\rangle_{0},res_{0},\langle z\rangle_{0}\}$ ；  

  2. 

     $\{\rho_{0},\hat{\rho}_{0}\}$ ；  

  3. 

     $E(\langle\sigma\rangle_{1})$ 。  

- 前两类视图遵循与 $Sim_{1}$ 中讨论相同的不可区分性保证 。关于第三类视图，所采用的同态加密（即 Paillier 加密）的安全性确保了其不可区分性 。因此，我们的 STM 协议能够抵抗破坏了 $\mathcal{S}_{0}$ 的半诚实对手 $\mathcal{A}_{0}$ 。  



**定理 3**：FTKS 协议能够在公开比较结果 $\mathcal{L}$ 的情况下，安全地找出 Top-$k$ 元素，且不泄露原始的时间序列 。  

- 

  **证明**：回顾算法 1 和函数 3，FTKS 协议的核心组件是底层的 `SecComparison()` 函数（包括旋转 MSB 比较和基于 HE 的比较），以及基于公开比较结果 $\mathcal{L}=\{\omega_{i},\omega_{j},Fset\}$ 来选择极小值 。  

- 首先，`RotatedMSBComparison()` 函数对半诚实对手是安全的，因为它呈现的视图与原始 MSB 比较相同，而原始方法已被证明是安全的 。  

- 其次，正如文献 [12] 中所展示的，`HE-basedComparison()` 函数的安全性依赖于其底层的同态加密技术 。因此，如果所采用的同态加密是安全的，则 `HE-basedComparison()` 函数就能抵抗半诚实的对手 。  

- 第三，文献 [12] 中的研究已经证明，公开比较结果 $\mathcal{L}$ 不会导致时间序列的泄露 。因此，我们的 FTKS 协议可以安全地识别 Top-$k$ 元素，而不泄露时序数据 。  



**定理 4**：如果不透光 Top-$k$ 算法在执行期间的视图（现实世界中的视图）与模拟器生成的视图（理想世界中的视图）不可区分，则该算法是安全的 。  

- **证明**：在理想世界中， $Sim$ 的工作流程如下：
  1. 获得 $\langle d_{i}^{\prime}\rangle_{b}$ ；  
  2. 构建 $\{\langle m_{i}^{\prime}\rangle_{b},\langle m_{i}^{\prime}\rangle_{b}\}$ ；  
  3. 获得比较结果 $\{\langle\omega_{i}^{\prime}\rangle_{b},Fset_{b}^{\prime}\}$ ；  
  4. 计算出值 $\{\min(\langle m_{2i-1}^{\prime}\rangle_{b},\langle m_{2i}^{\prime}\rangle_{b}), \max(\langle m_{2i-1}^{\prime}\rangle_{b},\langle m_{2i}^{\prime}\rangle_{b})\}$ ；  
  5. 输出 $Kset$ 。  
- 在现实世界中，其视图为 $\{\langle d_{i}\rangle_{b},\langle m_{i}\rangle_{b},\langle m_{i}\rangle_{b},\langle\omega_{i}\rangle_{b},Fset_{b},\min(\langle m_{2i-1}\rangle_{b},\langle m_{2i}\rangle_{b}), \max(\langle m_{2i-1}\rangle_{b},\langle m_{2i}\rangle_{b}),Kset_{b}\}$ 。  
- 首先，减法秘密共享的安全性确保了难以区分 $\{\langle d_{i}^{\prime}\rangle_{b},\langle m_{i}^{\prime}\rangle_{b},\langle m_{i}^{\prime}\rangle_{b},Kset_{b}^{\prime}\}$ 和 $\{\langle d_{i}\rangle_{b},\langle m_{i}\rangle_{b},\langle m_{i}\rangle_{b},Kset_{b}\}$ 。  
- 其次，`RotatedMSBComparison()` 和 `HE-basedComparison()` 的安全性确保了比较结果 $\{\langle\omega_{i}\rangle_{b},Fset_{b}\}$ 与 $\{\langle\omega_{i}^{\prime}\rangle_{b},Fset_{b}^{\prime}\}$ 之间是不可区分的 。  
- 第三，由于 $\{\min(\langle m_{2i-1}\rangle_{b},\langle m_{2i}\rangle_{b}), \max(\langle m_{2i-1}\rangle_{b},\langle m_{2i}\rangle_{b})\}$ 是利用公式 (5) 计算得出的，因此基于 Beaver 三元组的乘法的安全性确保了它们难以与理想世界中的对应项区分开来 。因此，我们的不透光 Top-$k$ 算法是安全的 。  

### B. 我们提出方案的安全性

在本小节中，我们将分别为提出的 PSTSA 和 SETSA 方案提供独立的安全分析 。  



**定理 5**：如果 NISED、STM 和 FTKS 协议是安全的，则 PSTSA 方案能够保证半诚实的对手 $\mathcal{A}_{b}$ 无法推断出时间序列和查询令牌 。  

- 

  **证明**：从第五章 A 节可以获知，PSTSA 方案仅包含三个组件：NISED、STM 和 FTKS 协议 。时间序列 $\{x_{i}\}$ 和查询令牌 $\{y_{j}\}$ 在上传至云服务器之前，已被分别转换为 $\{x_{i,b} \mid i\in[1,n]\}$ 以及 $\{\langle y_{j}^{2}\rangle_{b},[[y_{j}]] \mid j\in[1,\beta]\}$ 。根据文献 [28] 中的复合定理，我们的 PSTSA 方案确保了时间序列和查询令牌在面对半诚实对手时是安全的 。  



**定理 6**：如果 NISED 协议、STM 协议以及不透光 Top-$k$ 算法是安全的，则 SETSA 方案能够保证即使是具有背景知识的对手 $\mathcal{A}_{b}$ 也无法推断出时间序列和查询令牌 。  

- 

  **证明**：从第五章 B 节可以获知，SETSA 方案由 NISED 协议、STM 协议以及不透光 Top-$k$ 算法组合而成 。如前所述，所有这些构建块都确保了计算过程的不透光性（obliviousness），这意味着对手在执行过程中无法提取出任何有意义的信息 。因此，即使对手拥有背景知识，也无法将其与特定的时间序列或查询令牌关联起来 。因此，我们的 SETSA 方案能够抵抗具备背景知识的更强对手 $\mathcal{A}_{b}$ 。  

这里是论文第七章 **VII. 性能评估 (PERFORMANCE EVALUATION)** 的纯中文高保真逐句译文。为了满足您的阅读习惯，本章包含的所有**图表（图 4 至 图 8 以及 表 II）的图注、轴标签及内部关键英文文本**，均已完整翻译并按照原文章节结构有机排版在一起，方便您随时对照原图查看。

# VII. 性能评估

在本节中，我们通过将提出的方案与最先进的方案（即 STDA [12]）进行对比，来评估其性能。为了确保公平对比，我们在相同的运行环境下实现了这两个方案。具体而言，实验是在一台配备了 AMD Ryzen 7 5800H（3.20 GHz）CPU 和 16.0 GB 内存、运行 Ubuntu 20.04 系统的本地机器上执行的。我们使用 C++ 语言实现了这两个方案，并利用了著名的 GMP 库（版本 6.2.1）和 NTL 库（版本 11.5.1）。

为了模拟现实世界中的网络延迟，我们分别构建了局域网（LAN）环境和广域网（WAN）环境。在局域网环境下，两台云服务器之间的通信往返时延（RTT）由于在本地运行而可以忽略不计；而在广域网环境下，我们通过网络控制工具（Linux traffic control）将往返时延设置为 40 毫秒，以模拟跨区域的云服务器交互。此外，对于同态加密技术，其安全参数设置为 1024 比特，这在现有的安全云计算方案中被广泛采用。最后，我们将感知时序数据的最大比特长度（即 $l$）设置为 16 比特，同时，在不失一般性的情况下，我们将时间序列的长度分别固定为 $\alpha=10$ 和 $\beta=10$。

### A. 构建块的实验评估

在本小节中，我们对第四章中提出的三个主要构建块（即 NISED、STM 和 FTKS 协议）进行全面的实验评估。

#### 1) NISED 协议的评估

如前所述，由于我们的 NISED 协议在局部距离矩阵的计算过程中实现了零交互，因此该协议的通信开销和往返时延完全为零。这一特性在图 4 中得到了显著体现，我们的方案在通信量和延迟方面均达到了绝对的零消耗。

- **【图 4 图注与文本排版对照】**
  - **图 4 标题**：在计算局部距离矩阵时，NISED 协议与现有 STDA 协议的性能对比。
  - **(a) 纵轴标签**：通信开销 (MB)
  - **(b) 纵轴标签**：时间延迟 (毫秒)
  - **横轴标签（两图通用）**：数据规模 (时间序列数量 $n$)
  - **图例**：STDA 方案（折线）、NISED 方案（折线）

而在计算时间（Computation Time）方面，图 5 的实验结果表明，随着数据规模 $n$ 从 100 增加到 500，我们的 NISED 协议展现出了巨大的优势。具体而言，当 $n=500$ 时，STDA 协议需要消耗 2073 毫秒，而我们的 NISED 协议仅需要 94 毫秒。这意味着我们的方案实现了大约 **22 倍的计算性能提升**。这一显著的改进主要得益于我们对同态秘密共享（HSS）架构进行的简化和定制。

- **【图 5 图注与文本排版对照】**
  - **图 5 标题**：NISED 协议与现有的 STDA 协议在局部距离矩阵计算时间上的对比。
  - **纵轴标签**：计算时间 (毫秒)
  - **横轴标签**：数据规模 (时间序列数量 $n$)
  - **图例**：STDA 方案（柱状）、NISED 方案（柱状）

#### 2) STM 协议的评估

表 II 详细展示了我们的 STM 协议与 STDA 中基于同态加密的极小值处理方法在各个核心指标上的对比。在此评估中，我们独立执行了单次三元组极小值的计算。

- **【表 II 标题与内容排版对照】**
  - **表 II 标题**：STM 协议与现有 STDA 方案中极小值算法的开销对比。

| **评估指标**                            | **现有的 STDA 方案 [12]** | **我们提出的 STM 协议** |
| --------------------------------------- | ------------------------- | ----------------------- |
| **计算时间 (Computation Time)**         | 2.56 毫秒                 | 0.96 毫秒               |
| **通信轮数 (Communication Rounds)**     | 8 轮                      | 8 轮                    |
| **同态密文数量 (HE Ciphertexts)**       | 16 个                     | 4 个                    |
| **密文传输体量 (HE Ciphertext Vol.)**   | 4096 字节                 | 1024 字节               |
| **外加元素传输量 (Extra Element Vol.)** | 0 字节                    | 48 字节                 |

从表中可以获知，在保持相同通信轮数（均为 8 轮）的前提下，我们的 STM 协议将单次计算时间从 2.56 毫秒缩短至 0.96 毫秒，实现了 **2.6 倍的计算提速**。同时，在密文传输方面，我们的方案仅需传输 4 个同态密文，比 STDA 减少了 75%。虽然我们的方案在秘密共享域上引入了 48 字节的微量额外元素传输，但由于这部分数据体量极小，总体网络通信量依然得到了极大的优化。这充分印证了我们通过混合模型将多种密码学原语进行优势互补的设计有效性。

#### 3) FTKS 协议的评估

为了更清晰地展示我们提出的快速 Top-k 协议（FTKS）的性能改进，我们将其与 STDA 采用的基于最小堆的安全 Top-k 算法进行了全方位的对比。在实验中，我们将数据规模 $n$ 固定为 500，并观察 Top-k 选择中 $k$ 值在 $\{5, 10, 15, 20, 25\}$ 范围变化时对性能的影响。

图 6(a) 和 6(b) 分别展示了在局域网（LAN）和广域网（WAN）环境下，FTKS 协议在总时间延迟（Time Delay）方面的巨大飞跃。在局域网环境下，当 $k=25$ 时，STDA 需要耗时 4220 毫秒，而我们的 FTKS 协议仅需 3 毫秒，计算效率提升了 **1400 倍**。在广域网环境下，由于网络延迟占主导，STDA 的延迟高达 130 毫秒，而我们的 FTKS 协议成功将其压低至 64 毫秒。这证明了我们基于旋转的混合比较模式能够成功跨越网络性能瓶颈。

- **【图 6 图注与文本排版对照】**
  - **图 6 标题**：FTKS 协议与现有的 STDA 方案在总时间延迟上的对比。
  - **(a) 局域网环境 (LAN)**：纵轴为 时间延迟 (毫秒)
  - **(b) 广域网环境 (WAN)**：纵轴为 时间延迟 (毫秒)
  - **横轴标签（两图通用）**：Top-$k$ 选择中的参数 $k$ 值
  - **图例**：STDA 方案（折线）、FTKS 方案（折线）

图 7 进一步揭示了 FTKS 协议在网络通信量（Communication Volume）上的绝对优势。随着 $k$ 值的增长，STDA 的通信量呈现显著上升趋势，在 $k=25$ 时达到了 3.9 MB。相比之下，我们的 FTKS 协议在整个过程中表现极其平稳，通信开销仅为 0.05 MB。这表明我们的方案实现了高达 **78 倍的通信量削减**，有力支撑了大数据量下的低带宽应用。

- **【图 7 图注与文本排版对照】**
  - **图 7 标题**：FTKS 协议与现有的 STDA 方案在通信量上的对比。
  - **纵轴标签**：通信开销 (MB)
  - **横轴标签**：Top-$k$ 选择中的参数 $k$ 值
  - **图例**：STDA 方案（折线）、FTKS 方案（折线）

### B. 整体方案的实验评估

在本小节中，我们对所提出的完整方案（PSTSA 和安全增强型的 SETSA 方案）进行整体性能评估。在本轮实验中，我们将 Top-k 参数固定为 $k=10$，数据提供方的总体规模 $n$ 在 100 到 500 之间动态变化。

图 8(a) 展示了在局域网（LAN）环境下整体执行时间的对比。实验数据表明，当 $n=500$ 时，现有的 STDA 方案总耗时为 5484 毫秒。相比之下，我们的 PSTSA 方案仅需 535 毫秒，整体性能提升了 **10.2 倍**。即使是考虑了最强背景知识对手、实现了全流程数据和标志位隐匿的不透光 SETSA 方案，其总耗时也仅为 3144 毫秒，同样显著优于最先进的现有公开方案。

- **【图 8 图注与文本排版对照】**
  - **图 8 标题**：我们提出的完整方案（PSTSA, SETSA）与现有 STDA 方案在整体总时间延迟上的对比。
  - **(a) 局域网环境 (LAN)**：纵轴为 整体延迟 (毫秒)
  - **(b) 广域网环境 (WAN)**：纵轴为 整体延迟 (毫秒)
  - **横轴标签（两图通用）**：数据规模 (时间序列数量 $n$)
  - **图例**：STDA 方案（折线）、SETSA 方案（折线）、PSTSA 方案（折线）

图 8(b) 阐明了在广域网（WAN）环境下的整体时间延迟。当数据规模 $n=500$ 且存在 40 毫秒的网络硬延迟时，STDA 方案的开销增加到了 5842 毫秒。此时，我们的 PSTSA 方案凭借极低的通信轮数和非交互式欧氏距离模块，将总延迟控制在 671 毫秒，保持了 **8.7 倍的综合性能跨越**。这些全面的实验结果强有力地证明，无论是在高带宽的局域网内部，还是在具有网络延迟挑战的广域网云端，本研究所提出的混合模型时序分析方案都具备极高的实用价值与卓越的性能表现。

第七章完整纯中文高保真译文（含所有图表排版）如上。接下来将为您翻译 **VIII. 相关工作 (RELATED WORK)**，请随时示意。



# VIII. 相关工作

由于安全时间序列分析能够在执行时序分析的同时保护数据安全，因此它吸引了极大的关注 。根据所针对的功能性，相关工作大致可以分为两类 。  

### 1) 时序数据上的安全聚合

针对时序数据上的安全聚合（secure aggregation），Shi 等人 [8] 引入了一种基于决策 Diffie-Hellman（DDH）的方案，用于计算来自不同参与方的加密时间序列之和 。然而，它在解密时需要搜索明文域，从而将明文限制在了一个有限的范围内 。为了克服这个问题，Joye 等人 [36] 提出了一种基于复合剩余假设的安全聚合方案，实现了无需搜索明文空间即可直接解密。此外，Benhamouda 等人  [37] 构建了具有加法同态特征的光滑射影哈希，并采用它优化了文献 [36] 研究中的密文大小。  

通过采用一种定制的对称同态流加密（SHSE），Burkhalter 等人 [38] 实现了单用户和多用户时序数据的加法聚合 。基于该 SHSE 技术，一种被称为 TimeCrypt 的新颖安全聚合系统 [39] 被提出，以此额外支持访问控制和数据完整性。Harvan 等人 [40] 尝试支持更丰富的聚合函数，例如最大值 $\max()$ 和最小值 $\min()$ 。然而，由于使用了保留顺序加密，它面临着安全隐患 。为了增强安全性，Faisal 等人 [41] 采用了一种三服务器模型，在确保时间值机密性的同时，实现跨多个时间窗口的安全聚合 。  

利用类似的三服务器模型，Dauterman 等人 [42] 将功能秘密共享与复制秘密共享相结合，构建了一种能够支持丰富聚合函数的不透光聚合方案。然而，上述所有方案都聚焦于数据聚合，无法用于评估时间序列之间的相似度 。  

### 2) 时序数据上的安全相似度分析

在 2014 年，Zhu 等人 [9] 提出了一种利用两方计算的时序数据隐私保护相似度分析方案 。尽管该方案可以支持 DTW 计算，但它是为明文时间序列设计的，而不是针对我们工作中所考虑的“加密”时间序列 。Zheng 等人 [10] 采用安全两方计算，在加密时间序列上实现了一种安全的 TWED 方案。不幸的是，由于其中一台服务器持有私钥，它面临着密钥托管的风险 。Liu 等人 [11] 引入了一种基于加法秘密共享的安全 DTW 方案，该方案自然避免了托管风险 。  

然而，正如文献 [12] 中指出的，它在现实应用中存在性能问题 。最近，文献 [12] 利用基于门限 Paillier 的两方计算，提出了一种高效的安全时间序列分析方案。与先前的安全 DTW 方案相比，该方法显著减少了通信轮数 。然而，经过仔细的研究，我们发现安全时间序列分析的性能在计算和通信两方面都可以实现里程碑式的重大提升 。  

# IX. 结论

在这项工作中，我们通过一条新的技术路线，提出了一种具有卓越性能的实用安全时间序列分析方案（PSTSA），以及一种旨在抵抗更强对手的安全增强型方案（SETSA） 。  

具体而言：

- 我们首先通过定制同态秘密共享（HSS）使其适配减法秘密共享，设计了一种非交互式安全欧氏距离协议，同时确保了计算效率 。  
- 然后，我们通过无缝混合不同的原语设计了一种安全三元组极小值协议，从而同时降低了计算和通信开销 。  
- 随后，我们引入了一种基于旋转的混合比较模式，并通过将该新比较模式与二叉树结构相结合，提出了一种共享数据快速 Top-k（FTKS）协议 。  
- 此外，我们采用我们的混合比较模式来优化最先进的安全 Top-k 解决方案，并提出了一种高效的不透光 Top-k 算法 。  
- 最后，基于这些安全协议，我们构建了我们的安全时间序列分析方案 。  

安全分析证实，我们提出的方案能够达到所需的安全保证；同时，实验评估表明，本研究在计算和通信效率上均实现了实质性的巨大提升 。  



