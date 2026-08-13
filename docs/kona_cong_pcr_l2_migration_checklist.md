# Kona Cong + PCR L2 Migration Checklist

本文档基于 `docs/kona_primitives_audit.md`、`references/cong/cong_topk_no_wrap_simple.py` 和 `references/pcrreduce/`，给出 Kona 项目改造成“Cong + PCR 化简版”的实施清单。

这里的 L2 口径是：保证协议步骤、数据流、接口和数学逻辑切合后续真实安全实现；允许用本地重构、固定零三元组、复用随机数、`a=b=c=0` 这类方式先跑通正确性。也就是说，L2 是“协议形状正确 + 程序结果正确”的阶段，不以真实隐私安全为验收标准。

## 0. 总目标

把当前 Kona 的 Top-k 路径从“反复 top_1 + DCF 比较 + SS_vec 交换”改造成：

```text
ESD additive shares
    -> 转成 Cong 使用的 subtractive shares 视图
    -> Cong 固定比较网络逐层执行
    -> 每层调用 PCR L2 比较得到 swap bit
    -> 用 Kona 现有 SS_vec 公式做条件交换
    -> 输出最小 k 个 distance,label pair
```

目标不是一次性替换所有密码学底层，而是先形成清晰可测的模块边界。后续把 L2 的 cheat compare、cheat AND、zero Beaver 替换成真实协议时，上层 Cong 网络和排序交换逻辑不需要大动。

## 1. 现状依据

当前 Kona 可复用的核心事实：

- `secure_compare(x1,x2,true)` 语义是 `[x1 > x2]`，相等输出 `0`，见 `docs/kona_primitives_audit.md` 第 2 问。
- `compare_in_vec()` 输出是 `Z2<64>` 算术加法份额，不是 XOR 份额，见审计第 3 问。
- `SS_vec()` 已经实现按 secret bit 同时交换 `(distance,label)` 的公式，见审计第 6、7 问。
- `mul_additive()` / `mul_vector_additive()` 当前虽然不是密码学安全乘法，但 L2 阶段可以继续接受这种“有操作步骤即可”的形态，见审计第 4 问。
- Cong reference 已经把 Top-k 主循环拆成“公开网络 + 每层 compare-and-swap 函数”，关键接口在 `references/cong/cong_topk_no_wrap_simple.py:220-322`。
- PCR reference 给出了 64-bit 最终进位比较器的结构：输入 subtractive shares，输出 XOR Boolean shares，`lt = 1 <=> x < y`，见 `references/pcrreduce/pcr_final_carry_reduce_uint64.py:7-18`、`430-551`。

## 2. L2 版本边界

L2 允许做的事情：

- 允许在比较内部重构明文差值再重新分享比较 bit。
- 允许 Boolean AND 用 `reconstruct -> and -> reshare`。
- 允许 A2B/B2A 用本地重构桥接。
- 允许随机份额固定为 `0` 或使用简单 PRNG。
- 允许 `mul_vector_additive()` 继续使用当前 `a=b=c=0` 的 Beaver-like 形状。
- 允许先只支持 64-bit signed/no-wrap 数据范围。

L2 暂不解决的事情：

- 不保证比较输入在真实部署中不泄漏。
- 不保证 Beaver triple 新鲜性。
- 不保证 DCF alpha 或 PCR triple 不复用。
- 不实现真实 `prepare / exchange / finalize` 网络轮次拆分。
- 不处理恶意安全、MAC、认证份额。

一句话：L2 先搭“能替换成安全协议的管线”，不追求这一步本身达到最终论文安全级别。

## 3. 建议新增模块

### 3.1 Cong 网络模块

建议文件：

```text
Machines/kona-cong.hpp
Machines/kona-cong.cpp
```

职责：

- 根据公开 `n` 和 `k` 生成 Cong 固定比较网络。
- 每一层返回互不冲突的比较对 `(left,right)`。
- 保持和 reference 一样的接口思想：网络生成和 compare-and-swap 解耦。

核心接口建议：

```cpp
using ComparePair = std::array<int, 2>;
using CongLevel = std::vector<ComparePair>;

std::vector<CongLevel> build_cong_network(int n, int k);
std::vector<int> build_cong_output_wires(int n, int k);
```

L2 可接受简化：

- 可以先把 `references/cong/cong_topk_no_wrap_simple.py` 的递归逻辑翻译成 C++。
- 可以先不做 GPU。
- 可以先只做 `n >= 1`、`1 <= k <= n` 的基础校验。

验收：

- 对 `n=16,k=3`，比较器数量应能对齐 reference 的 `35`，层数对齐 `9`，见 `references/cong/cong_topk_no_wrap_simple.py:330-339`。
- 随机明文数组排序后，输出最小 `k` 个元素正确。

### 3.2 PCR L2 比较模块

建议文件：

```text
Machines/kona-pcr-l2.hpp
Machines/kona-pcr-l2.cpp
```

职责：

- 提供 `[x < y]` 或 `[x > y]` 的 L2 比较接口。
- 对外尽量返回 Kona 当前可消费的 arithmetic share bit `Z2<64>`。
- 内部保留 PCR 的“subtract -> final carry -> lt bit”步骤形状。

核心接口建议：

```cpp
Z2<K> pcr_compare_lt_l2(Z2<K> x_share, Z2<K> y_share, RealTwoPartyPlayer* player);
Z2<K> pcr_compare_gt_l2(Z2<K> x_share, Z2<K> y_share, RealTwoPartyPlayer* player);

void pcr_compare_gt_vec_l2(
    const std::vector<std::array<Z2<K>, 2>>& shares,
    const std::vector<int>& compare_idx_vec,
    std::vector<Z2<K>>& compare_res,
    RealTwoPartyPlayer* player);
```

L2 内部步骤：

1. 双方交换 `x_share` 和 `y_share`。
2. 本地重构 `x = x0 + x1`、`y = y0 + y1`。
3. 计算 `lt = (x < y) ? 1 : 0`。
4. 如果需要 `gt`，计算 `gt = (x > y) ? 1 : 0`，相等时为 `0`。
5. 重新分享成 arithmetic share bit：L2 可令 party 0 返回 `0`，party 1 返回 `bit`；也可以随机拆分。
6. vector 接口按 pair 填充 `compare_res[2*i]` 和 `compare_res[2*i+1]`，保持 `SS_vec()` 需要的重复布局。

PCR 形状保留版步骤：

1. 把加法份额重构值临时转换为 subtractive 视图：`x = x1_sub - x0_sub`、`y = y1_sub - y0_sub`。
2. 构造比较参考里的 `left_signed = x1_sub - y1_sub`、`right_signed = x0_sub - y0_sub`。
3. 转成保序 uint64：`left = signed_to_ordered_uint64(left_signed)`、`right = signed_to_ordered_uint64(right_signed)`。
4. 计算 `left + (~right) + 1` 的最终 carry。
5. `lt = NOT carry64`，对应 reference 中 `lt=1 <=> x<y`。
6. L2 可直接用 C++ 明文算 carry；也可以实现 bitwise prefix 版本作为更贴近 PCR 的中间态。

建议先做的版本：

- 第一版先使用“直接重构比较”。
- 第二版再实现 `xor_share64`、`and_l2_cheat`、`bitwise_add64_l2`，把 PCR 的 carry 过程补完整。
- 上层接口不变，避免返工。

### 3.3 Boolean share L2 工具模块

建议文件：

```text
Machines/kona-boolean-l2.hpp
```

职责：

- 模拟 XOR share 的基础操作，服务 PCR。

核心数据结构：

```cpp
struct XorShare64 {
    uint64_t s0;
    uint64_t s1;
};

struct XorBitShare {
    uint8_t s0;
    uint8_t s1;
};
```

核心函数：

```cpp
uint64_t reconstruct_xor64(XorShare64 x);
XorShare64 share_xor64_l2(uint64_t x);
XorShare64 xor_share64(XorShare64 a, XorShare64 b);
XorShare64 and_share64_l2(XorShare64 a, XorShare64 b);
XorShare64 or_share64_l2(XorShare64 a, XorShare64 b);
XorShare64 shift_left_share64(XorShare64 a, int offset);
XorBitShare bit_share64(XorShare64 a, int bit);
```

L2 可接受实现：

- `share_xor64_l2(x)` 可以返回 `{0, x}`。
- `and_share64_l2(a,b)` 可以重构 `a`、`b`，计算 `a & b`，再 `{0, result}` 分享。
- `or_share64_l2(a,b)` 可用 `a XOR b XOR (a AND b)`，也可 L2 重构后直接 OR。

### 3.4 Arithmetic/Boolean bridge

建议文件：

```text
Machines/kona-conversion-l2.hpp
```

职责：

- 给 PCR L2 提供 A2B/B2A 的接口形状。
- 现在用重构作弊，未来可替换为 daBit/edaBit 或 masked conversion。

核心接口：

```cpp
XorShare64 arithmetic_to_xor_l2(Z2<K> x_share, RealTwoPartyPlayer* player);
Z2<K> xor_bit_to_arithmetic_l2(XorBitShare bit_share, RealTwoPartyPlayer* player);
Z2<K> public_bit_to_arithmetic_l2(uint64_t bit, int playerno);
```

L2 规则：

- A2B：交换 additive share，重构 `x`，返回 XOR share `{0,x}`。
- B2A：重构 XOR bit，返回 arithmetic share `0/bit`。
- `public_bit_to_arithmetic_l2(bit, playerno)`：party 0 返回 `0`，party 1 返回 `bit`。

## 4. 改造主路径

### Step 1: 保留 ESD 输出格式

现有 `KNN_party_optimized::compute_ESD_for_one_query()` 继续输出：

```cpp
m_ESD_vec[i][0] = distance additive share;
m_ESD_vec[i][1] = label additive share;
```

这正好是 Cong compare-and-swap 需要的 `(key,payload)` 数组。L2 阶段不必先动 ESD。

### Step 2: 新增 Cong Top-k 执行函数

建议在 `KNN_party_base` 或独立 helper 中新增：

```cpp
void cong_top_k_l2(
    std::vector<std::array<Z2<K>,2>>& shares,
    int n,
    int k,
    bool min_k);
```

执行逻辑：

```text
levels = build_cong_network(n,k)

for each level:
    compare_idx_vec = flatten(level)
    compare_res.resize(compare_idx_vec.size())
    pcr_compare_gt_vec_l2(shares, compare_idx_vec, compare_res, player)
    SS_vec(shares, compare_idx_vec, compare_res)

copy/select output_wires as final top-k positions
```

注意：

- 如果 `SS_vec()` 假设 compare pair 原地交换，Cong 网络也可以原地执行。
- 如果 Cong reference 的 `output_wires` 不等于最后连续 `k` 个位置，就需要最后按 `output_wires` gather 一次。
- 为了少改 Kona 主流程，第一版可以把 `output_wires` 对应结果搬到 `m_ESD_vec` 末尾 `k` 个位置，沿用后续 label_compute 逻辑。

### Step 3: 替换当前 repeated `top_1`

当前优化路径是：

```cpp
for(int i=0;i<k_const;i++){
    top_1(m_ESD_vec,num_train_data-i,true);
}
```

建议 L2 替换为：

```cpp
cong_top_k_l2(m_ESD_vec, num_train_data, k_const, true);
```

替换后保持：

```cpp
for(int i=0;i<k_const;i++)
    shares_selected_k.push_back(m_ESD_vec[m_ESD_vec.size()-1-i][1]);
```

如果新函数选择把 top-k 放在前 `k` 个位置，则这段也要改成从前 `k` 个读。建议第一版为了少改代码，把 top-k 搬到末尾。

### Step 4: label vote 暂时不动

`label_compute()` 继续使用现有 `compare_in_vec()` 和 `mul_vector_additive()`。

原因：

- 用户目标是 Cong + PCR Top-k 改造，最主要替换距离排序比较。
- label vote 目前 k 很小，先保留能减少变量。
- 后续第二阶段可以再把 label equality 的比较也替换成 PCR L2。

## 5. PCR L2 详细算法建议

### 5.1 最简单正确性版

这是建议先落地的版本。

```cpp
Z2<K> pcr_compare_gt_l2(Z2<K> x_share, Z2<K> y_share, RealTwoPartyPlayer* player)
{
    // 1. exchange shares
    // 2. reconstruct x,y by addition mod 2^64
    // 3. interpret as signed/no-wrap value if needed
    // 4. bit = (x > y)
    // 5. return party0=0, party1=bit
}
```

vector 版：

```cpp
for i in 0..pair_count-1:
    left = shares[compare_idx_vec[2*i]][0]
    right = shares[compare_idx_vec[2*i+1]][0]
    bit = pcr_compare_gt_l2(left, right, player)
    compare_res[2*i] = bit
    compare_res[2*i+1] = bit
```

优点：

- 最快接入 Cong。
- 和现有 `SS_vec()` 完全兼容。
- 相等时自然输出 `0`。

缺点：

- PCR 的内部 carry 结构还没体现。

### 5.2 PCR 形状版

第二版把比较内部改成：

```text
x_lt_y_l2(x_share,y_share):
    x_xor = arithmetic_to_xor_l2(x_share)
    y_xor = arithmetic_to_xor_l2(y_share)
    diff = bitwise_sub_or_final_carry_l2(x_xor,y_xor)
    lt_xor_bit = NOT carry64
    return xor_bit_to_arithmetic_l2(lt_xor_bit)
```

也可以直接按 reference 的 subtractive 比较接口：

```text
compare_subtractive_uint64(x0,x1,y0,y1):
    left_signed = x1 - y1
    right_signed = x0 - y0
    left = order_preserving_uint64(left_signed)
    right = order_preserving_uint64(right_signed)
    carry64 = final_carry(left + ~right + 1)
    lt = carry64 XOR 1
```

对应 reference：

- `x = x1 - x0`、`y = y1 - y0`，见 `references/pcrreduce/pcr_final_carry_reduce_uint64.py:430-441`。
- `left_signed = x1 - y1`、`right_signed = x0 - y0`，见 `references/pcrreduce/pcr_final_carry_reduce_uint64.py:459-461`。
- `left + (~right) + 1` 的最终 carry，见 `references/pcrreduce/pcr_final_carry_reduce_uint64.py:467-468`。
- `lt = NOT carry64`，见 `references/pcrreduce/pcr_final_carry_reduce_uint64.py:380-382`。

### 5.3 Bitwise adder L2

按照 `references/pcrreduce/并行进位加法.md:291-353`：

```text
P0 = u XOR v
G = AND(u, v)

for offset in [1,2,4,8,16,32]:
    oldP = P
    oldG = G
    shiftedG = oldG << offset
    shiftedP = oldP << offset
    candidate = AND(oldP, shiftedG)
    G = OR(oldG, candidate)
    P = AND(oldP, shiftedP)

carry_into_low64 = (G << 1) & MASK64
sum_low64 = P0 XOR carry_into_low64
carry64 = bit(G,63)
```

L2 验收重点：

- 每轮必须用 `oldP/oldG`，不能边改边读。
- shift 后必须截断到 64 bit。
- sum 使用原始 `P0`，不是 prefix 后的 `P`。

## 6. 条件交换复用策略

建议第一版直接复用 `SS_vec()` 的交换公式。

输入：

```text
shares: [(distance,label), ...]
compare_res[2i] = compare_res[2i+1] = [left_distance > right_distance]
```

公式：

```text
d_left'  = d_left  - u*d_left  + u*d_right
d_right' = d_right + u*d_left  - u*d_right
l_left'  = l_left  - u*l_left  + u*l_right
l_right' = l_right + u*l_left  - u*l_right
```

当 `u=1` 时交换，较小 distance 留在 left；当 `u=0` 时不动。这个正好匹配 Cong reference 中“当 left_value > right_value 时交换两边，保证较小值留在 left_pos”，见 `references/cong/cong_topk_no_wrap_simple.py:224-227`。

L2 阶段可以继续使用当前 `mul_vector_additive()`。虽然它会重构乘数，但符合“先有对应操作步骤”的目标。

## 7. 文件级实施清单

建议按这个顺序做，风险最低。

1. 新增 `Machines/kona-cong.hpp/cpp`，实现 Cong 网络生成。
2. 新增 `Machines/kona-boolean-l2.hpp`，实现 XOR share、AND/OR/shift/bit 的 L2 helper。
3. 新增 `Machines/kona-conversion-l2.hpp`，实现 A2B/B2A L2 bridge。
4. 新增 `Machines/kona-pcr-l2.hpp/cpp`，先实现 direct reconstruct compare，再保留 PCR shape 函数入口。
5. 在 `KNN_party_base` 或 helper 中新增 `cong_top_k_l2()`。
6. 在 `KNN_party_optimized::run()` 中用 `cong_top_k_l2()` 替换 repeated `top_1()`。
7. 保留 `label_compute()`、`SS_vec()`、`mul_vector_additive()` 不动，减少第一阶段变更面。
8. 增加一个小规模测试入口，验证 Cong + PCR L2 输出的 top-k labels 和明文排序一致。

## 8. 推荐测试清单

### 8.1 Cong 网络测试

测试输入：

```text
n=1,k=1
n=2,k=1
n=5,k=2
n=16,k=3
n=64,k=5
```

检查：

- 每层 pair 不冲突。
- `n=16,k=3` 的 comparator count 和 depth 对齐 reference。
- 输出 wire 数量等于 `k`。

### 8.2 PCR L2 比较测试

测试 pair：

```text
0 vs 0
0 vs 1
1 vs 0
7 vs 7
7 vs 8
8 vs 7
2^31-1 vs 2^31
2^63-1 vs 2^63-2
```

检查：

- `lt`、`gt` 都正确。
- 相等时 `lt=0`、`gt=0`。
- arithmetic share bit 重构后只可能是 `0` 或 `1`。

### 8.3 Bitwise adder L2 测试

按 reference 建议：

```text
0 + 0
1 + 0
1 + 1
13 + 7
255 + 1
2^32 - 1 + 1
2^63 - 1 + 1
2^64 - 1 + 1
2^64 - 1 + 2^64 - 1
random 10000 cases
```

检查：

```text
got_low == (u + v) & MASK64
got_carry == ((u + v) >> 64) & 1
```

### 8.4 Top-k 集成测试

构造明文距离和 label：

```text
distance = [9,1,7,3,5,2]
label    = [90,10,70,30,50,20]
k = 3
```

期望：

```text
top distances = [1,2,3]
top labels    = [10,20,30]
```

检查：

- Cong + PCR L2 后，distance 和 label 一起移动。
- 不出现只交换 distance、不交换 label 的错位。
- 重复距离时保持任意稳定性均可，但结果集合必须正确。

## 9. 接口方向约定

统一约定：

```text
pcr_compare_lt_l2(x,y) = [x < y]
pcr_compare_gt_l2(x,y) = [x > y]
compare_res for SS_vec = [left > right]
```

这样 Cong 每层自然写成：

```text
u = [left_distance > right_distance]
if u then swap(left,right)
```

相等时：

```text
u = 0
```

所以相等元素不交换。这和当前 `secure_compare()` 的 equality 语义一致。

## 10. 复用与重写矩阵

可直接复用：

- `KNN_party_optimized::compute_ESD_for_one_query()`：继续产出 `(distance,label)` additive shares。
- `KNN_party_base::SS_vec()`：继续做条件交换 distance 和 label。
- `KNN_party_base::mul_vector_additive()`：L2 阶段继续作为条件交换乘法后端。
- `KNN_party_base::reveal_one_num_to()`：测试和 L2 bridge 可使用。

建议替换：

- repeated `top_1()`：换成 Cong 网络一次性 top-k。
- `compare_in_vec()`：Top-k 路径换成 `pcr_compare_gt_vec_l2()`。
- DCF `evaluate()`：Cong + PCR 路径不再依赖它。

暂时保留：

- `label_compute()`：第一阶段不动。
- `fake_load_triples()`：如果只是本地 L2 正确性测试，可以接受；如果要展示“不泄漏输入”的效果，则必须改成 `load_triples()` 或至少填入非零 mask。

## 11. 建议里程碑

### Milestone A: Cong 网络单测通过

交付：

- 能生成 levels 和 output_wires。
- 明文模拟 compare-and-swap 能输出正确 top-k。

### Milestone B: PCR L2 compare 接入

交付：

- `pcr_compare_gt_vec_l2()` 返回 arithmetic share bit。
- 可替代 `compare_in_vec()` 输入 `SS_vec()`。

### Milestone C: Kona Top-k 路径替换

交付：

- `KNN_party_optimized::run()` 中 repeated `top_1()` 替换为 `cong_top_k_l2()`。
- 小数据集上预测流程能跑完。

### Milestone D: PCR carry 形状补齐

交付：

- `bitwise_add64_l2()` 单测通过。
- `pcr_compare_lt_l2()` 内部从 direct compare 迁移到 final-carry compare。

### Milestone E: 后续真实安全替换点明确

交付：

- `and_share64_l2()` 可替换为 Boolean Beaver/OT。
- `arithmetic_to_xor_l2()` 可替换为真实 A2B。
- `xor_bit_to_arithmetic_l2()` 可替换为真实 B2A。
- `mul_vector_additive()` 可替换为真实 Beaver arithmetic multiplication。

## 12. 最小可行伪代码

下面是第一版最短路径。

```cpp
void cong_top_k_l2(vector<array<Z2<K>,2>>& shares, int n, int k)
{
    auto levels = build_cong_network(n, k);
    auto output_wires = build_cong_output_wires(n, k);

    for (auto& level : levels) {
        vector<int> compare_idx_vec;
        for (auto& pair : level) {
            compare_idx_vec.push_back(pair[0]);
            compare_idx_vec.push_back(pair[1]);
        }

        vector<Z2<K>> compare_res(compare_idx_vec.size());
        pcr_compare_gt_vec_l2(shares, compare_idx_vec, compare_res, m_player);
        SS_vec(shares, compare_idx_vec, compare_res);
    }

    // L2 convenience: gather top-k to the last k slots to match existing Kona code.
    move_output_wires_to_tail(shares, output_wires);
}
```

```cpp
void pcr_compare_gt_vec_l2(
    const vector<array<Z2<K>,2>>& shares,
    const vector<int>& compare_idx_vec,
    vector<Z2<K>>& compare_res,
    RealTwoPartyPlayer* player)
{
    for (int i = 0; i < compare_idx_vec.size() / 2; i++) {
        auto left = shares[compare_idx_vec[2*i]][0];
        auto right = shares[compare_idx_vec[2*i+1]][0];
        Z2<K> bit = pcr_compare_gt_l2(left, right, player);
        compare_res[2*i] = bit;
        compare_res[2*i+1] = bit;
    }
}
```

```cpp
Z2<K> pcr_compare_gt_l2(Z2<K> x_share, Z2<K> y_share, RealTwoPartyPlayer* player)
{
    auto x = reconstruct_additive_l2(x_share, player);
    auto y = reconstruct_additive_l2(y_share, player);
    uint64_t bit = (x > y) ? 1 : 0;
    return public_bit_to_arithmetic_l2(bit, player->my_num());
}
```

## 13. 风险备注

- L2 版本会有明文重构，这是本阶段有意接受的工程折中。
- 如果后续论文实验要声称隐私安全，必须把 L2 bridge、L2 AND、L2 multiplication 全部替换。
- 当前 `fake_load_triples()` 会让优化版输入分享退化成明文发送；本地 correctness demo 可接受，安全实验不可接受。
- Cong 输出位置要特别小心：reference 返回 `output_wires`，Kona 旧逻辑从数组末尾取 top-k。第一版建议显式 gather 到末尾，避免后续 label vote 读错。

## 14. 一句话行动方案

先新增 Cong 网络生成和 `pcr_compare_gt_vec_l2()`，让每层都按 `[left > right]` 生成 arithmetic swap bit，再直接复用 `SS_vec()` 完成 `(distance,label)` 同步交换；等 top-k 结果正确后，再把 PCR 内部从 direct compare 替换成 bitwise final-carry L2。
