# Kona Cong + PCR 化简版 L2 改造建议清单中文版

本文是 `docs/kona_cong_pcr_l2_migration_checklist.md` 的中文版本，面向后续实际改代码时使用。这里的“L2 安全”按当前约定理解为：协议步骤、接口方向、数据流和数学逻辑先闭合，允许使用本地重构、零随机、零三元组、`a=b=c=0` 等方式先验证正确性。真实隐私安全不是这个阶段的验收目标。

## 0. 改造目标

把 Kona 当前 Top-k 路径：

```text
重复 top_1
    -> DCF secure_compare / compare_in_vec
    -> SS_vec 条件交换
```

改造成：

```text
ESD 算术加法份额
    -> Cong 固定比较网络
    -> 每层批量 PCR L2 比较
    -> 得到 [left > right] 的算术份额 swap bit
    -> 复用 SS_vec 同步交换 distance 和 label
    -> 输出最小 k 个邻居
```

核心原则：

- 先不要改 ESD。
- 先不要改 label vote。
- 先把 Top-k 的比较网络换成 Cong。
- 先让 PCR L2 comparator 能产出 Kona 当前可消费的 arithmetic share bit。
- 先复用 `SS_vec()` 做条件交换。

## 1. 当前 Kona 可复用的事实

- `secure_compare(x1,x2,true)` 的语义是 `[x1 > x2]`，相等输出 `0`。
- `compare_in_vec()` 输出是 `Z2<64>` 算术加法份额，不是 XOR 份额。
- `SS_vec()` 已经实现 `(distance,label)` 同步条件交换。
- `mul_vector_additive()` 当前不是安全乘法，但 L2 阶段可以继续接受，因为我们只要求操作步骤存在且逻辑正确。
- Cong reference 已经把主循环拆成“公开比较网络 + 每层 compare-and-swap 函数”。
- PCR reference 的比较器语义是 `lt = 1 <=> x < y`，输出 XOR Boolean shares；Kona 集成时需要再转成 arithmetic share bit。

## 2. L2 阶段允许范围

允许：

- 在比较内部重构明文。
- 用 `reconstruct -> compute -> reshare` 实现 AND、OR、比较。
- A2B/B2A 先用本地重构桥接。
- share 可以固定成 `{0,value}`。
- Beaver triple 可以继续是零。
- `mul_vector_additive()` 可以继续用当前实现。
- 先只处理 64-bit、no-wrap、signed/int64 可解释范围。

暂不要求：

- 不要求真实输入隐私。
- 不要求每个 triple 新鲜。
- 不要求随机性正确。
- 不要求真实 `prepare / exchange / finalize` 轮次拆分。
- 不要求 GPU。
- 不要求恶意安全或 MAC。

## 3. 建议新增文件

建议新增：

```text
Machines/kona-cong.hpp
Machines/kona-cong.cpp
Machines/kona-pcr-l2.hpp
Machines/kona-pcr-l2.cpp
Machines/kona-boolean-l2.hpp
Machines/kona-conversion-l2.hpp
```

第一阶段也可以只新增前四个文件，把 boolean/conversion helper 暂时写在 `kona-pcr-l2.hpp` 内。后面再拆干净。

## 4. Cong 网络模块

职责：

- 根据公开 `n`、`k` 生成 Cong Top-k 固定比较网络。
- 每层输出多个互不冲突的比较 pair。
- 返回最终 top-k 对应的 output wires。

接口建议：

```cpp
using ComparePair = std::array<int, 2>;
using CongLevel = std::vector<ComparePair>;

std::vector<CongLevel> build_cong_network(int n, int k);
std::vector<int> build_cong_output_wires(int n, int k);
```

实现建议：

- 直接翻译 `references/cong/cong_topk_no_wrap_simple.py` 里的 `build_cong_network()`、`build_sort_network()`、`build_merge_network()`。
- 先保留 CPU vector 实现。
- 每层生成后检查同一层内 index 不重复。

验收：

- `n=16,k=3` 时 comparator count 对齐 reference 的 `35`。
- `n=16,k=3` 时 depth 对齐 reference 的 `9`。
- 随机明文数组能输出最小 `k` 个。

## 5. PCR L2 比较模块

Kona 需要的 comparator 不是直接返回 XOR share，而是返回 `SS_vec()` 能吃的 arithmetic share bit。

接口建议：

```cpp
Z2<K> pcr_compare_lt_l2(
    Z2<K> x_share,
    Z2<K> y_share,
    RealTwoPartyPlayer* player);

Z2<K> pcr_compare_gt_l2(
    Z2<K> x_share,
    Z2<K> y_share,
    RealTwoPartyPlayer* player);

void pcr_compare_gt_vec_l2(
    const std::vector<std::array<Z2<K>, 2>>& shares,
    const std::vector<int>& compare_idx_vec,
    std::vector<Z2<K>>& compare_res,
    RealTwoPartyPlayer* player);
```

方向约定：

```text
pcr_compare_lt_l2(x,y) = [x < y]
pcr_compare_gt_l2(x,y) = [x > y]
相等时输出 0
compare_res for SS_vec = [left > right]
```

这样 Cong 每层逻辑就是：

```text
如果 left > right，则交换
交换后较小值留在 left
```

## 6. PCR L2 第一版：直接重构比较

这是最快接入 Cong 的版本。

步骤：

1. party 0 和 party 1 交换 `x_share`、`y_share`。
2. 双方本地重构 `x = x0 + x1`、`y = y0 + y1`。
3. 计算 `gt = (x > y) ? 1 : 0`。
4. 相等时自然得到 `0`。
5. 重新分享成 arithmetic share bit。
6. L2 可令 party 0 返回 `0`，party 1 返回 `gt`。

伪代码：

```cpp
Z2<K> pcr_compare_gt_l2(Z2<K> x_share, Z2<K> y_share, RealTwoPartyPlayer* player)
{
    Z2<K> x = reconstruct_additive_l2(x_share, player);
    Z2<K> y = reconstruct_additive_l2(y_share, player);
    uint64_t bit = (x > y) ? 1 : 0;
    return public_bit_to_arithmetic_l2(bit, player->my_num());
}
```

vector 版：

```cpp
for each pair (left,right):
    bit = pcr_compare_gt_l2(shares[left][0], shares[right][0], player)
    compare_res[2*i] = bit
    compare_res[2*i+1] = bit
```

注意：

- 这里比较 `shares[*][0]`，也就是 distance/key。
- `compare_res` 必须重复写两份，否则 `SS_vec()` 的乘法对齐会错。

## 7. PCR L2 第二版：保留 final-carry 形状

第一版跑通后，再把 comparator 内部替换成 PCR final-carry 形状。

参考语义：

```text
x = x1 - x0
y = y1 - y0
lt = 1 <=> x < y
```

reference 中核心公式：

```text
left_signed = x1 - y1
right_signed = x0 - y0
left = signed_to_ordered_uint64(left_signed)
right = signed_to_ordered_uint64(right_signed)
carry64 = final_carry(left + ~right + 1)
lt = NOT carry64
```

接入 Kona 时可做：

```text
additive share -> L2 重构 -> 临时构造 subtractive view -> PCR final-carry -> XOR bit -> arithmetic bit
```

或者更简单：

```text
additive share -> L2 重构 x,y -> 直接用 bitwise final-carry 比较 x,y -> arithmetic bit
```

这一步的目的不是安全，而是让代码结构和 PCR 论文/参考实现一致。

## 8. Boolean L2 helper

建议结构：

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

建议函数：

```cpp
uint64_t reconstruct_xor64(XorShare64 x);
XorShare64 share_xor64_l2(uint64_t x);
XorShare64 xor_share64(XorShare64 a, XorShare64 b);
XorShare64 and_share64_l2(XorShare64 a, XorShare64 b);
XorShare64 or_share64_l2(XorShare64 a, XorShare64 b);
XorShare64 shift_left_share64(XorShare64 a, int offset);
XorBitShare bit_share64(XorShare64 a, int bit);
```

L2 实现：

- `share_xor64_l2(x)` 返回 `{0,x}`。
- `xor_share64(a,b)` 逐份额 XOR。
- `and_share64_l2(a,b)` 重构后计算 `a & b`，再 `{0,result}`。
- `or_share64_l2(a,b)` 可以用 `a XOR b XOR (a AND b)`，也可以重构后 OR。
- `shift_left_share64(a,offset)` 对两个份额分别左移并 `& MASK64`。
- `bit_share64(a,bit)` 对两个份额分别取 bit。

## 9. A2B/B2A L2 bridge

建议接口：

```cpp
XorShare64 arithmetic_to_xor_l2(Z2<K> x_share, RealTwoPartyPlayer* player);
Z2<K> xor_bit_to_arithmetic_l2(XorBitShare bit_share, RealTwoPartyPlayer* player);
Z2<K> public_bit_to_arithmetic_l2(uint64_t bit, int playerno);
```

L2 实现：

- A2B：交换 arithmetic share，重构 `x`，返回 XOR share `{0,x}`。
- B2A：重构 XOR bit，返回 party 0 为 `0`、party 1 为 `bit`。
- public bit to arithmetic：同样 party 0 为 `0`、party 1 为 `bit`。

后续真实安全版本替换点：

- A2B 替换为真实 masked conversion。
- B2A 替换为 daBit/edaBit 或其他转换协议。
- Boolean AND 替换为 Boolean Beaver/OT。

## 10. bitwise final-carry 加法器

PCR final-carry 比较需要知道 `left + (~right) + 1` 的最高 carry。

参考流程：

```text
P0 = u XOR v
G = AND(u, v)

for offset in [1,2,4,8,16,32]:
    oldP = P
    oldG = G
    shiftedG = (oldG << offset) & MASK64
    shiftedP = (oldP << offset) & MASK64
    candidate = AND(oldP, shiftedG)
    G = OR(oldG, candidate)
    P = AND(oldP, shiftedP)

carry_into_low64 = (G << 1) & MASK64
sum_low64 = P0 XOR carry_into_low64
carry64 = bit(G,63)
```

关键注意：

- 循环里必须先保存 `oldP`、`oldG`。
- shift 后必须 `& MASK64`。
- 最终 sum 使用原始 `P0`，不能使用 prefix 更新后的 `P`。
- PCR 比较最重要的是 `carry64`，不一定需要真的输出完整 sum。

## 11. Cong Top-k 集成函数

建议新增：

```cpp
void cong_top_k_l2(
    std::vector<std::array<Z2<K>,2>>& shares,
    int n,
    int k,
    bool min_k);
```

主逻辑：

```text
levels = build_cong_network(n,k)
output_wires = build_cong_output_wires(n,k)

for each level:
    compare_idx_vec = flatten(level)
    compare_res.resize(compare_idx_vec.size())
    pcr_compare_gt_vec_l2(shares, compare_idx_vec, compare_res, player)
    SS_vec(shares, compare_idx_vec, compare_res)

把 output_wires 对应元素搬到末尾 k 个位置
```

为什么搬到末尾：

- Kona 当前主流程在 Top-k 后从 `m_ESD_vec` 末尾取 label。
- 为了第一版少改代码，建议让 `cong_top_k_l2()` 输出后兼容这个旧假设。
- 如果选择把 top-k 放在前 `k` 个位置，也可以，但后续 label 读取逻辑必须一起改。

## 12. 替换 Kona 主路径

当前代码形状：

```cpp
for(int i=0;i<k_const;i++){
    top_1(m_ESD_vec,num_train_data-i,true);
}
```

L2 替换为：

```cpp
cong_top_k_l2(m_ESD_vec, num_train_data, k_const, true);
```

暂时不改：

- `compute_ESD_for_one_query()`。
- `label_compute()`。
- `SS_vec()`。
- `mul_vector_additive()`。

这样第一阶段的变更范围最小。

## 13. 条件交换公式

`SS_vec()` 复用的核心公式：

```text
u = [left_distance > right_distance]

d_left'  = d_left  - u*d_left  + u*d_right
d_right' = d_right + u*d_left  - u*d_right
l_left'  = l_left  - u*l_left  + u*l_right
l_right' = l_right + u*l_left  - u*l_right
```

当 `u=1`：

```text
left 和 right 交换
```

当 `u=0`：

```text
left 和 right 不动
```

这和 Cong reference 的要求一致：当 `left_value > right_value` 时交换，保证较小值留在 left。

## 14. 测试清单

### 14.1 Cong 网络测试

测试参数：

```text
n=1,k=1
n=2,k=1
n=5,k=2
n=16,k=3
n=64,k=5
```

检查：

- 每层 pair 不冲突。
- `n=16,k=3` comparator count 为 `35`。
- `n=16,k=3` depth 为 `9`。
- output wires 数量等于 `k`。

### 14.2 PCR L2 比较测试

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

- `lt` 正确。
- `gt` 正确。
- 相等时 `lt=0`、`gt=0`。
- 重构后的 arithmetic bit 只可能是 `0` 或 `1`。

### 14.3 bitwise adder L2 测试

测试：

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

### 14.4 Top-k 集成测试

样例：

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

- distance 和 label 同步移动。
- 不出现只交换 distance 不交换 label 的错位。
- 重复 distance 时，集合正确即可，稳定性第一版不强制。

## 15. 复用与替换矩阵

直接复用：

- `KNN_party_optimized::compute_ESD_for_one_query()`。
- `KNN_party_base::SS_vec()`。
- `KNN_party_base::mul_vector_additive()`。
- `KNN_party_base::reveal_one_num_to()`。

Top-k 路径替换：

- repeated `top_1()` 替换为 `cong_top_k_l2()`。
- `compare_in_vec()` 替换为 `pcr_compare_gt_vec_l2()`。
- DCF `evaluate()` 在 Cong + PCR Top-k 路径中不再使用。

暂时保留：

- `label_compute()`。
- `fake_load_triples()` 可用于本地 L2 correctness demo。

真实安全前必须替换：

- `fake_load_triples()`。
- `mul_additive()`。
- `mul_vector_additive()`。
- `and_share64_l2()`。
- `arithmetic_to_xor_l2()`。
- `xor_bit_to_arithmetic_l2()`。

## 16. 里程碑

Milestone A：Cong 网络生成

- 生成 levels 和 output_wires。
- 明文模拟 compare-and-swap 测试通过。

Milestone B：PCR L2 比较

- `pcr_compare_gt_l2()` 单点测试通过。
- `pcr_compare_gt_vec_l2()` 能填充重复 compare_res。

Milestone C：接入 `SS_vec()`

- 每层 Cong 网络能调用 `pcr_compare_gt_vec_l2()`。
- `SS_vec()` 能同步交换 distance 和 label。

Milestone D：替换 Kona Top-k

- `KNN_party_optimized::run()` 中 repeated `top_1()` 替换为 `cong_top_k_l2()`。
- 小数据集流程跑通。

Milestone E：补齐 PCR final-carry 形状

- `bitwise_add64_l2()` 测试通过。
- comparator 内部从直接比较改成 final-carry 计算。

Milestone F：标记真实安全替换点

- Boolean AND 可换成 Beaver/OT。
- A2B/B2A 可换成真实转换。
- Arithmetic multiplication 可换成真实 Beaver。

## 17. 最小可行伪代码

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

## 18. 关键风险提示

- L2 版本会明文重构，这是本阶段故意接受的正确性验证手段。
- 如果后续要写论文安全实验，不能把 L2 当真实安全协议。
- `fake_load_triples()` 会让优化版输入分享退化成明文发送，只适合本地 correctness demo。
- Cong 输出位置必须处理清楚；Kona 旧逻辑从末尾取 top-k，因此第一版建议 gather 到末尾。
- PCR reference 的输出是 XOR bit，Kona `SS_vec()` 需要 arithmetic bit，中间必须有 L2 B2A bridge。

## 19. 一句话实施方案

先实现 Cong 网络生成和 `pcr_compare_gt_vec_l2()`，让每一层都得到 `[left > right]` 的 arithmetic swap bit；然后直接复用 `SS_vec()` 做 `(distance,label)` 同步交换。Top-k 结果正确后，再把 comparator 内部从直接重构比较替换成 PCR final-carry L2。
