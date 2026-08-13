【Kona 实验统一计时、通信统计与 CPU 资源规范】

后续新增或修改任何 benchmark / performance test 时，必须遵守以下实验规范。
除非任务明确要求改变口径，否则不得自行改变。

==================================================
一、Online / Setup 的统一定义
==================================================

论文主性能指标统一使用 Online Runtime / Online Latency。

Online timer 的起点定义为：

1. 双方/两服务器进程已经启动；
2. socket/network connection 已经建立；
3. benchmark 所需输入已经读取并解析；
4. secret-shared inputs 已经构造完成；
5. 所有 query-independent preprocessing materials 已经生成并加载到内存；
6. DCF keys、Beaver triples、PCR preprocessing materials、
   Kona Euclidean triples/masks 等均已经 ready in memory。

满足以上条件后，才允许：

    comm_before = player->total_comm();
    online_start = now();

Online timer 的终点定义为：

    当前协议要求的 secret-shared output 已经生成。

然后立即：

    online_end = now();
    comm_after = player->total_comm();

因此：

    online_ms = online_end - online_start

通信统计必须严格使用：

    online_comm = comm_after - comm_before

Timer 和 communication statistics 必须使用完全相同的协议边界。


==================================================
二、Online timer 中必须包含的内容
==================================================

只要某个操作是一次真实在线查询执行协议所必须发生的，
就必须计入 Online Runtime。

包括但不限于：

- secure Euclidean distance computation
- secure comparison
- PCR / DCF / Rotated MSN 等 comparator 在线计算
- A2B / B2A（如果在线协议需要）
- Beaver-style multiplication 在线阶段
- DCF Eval
- secure swap
- comparison network execution
- Cong / Kona / Matrix Top-k
- vote / winner selection
- online message packing / unpacking
- send / receive
- online local computation
- batch slicing / batch orchestration
- network-level orchestration
- 在线阶段必要的内存访问和 preprocessing material consumption

不得为了降低 benchmark 时间，
把协议在线必需的本地计算、packing、batch handling、
比较、转换、交换等提前到 timer 外。


==================================================
三、Online timer 中不得包含的内容
==================================================

以下属于 setup / preprocessing / engineering overhead，
默认不计入论文 Online Runtime：

- CSV / dataset 文件读取
- dataset parsing
- 明文数据预处理
- socket/server/network initialization
- 程序启动
- DCF key generation
- DCF key/material 文件加载
- Beaver triple generation
- Beaver triple 文件加载
- PCR preprocessing material generation/loading
- Kona Euclidean triple/mask generation/loading
- 其他 query-independent preprocessing generation/loading
- correctness reveal/open
- correctness checking
- benchmark result CSV 写入
- benchmark log/output 文件写入

如果需要测量这些成本，应单独输出：

    setup_ms
    dcf_key_init_ms
    triple_load_ms
    preprocessing_ms
    read_ms
    share_setup_ms
    benchmark_wall_ms

不得把这些字段称为 online_ms。


==================================================
四、Preprocessing material 的统一原则
==================================================

论文 Online Runtime 默认采用：

    all query-independent preprocessing materials
    are already resident in memory.

即：

DCF:
    提前准备 DCF materials
    → load into memory
    → TIMER START
    → online Eval / material consumption

Beaver/PCR:
    提前准备 triples
    → load into memory
    → TIMER START
    → online multiplication / triple consumption

Kona ESD:
    提前准备 Euclidean preprocessing materials
    → load into memory
    → TIMER START
    → online distance computation

如果 benchmark 为性能简化而复用同一 preprocessing material
或使用 zero/fake preprocessing material，可以保留，
但必须满足：

1. 真实在线协议动作没有被删除；
2. 应发生的乘法、Eval、packing、send、receive、
   reconstruction、本地计算仍然执行；
3. 注释明确说明这是 benchmark-oriented simplification；
4. 不得声称这种 material reuse / zero material 是正式安全部署方案。


==================================================
五、端到端实验的统一边界
==================================================

End-to-End Online Query 的推荐边界：

    secret-shared query/database inputs ready
                    ↓
              TIMER START
                    ↓
          secure distance computation
                    ↓
             secure Top-k
       (compare + conversion + swap)
                    ↓
          vote / result-share generation
                    ↓
              TIMER STOP

CSV loading、share generation、preprocessing material loading
均不进入该 Online Query Time。

如果实验只测试 Top-k，则：

    secret-shared distances ready
            ↓
        TIMER START
            ↓
          Top-k
            ↓
    output shares ready
            ↓
        TIMER STOP

如果只测试 comparator，则：

    comparison input shares ready
            ↓
        TIMER START
            ↓
    compare + required conversions
            ↓
 arithmetic comparison shares ready
            ↓
        TIMER STOP


==================================================
六、组件 benchmark 与端到端 benchmark 分离
==================================================

必须明确区分：

A. End-to-End benchmark
用于论文主性能表。

B. Component / Ablation benchmark
例如：

- Distance
- PCR
- DCF
- Rotated MSN
- Secure Swap
- Cong Top-k
- Matrix Top-k

组件 benchmark 不得冒充完整方案 Online Query Time。

如果同一个程序串行运行多个独立 benchmark，例如：

    pair-batched ESD
    query-batched ESD
    all-pairs microbenchmark

则整程序时间只能称为：

    benchmark_wall_seconds

不得称为：

    online_seconds
    query_seconds
    total_online_seconds


==================================================
七、通信量和轮次统计
==================================================

所有性能 benchmark 必须至少报告：

    online_ms
    sent_bytes
    transport_rounds

如果当前实现采用：

    send()
    receive()

作为一次双向协议交互，可以额外报告：

    logical_rounds

但必须明确：

    logical_rounds 是 implementation-level logical interactions

不得未经证明直接把：

    transport_rounds / 2

称为理论 cryptographic round complexity。

如果存在 chunking：

    batch > chunk_size

导致一个逻辑 primitive 被拆成多个 send/receive，
必须保留实际增加的 transport rounds。

不得为了匹配理论轮数而隐藏 chunking 产生的通信。


==================================================
八、单进程单核 CPU 规范
==================================================

两服务器实验统一采用：

    P0 = 一个独立进程
    P1 = 一个独立进程

每个 party 独占一个 CPU core。

例如：

    P0 → CPU core 0
    P1 → CPU core 1

推荐通过外部 taskset 控制：

    taskset -c 0 ./program.x 0 ...
    taskset -c 1 ./program.x 1 ...

单机模拟两服务器时，P0 和 P1 必须绑定到不同物理 core。
如果机器开启 SMT / 超线程，正式实验应优先选择不同物理 core，
不得把 P0 和 P1 绑定到同一物理 core 的两个 logical CPU sibling。

禁止：

    P0 和 P1 同时绑定到同一个 core

因为这会造成 CPU contention，
不等价于“两台单核服务器”。

也不要在协议源码中写死 CPU affinity。
CPU affinity 应由 benchmark 启动脚本统一控制。


==================================================
九、禁止隐藏多线程
==================================================

“单进程单核”意味着每个 party 在 benchmark 期间
只能实际使用一个 CPU core。

新增 benchmark 或调用第三方库时必须检查是否存在：

- OpenMP
- pthread worker pool
- TBB
- Rayon
- Eigen internal threading
- OpenBLAS / MKL threading
- OpenFHE internal parallelism
- std::async / worker threads
- 其他隐式线程池

如果存在，应在实验启动环境或配置中限制为 1 thread。

不要仅因为程序只有：

    P0
    P1

两个进程，就声称实验是 single-core。

进程数和 CPU core 使用数是两个不同概念。


==================================================
十、CPU affinity 的验证
==================================================

benchmark 启动脚本应打印或记录：

    P0 PID
    P1 PID
    P0 CPU affinity
    P1 CPU affinity

至少在正式实验开始前验证：

    P0 只能运行在指定 core
    P1 只能运行在另一个指定 core

如果需要，可以使用：

    taskset -pc <PID>

或等价方法验证。

正式实验报告中的“single-core”必须指：

    one dedicated CPU core per server/party process.


==================================================
十一、Benchmark 修改原则
==================================================

修改 benchmark 时：

1. 不得为了优化结果修改密码协议；
2. 不得删除真实在线动作；
3. 不得把原本在线发生的计算偷偷移到 timer 外；
4. 不得为了减少 rounds 合并存在数据依赖的 sequential rounds；
5. 同一对比实验必须采用相同 timer 定义；
6. timer 和 communication before/after 必须同边界；
7. setup 必须单独计；
8. correctness reveal 必须在 timer 外；
9. benchmark simplification 必须写注释；
10. 如果不确定某个动作属于 online 还是 setup，不要自行决定，
    在修改前指出并询问。


==================================================
十二、每次新增 benchmark 后必须自检
==================================================

完成代码后必须报告：

1. Online timer 起点；
2. Online timer 终点；
3. 哪些 setup 被排除；
4. communication before/after 的位置；
5. 是否使用 preprocessing material；
6. preprocessing material 是否复用/模拟；
7. 是否存在 chunking；
8. 是否存在多线程；
9. P0/P1 是否可以通过 taskset 分别绑定独占核心；
10. correctness 是否通过；
11. 实际输出的 sent_bytes；
12. 实际输出的 transport_rounds；
13. 理论通信量是否与实测基本一致。

如果修改已有 benchmark，还必须给出 git diff，
并明确说明是否改变了任何协议行为。


==================================================
最终实验原则
==================================================

统一目标不是“让 benchmark 尽可能快”，而是：

    相同硬件资源
    + 相同 CPU 限制
    + 相同网络条件
    + 相同 Online 定义
    + 相同 preprocessing 边界
    + 完整保留真实在线协议动作

在此前提下比较各方案性能。
