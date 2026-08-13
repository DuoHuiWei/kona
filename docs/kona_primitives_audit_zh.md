# Kona 密码学基础操作审计中文版

本文是 `docs/kona_primitives_audit.md` 的中文版本。审计范围是只读阅读 `Machines/kona.cpp`，并参考 `references/` 下 Cong、PCR、Boolean/Beaver 相关材料。本文只根据源码和参考材料下结论；源码无法确认的地方明确写“无法确认”。

## 读取范围

- `Machines/kona.cpp:46-184`
- `Machines/kona.cpp:221-559`
- `Machines/kona.cpp:561-1263`
- `Machines/kona.cpp:1345-1794`
- `Machines/knn-party-offline.cpp:18-123`
- `Machines/knn-party-offline.cpp:126-220`
- `references/cong/cong_topk_no_wrap_simple.py:1-260`
- `references/pcrreduce/pcr_final_carry_reduce_uint64.py:1-220`
- `references/pcrreduce/并行进位加法.md:180-260`

## 份额模型

Kona 当前主要使用 `Z2<K>`，其中 `K=64`，也就是环 `Z_{2^64}` 上的算术加法份额。

- `KNN_party_base` 中定义 `typedef Z2<K> additive_share`，见 `Machines/kona.cpp:49`。
- `reveal_one_num_to(Z2<K>)` 通过接收对方份额并返回 `tmp + x` 重构，见 `Machines/kona.cpp:561-576`。
- 优化版 ESD 使用二元 ABY2 风格份额 `FixedVec<Z2<K>,2>`，见 `Machines/kona.cpp:151`。
- 比较输出被 `SS_scalar()`、`SS_vec()` 传入 `mul_vector_additive()` 继续做算术乘法，所以比较结果是算术加法份额，不是 XOR Boolean 份额，见 `Machines/kona.cpp:1042-1053`、`1059-1063`、`1088-1105`。

## 函数逐项审计

### `KNN_party_base::secure_compare`

位置：`Machines/kona.cpp:1218-1261`

输入：

- `x1`、`x2`：`Z_{2^64}` 上的算术加法份额。
- `greater_than`：公开布尔参数。

输出：

- 一个 `Z2<K>` 算术加法份额，表示比较 bit。

源码中的掩蔽差值：

- 当 `greater_than == true` 时，代码计算 `revealed = x2 - x1 + alpha_share`，见 `Machines/kona.cpp:1227`。
- 当 `greater_than == false` 时，代码计算 `revealed = x1 - x2 + alpha_share`，见 `Machines/kona.cpp:1228-1230`。
- 双方交换后相加，所以公开的掩蔽值是 `R = (x2 - x1) + alpha` 或 `R = (x1 - x2) + alpha`，见 `Machines/kona.cpp:1232-1238`。

DCF 后处理：

- 计算 `u = evaluate(R)`。
- 再计算 `v = evaluate(R + 2^{K-1})`。
- 最终结果份额形状是 `b - (v - u + correction)`，其中 `b = m_playerno`，correction 由打开后的 masked value 最高位决定，见 `Machines/kona.cpp:1242-1259`。

谓词方向：

- `secure_compare(x1, x2, true)` 返回 `[x1 > x2]` 的算术份额。
- `secure_compare(x1, x2, false)` 返回 `[x1 < x2]` 的算术份额。
- 相等时输出 `0`。

依据：

- 函数注释写明 `x1>x2-->1`、`x1==x2-->0`，见 `Machines/kona.cpp:1218`。
- `SS_scalar()` 使用 `u` 做交换 bit，公式是 `x1' = x1 - u*x1 + u*x2`、`x2' = x2 + u*x1 - u*x2`，见 `Machines/kona.cpp:1042-1048`、`1059-1063`。只有当 `u=[x1>x2]` 时，`min_then_max` 的交换语义才成立。

### `KNN_party_base::compare_in_vec(vector<Z2<K>>)`

位置：`Machines/kona.cpp:839-909`

输入：

- `shares`：一维算术加法份额数组。
- `compare_idx_vec`：公开偶数长度索引数组，每两个索引形成一组比较。
- `greater_than`：公开布尔参数。

输出：

- `compare_res`：算术加法份额形式的比较 bit。
- 每组比较结果会重复写两次：`compare_res[2*i+1] = compare_res[2*i]`，见 `Machines/kona.cpp:903-906`。

公式：

- `greater_than == true` 时，先打开 masked value `R_i = shares[idx_{2i+1}] - shares[idx_{2i}] + alpha`，见 `Machines/kona.cpp:852-858`。
- `greater_than == false` 时，先打开 masked value `R_i = shares[idx_{2i}] - shares[idx_{2i+1}] + alpha`，见 `Machines/kona.cpp:860-865`。
- 后续 DCF 处理与 `secure_compare()` 相同，见 `Machines/kona.cpp:882-906`。

语义：

- 当 `greater_than == true` 时，`compare_res[2*i]` 是 `[shares[idx_{2i}] > shares[idx_{2i+1}]]` 的算术份额。
- 相等输出 `0`。
- 重复一份结果是为了和 `SS_vec()` 后续乘法对齐。

### `KNN_party_base::compare_in_vec(vector<array<Z2<K>,2>>)`

位置：`Machines/kona.cpp:914-986`

输入：

- `shares`：二维 pair 数组，每个元素是 `(distance,label)`。
- 比较只看 `shares[*][0]`，也就是 distance/key 字段，见 `Machines/kona.cpp:930-940`。

输出：

- 算术加法份额形式的比较 bit。
- 每组结果同样重复写两次。

语义：

- 与一维 `compare_in_vec()` 相同，只是比较对象变成 pair 的第一个字段。

### `KNN_party_base::mul_additive`

位置：`Machines/kona.cpp:1109-1127`

输入：

- `x1`、`x2`：`Z_{2^64}` 上的算术加法份额。

输出：

- `res`：算术加法份额。

实际实现：

- 代码中 `a=b=c=0`，见 `Machines/kona.cpp:1111`。
- 双方发送 `(x1-a, x2-b)`，由于 `a=b=0`，实际发送的是本地份额 `x1` 和 `x2`，见 `Machines/kona.cpp:1113-1116`。
- 收到对方份额后，双方都能重构 `e=x`、`f=y`，见 `Machines/kona.cpp:1117-1122`。
- 计算 `r = f*a + e*b + c = 0`。
- party 1 再加 `e*f = x*y`，见 `Machines/kona.cpp:1123-1125`。

结论：

- 这不是实际安全的 Beaver 乘法。
- 它有 Beaver 乘法的外形，但没有使用真实三元组。
- 当前结果份额是 party 0 得到 `0`，party 1 得到 `x*y`。
- 当前实现会让双方在乘法过程中看到被乘数明文。

### `KNN_party_base::mul_vector_additive`

位置：`Machines/kona.cpp:1129-1216`

输入：

- `v1`、`v2`：算术加法份额数组。
- `double_res == false`：逐项计算 `v1[i] * v2[i]`。
- `double_res == true`：`v1` 长度是 `v2` 的两倍，前半段和后半段都乘同一个 `v2`。

输出：

- `res`：算术加法份额数组。

实际实现：

- 与 `mul_additive()` 相同，`a=b=c=0`，见 `Machines/kona.cpp:1134`、`1188`。
- 各项乘法都会通过交换本地份额重构被乘数。
- party 1 持有完整乘积，party 0 持有 `0`，见 `Machines/kona.cpp:1137-1181`、`1190-1212`。

结论：

- L2 正确性阶段可以把它当作“乘法操作形状”继续用。
- 如果要真实安全，这个函数必须替换成真实 Beaver/OT/ABY2 乘法。

### `KNN_party_base::SS_scalar(vector<array<Z2<K>,2>>)`

位置：`Machines/kona.cpp:1040-1054`

输入：

- `shares[first_idx] = (d1,l1)`。
- `shares[second_idx] = (d2,l2)`。
- 四个值都是算术加法份额。
- `min_then_max` 是公开参数。

比较 bit：

- `u = secure_compare(d1, d2, min_then_max)`，见 `Machines/kona.cpp:1042`。

更新公式：

```text
d1' = d1 - u*d1 + u*d2
d2' = d2 + u*d1 - u*d2
l1' = l1 - u*l1 + u*l2
l2' = l2 + u*l1 - u*l2
```

依据：

- 乘法向量 `Y = (u*d1, u*d2, u*l1, u*l2)`，见 `Machines/kona.cpp:1046`。
- 更新 distance 和 label，见 `Machines/kona.cpp:1047-1053`。

解释：

- `u=1` 时交换两个元素。
- `u=0` 时保持不变。
- distance 和 label 会一起交换。

### `KNN_party_base::SS_scalar(vector<Z2<K>>)`

位置：`Machines/kona.cpp:1057-1064`

输入输出：

- 输入是一维算术加法份额数组。
- 输出是原地更新后的算术加法份额。

公式：

```text
u = secure_compare(x1, x2, min_then_max)
x1' = x1 - u*x1 + u*x2
x2' = x2 + u*x1 - u*x2
```

源码依据：`Machines/kona.cpp:1059-1063`。

### `KNN_party_base::SS_vec`

位置：`Machines/kona.cpp:1084-1106`

输入：

- `shares[idx][0]`：distance/key 的算术加法份额。
- `shares[idx][1]`：label/payload 的算术加法份额。
- `compare_res`：算术加法份额形式的比较 bit，且每组重复两次。

输出：

- 原地更新 `shares`。

构造：

- 先把所有 distance 放入 `tmp_ss`。
- 再把所有 label 追加到 `tmp_ss`。
- 然后调用 `mul_vector_additive(tmp_ss, compare_res, tmp_res, true)`，见 `Machines/kona.cpp:1088-1092`。

完整公式：

对一组比较 pair `(a,b)`，令 `u = [d_a > d_b]`：

```text
d_a' = d_a - u*d_a + u*d_b
d_b' = d_b + u*d_a - u*d_b
l_a' = l_a - u*l_a + u*l_b
l_b' = l_b + u*l_a - u*l_b
```

依据：

- distance 更新在 `Machines/kona.cpp:1093-1098`。
- label 更新在 `Machines/kona.cpp:1100-1105`。

结论：

- `SS_vec()` 会同时交换 distance 和 label。
- 这正适合 Cong 每层 compare-and-swap。

### `KNN_party_optimized::fake_load_triples`

位置：`Machines/kona.cpp:301-374`

输入：

- 没有显式参数。
- 使用 `num_train_data`、`num_features`、`playerno`。

输出/状态：

- 只 resize 内部向量。
- 所有文件读取逻辑都被注释掉。
- 因此向量默认初始化，实际使用的是零 triple/零 mask。

关键后果：

- `KNN_party_optimized::run()` 当前调用 `fake_load_triples()` 后立即调用 `aby2_share_data_and_additive_share_label_list()`，见 `Machines/kona.cpp:696-698`。
- 所以优化版数据分享阶段实际会使用零 mask。

### `KNN_party_optimized::load_triples`

位置：`Machines/kona.cpp:377-450`

输入：

- 从 `./Player-Data/Knn-Data/<dir><dataset>-data/` 读取二进制预处理文件。

party 0 读取：

- `m_Train_Triples_0`
- `m_Train_Triples_1`
- `m_Test_Triples_0`
- `m_Test_Triples[*][*]`
- 见 `Machines/kona.cpp:379-409`。

party 1 读取：

- `m_Test_Triples_0`
- `m_Test_Triples_1`
- `m_Train_Triples_1`
- `m_Test_Triples[*][*]`
- 见 `Machines/kona.cpp:411-448`。

### `KNN_party_optimized::compute_ESD_two_sample`

位置：`Machines/kona.cpp:532-559`

输入：

- 训练样本 ABY2 风格份额 `m_train_aby2_share_vec[train_idx][j]`。
- 查询样本 ABY2 风格份额 `m_test_aby2_share_vec[query_idx][j]`。
- 自定义预处理值 `m_Test_Triples[train_idx][j]`。

输出：

- 一个 `Z2<K>` 算术加法份额，表示平方欧氏距离。

每个特征的公式：

令：

```text
s0_j = (x_j - y_j) + (r^x_0 + r^x_1) - (r^y_0 + r^y_1)
s1_j = r^x_b - r^y_b
```

代码计算：

```text
tmp_1 += s0_j^2
res += -2*s0_j*s1_j + t_j
```

party 1 最后额外加 `tmp_1`，见 `Machines/kona.cpp:538-545`。

预处理满足：

```text
t_0_j + t_1_j =
(r^y_0 + r^y_1 - r^x_0 - r^x_1)^2
```

见 `Machines/kona.cpp:481-487`。

所以重构后得到：

```text
sum_j (x_j - y_j)^2
```

## 必答问题汇总

### 1. 每个函数的输入、输出和分享类型

- `secure_compare`：输入算术加法份额，输出算术加法份额形式的比较 bit。
- `compare_in_vec`：输入算术加法份额数组，输出算术加法份额形式的比较 bit，且每组重复两次。
- `mul_additive`：输入/输出算术加法份额，但当前会打开被乘数。
- `mul_vector_additive`：输入/输出算术加法份额数组，当前同样会打开被乘数。
- `SS_scalar`：输入 `(distance,label)` 或一维值的算术加法份额，原地输出。
- `SS_vec`：输入 `(distance,label)` 算术加法份额数组，原地输出，并同步交换 distance 和 label。
- `fake_load_triples`：只 resize 状态，不实际加载随机/三元组。
- `load_triples`：从离线文件加载优化版 ESD 所需预处理。
- `compute_ESD_two_sample`：输入 ABY2 风格份额和 ESD 预处理，输出算术加法份额的距离。

### 2. `secure_compare` 的准确谓词方向

```text
secure_compare(x1,x2,true)  = [x1 > x2]
secure_compare(x1,x2,false) = [x1 < x2]
相等时输出 0
```

### 3. `compare_in_vec` 输出是 XOR 份额还是算术份额

输出是 `Z2<64>` 算术加法份额，不是 XOR 份额。

依据：

- 类型是 `vector<Z2<K>>`。
- 被 `SS_vec()` 作为算术乘法输入。
- 重构语义沿用加法重构。

### 4. `mul_additive` 是否使用 Beaver triple、OT、ABY2 或其他协议

当前没有使用真实 Beaver triple、OT 或 ABY2 乘法。

它只是 Beaver-like 公式外形，实际 `a=b=c=0`。双方交换后重构被乘数，party 1 得到乘积，party 0 得到 `0`。

### 5. 三元组从哪里生成、每次乘法是否消耗新三元组、是否可能复用

对 `mul_additive()` 和 `mul_vector_additive()`：

- 没有三元组生成。
- 没有三元组加载。
- 没有每次乘法消耗新 triple。
- 因为 `a=b=c=0`，复用问题不适用。

对优化版 ESD：

- 预处理由 `generate_triples_save_file()` 或 `knn-party-offline.cpp` 生成。
- 写入 `P0-Train-Triples`、`P1-Train-Triples`、`P0-Test-Triples`、`P1-Test-Triples`。
- `m_Test_Triples[train_idx][j]` 会被每个 query 重复使用。
- 这种复用是否符合原协议安全设计：无法确认。

### 6. `SS_vec` 的完整数学公式

对 pair `(a,b)`，`u=[d_a>d_b]`：

```text
d_a' = (1-u)d_a + u d_b
d_b' = u d_a + (1-u)d_b
l_a' = (1-u)l_a + u l_b
l_b' = u l_a + (1-u)l_b
```

等价写法：

```text
d_a' = d_a - u*d_a + u*d_b
d_b' = d_b + u*d_a - u*d_b
l_a' = l_a - u*l_a + u*l_b
l_b' = l_b + u*l_a - u*l_b
```

### 7. `SS_vec` 是否同时交换 distance 和 label

是。`SS_vec()` 先处理 distance，再用同一组 comparison bit 处理 label，因此 distance 和 label 同步移动。

### 8. 是否存在任何基于秘密 bit 的本地 if

审计范围内没有发现显式 `if(secret_share_bit)`。

相关 `if` 条件主要是：

- 公开参数 `greater_than`、`min_then_max`。
- 公开 party 编号 `playerno`。
- 已打开 masked value 的最高位。
- 公开循环和索引逻辑。

### 9. 是否存在显式 B2A；若没有，现有比较为什么不需要 B2A

没有发现显式 `B2A`、`A2B`、`daBit`、`edaBit`。

现有比较不需要 B2A，是因为它对外直接返回算术加法份额形式的 bit：

```text
arithmetic shares -> 打开 masked arithmetic difference -> DCF evaluate -> arithmetic share bit
```

比较结果没有暴露为 XOR Boolean share API，所以没有显式 B2A 步骤。

### 10. `fake_load_triples` 是否会导致原始数据以明文形式发送

会。

当前 `run()` 调用 `fake_load_triples()`，它只 resize 且不加载随机值。随后 `aby2_share_data_and_additive_share_label_list()` 发送：

```text
training feature + zero + zero
test feature + zero + zero
```

因此训练特征和查询特征会以明文形式发送。

### 11. DCF alpha 是否在同一批次或多次调用中复用

是。

每次比较都重新从 `Player-Data/2-fss/r<playerno>` 读取同一个值。`gen_fake_dcf()` 也只为每个 party 写一个 `r` 文件值。源码没有按比较次数推进 alpha 池。

因此同一个 alpha 会在：

- 一个 `compare_in_vec()` 批次内复用。
- 多次 `secure_compare()` 调用间复用。
- 整个运行过程中复用，除非外部重新生成文件。

### 12. 哪些函数可以安全复用于 Cong + PCR，哪些必须重写

L2 阶段可复用：

- `compute_ESD_for_one_query()`：继续产出 `(distance,label)` 算术份额。
- `SS_vec()`：继续做条件交换，同时移动 distance 和 label。
- `mul_vector_additive()`：L2 阶段可接受其现有乘法形状。
- `reveal_one_num_to()`：可作为测试或 L2 桥接辅助。

真实安全阶段必须重写：

- `mul_additive()` / `mul_vector_additive()`：需要真实 Beaver/OT/ABY2 乘法。
- `compare_in_vec()`：Cong + PCR 路径需要新的逐层批量比较接口。
- `secure_compare()`：如果采用 PCR comparator，则 DCF 比较路径不再是目标后端。
- `fake_load_triples()`：真实安全场景不能让输入分享退化成明文发送。
- 预处理加载：当前优化版 ESD 预处理不是 PCR 所需 Boolean Beaver triple 池。

## 附加结论

- `fake_load_triples()` 在当前运行路径下会导致明文特征发送。
- 优化版 ESD 使用的是自定义预处理，不是通用乘法三元组池。
- DCF/FSS 比较依赖 `Player-Data/2-fss/k0/k1/r0/r1/r2`，并复用同一个 `r`。

## 无法确认事项

- 仅凭本地源码，无法确认 `evaluate()` 的 DCF 构造是否从理论上完全正确。
- 代码显示 ESD 预处理会跨 query 复用，但这种复用是否符合原协议安全设计：无法确认。
- 没有发现外部流程会在 `fake_load_triples()` 和数据分享之间写入非零随机值；若存在外部注入路径，审计范围内无法确认。
