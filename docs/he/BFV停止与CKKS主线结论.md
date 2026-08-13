# BFV停止与CKKS主线结论

本文用于记录当前 HE 对比组已经完成的关键技术判断，以及为什么主线从 BFV 系数提取路线切换到 CKKS→FHEW 路线。

## 1. 已经验证通过的内容

当前已经完成并保留了以下测试：

- `openfhe-smoke`
- `polynomial-distance-plain-test`
- `openfhe-polynomial-api-test`
- `openfhe-coefficient-extraction-boundary-test`
- `openfhe-public-schemeswitch-test`
- `openfhe-bfv-to-lwe-test`

这些测试共同证明了下面几件事。

### 1.1 明文代数和分块编码正确

`polynomial-distance-plain-test` 已经验证：

- 反向多项式编码公式正确
- 10000 维按 `4096 + 4096 + 1808` 分块正确
- Arcene 一条 query 对 199 条 train 的分块内积正确
- 简化分数
  `s_i = ||x_i||^2 - 2<q, x_i>`
  与完整平方欧氏距离排序一致

这个测试的定位应明确为：

```text
Cong 距离编码的明文代数测试
```

它不是正式 HE 距离协议。

### 1.2 BFV 密文 × 明文多项式乘法正确

`openfhe-polynomial-api-test` 已经验证：

- OpenFHE BFV 公共接口可承载查询多项式加密
- 可执行 `ciphertext × plaintext polynomial`
- 解密完整结果多项式后，目标系数确实等于普通内积

这证明了：

- Cong 的反向多项式编码在 OpenFHE BFV 中是可实现的
- BFV/RNS 的环乘法结果与预期一致

但这个测试仍然只是：

```text
正确性 oracle / API 能力测试
```

它不能直接下沉成正式协议实现，因为正式服务器不能持有客户端私钥，也不能把完整多项式密文批量返还给客户端再解。

### 1.3 OpenFHE 公开 API 的边界已经查清

`openfhe-coefficient-extraction-boundary-test` 已经验证：

- `EvalCKKStoFHEWSetup`：公开可见
- `EvalCKKStoFHEW`：公开可见
- `GetBinCCForSchemeSwitch`：公开可见
- `GetSwkFC`：公开可见
- 但没有直接公开的：
  - `SampleExtract`
  - `EvalSampleExtract`
  - `ExtractCiphertextCoefficient`

因此准确结论是：

```text
OpenFHE 公共 CryptoContext 表面
公开了 CKKS↔FHEW 方案切换钩子，
但没有直接公开一个 SampleExtract 风格的系数提取 API。
```

### 1.4 CKKS→FHEW 公开路线已经实测跑通

`openfhe-public-schemeswitch-test` 已经验证：

```text
CKKS ciphertext
-> EvalCKKStoFHEW
-> vector<LWECiphertext>
-> BinFHE decrypt
```

这说明：

- OpenFHE 原生公开支持的公开路线是真实可用的
- 它不是只存在于头文件里的接口名

## 2. BFV→BinFHE 的最终判断

`openfhe-bfv-to-lwe-test` 的目标是：

```text
BFV ciphertext
-> 目标系数提取
-> BinFHE-compatible LWECiphertext
```

在本项目设定的约束下：

- 不修改 `/opt/openfhe`
- 不依赖私有符号
- 不复制大段内部实现
- 不在 BFV 加密后到最终 LWE 验证前调用 `Decrypt()`
- 只使用公开头文件、公开元素访问和小型适配层

这个探针没有打通。

更严谨的结论应写成：

```text
bfv_to_binfhe_compatibility=
unsupported_by_openfhe_public_api_under_current_constraints
```

而不是简单写成“密码学上不可能”。

原因是：

- 这次实验只能证明在当前工程约束下不可落地
- 不能从数学上证明永远无法实现
- 但对本项目的工程决策来说，这已经足够

因为再继续下去，问题会从“做单服务器 HE 对照组”转变为“扩展并维护 OpenFHE 密码库”。

## 3. 为什么正式停止 BFV 主线

当前 BFV 路线是：

```text
BFV 系数编码查询
-> ciphertext × plaintext polynomial
-> 目标系数包含内积
-> ??? BFV/RNS 密文转 BinFHE LWE
-> Cong 比较器
```

前半段已经证明正确，但问号没有打通。

而且已有证据表明：

- 单纯读取 BFV 密文元素
- 调整系数顺序与符号
- 结合公开 LWE 类型

并不足以完成 BFV 到 BinFHE LWE 的可靠桥接。

因此现在应正式停止：

- 继续深挖 BFV→LWE
- 继续扩建 BFV 正式距离模块
- 任何“客户端解密完整多项式”的退路

## 4. 新的正式技术主线

主线应确定为：

```text
CKKS 同态距离
+ CKKS↔FHEW 比较
+ Cong Top-k 网络
```

这条路线的含义不是：

```text
直接使用 OpenFHE 的整体 Min/Max
```

而是：

```text
在每一个 Cong compare-and-swap 位置
调用 OpenFHE 的方案切换比较能力
```

也就是：

```text
Cong schedule
    ↓
每一对候选项调用 EvalCompareSchemeSwitching
    ↓
返回 CKKS 加密比较结果
    ↓
使用 CKKS 算术完成 score/label/index 的交换
```

这样单服务器组仍然保留：

- 相同比较位置
- 相同比较器数量
- 相同网络深度
- 相同 `n` 和 `k`

## 5. 下一步建议

最近的正式目标应当是新增：

```text
HECompare/tests/openfhe_ckks_distance_compare_test.cpp
```

这个测试应只做一个最小闭环：

### 第一部分：CKKS 简化距离

验证：

```text
score_i = ||x_i||^2 - 2<q, x_i>
```

在小规模样例上解密后与明文结果的误差。

至少记录：

- 最大绝对误差
- 最大相对误差
- 排序是否一致

### 第二部分：方案切换比较

对多个分数对执行：

```text
EvalCompareSchemeSwitching(...)
```

确认比较方向。

至少覆盖：

- 差距很大
- 差距很小
- 相等
- 正负混合

### 第三部分：加密 compare-and-swap

验证：

```text
(score_a, label_a)
(score_b, label_b)
```

能正确变成：

```text
(min_score, corresponding_label)
(max_score, corresponding_label)
```

至少随机测试 100 组。

## 6. BFV 相关文件的保留方式

下面这些文件都应保留，不删除：

- `polynomial_distance_plain_test.cpp`
- `openfhe_polynomial_api_test.cpp`
- `openfhe_coefficient_extraction_boundary_test.cpp`
- `openfhe_bfv_to_lwe_test.cpp`

但它们不再属于正式主线实现，而应分成两类：

### 正确性证明

- `polynomial_distance_plain_test.cpp`
- `openfhe_polynomial_api_test.cpp`

### 技术边界证明

- `openfhe_coefficient_extraction_boundary_test.cpp`
- `openfhe_bfv_to_lwe_test.cpp`

这组文件的作用是：

- 证明为什么 BFV 路线前半段成立
- 证明为什么在当前工程约束下最终仍然停止 BFV 主线

## 7. 最终结论

当前项目的正式判断应固定为：

```text
BFV 密文–明文多项式乘法能够正确承载 Cong 的内积编码，
但 OpenFHE 当前公开接口没有提供可直接将 BFV 目标系数
转换为 BinFHE LWE 密文的完整公共路径。

在不维护 OpenFHE 私有补丁的约束下，
项目正式转用 OpenFHE 原生支持的 CKKS→FHEW 路线。
```
