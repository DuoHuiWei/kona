# CKKS / HE 实验运行清单（合并整理版，2026-08-05）

本文是当前仓库中 **CKKS / HE 相关实验数据** 的主整理文档，合并并替代以下两份旧整理：

- `/home/u7231/kona-work/Kona/reports/ckks_experiment_summary_from_dialog.md`
- `/home/u7231/kona-work/Kona/docs/he/ckks_experiment_summary_20260804.md`

整理原则：

- 只保留本轮工作中 **已经确认、已经落盘、或者已经形成稳定结论** 的内容。
- 关于 compare-resolution / scaleSign 的多轮试探、失败 case、历史扫参过程不再展开。
- 分辨率部分只保留 **最终采纳** 的 benchmark profile。

---

## 1. 当前统一口径

### 1.1 数据口径

- 数据源：`HECompare/testdata/tcga-pancan-raw/data.csv`
- 标签：`HECompare/testdata/tcga-pancan-raw/labels.csv`
- 样本数：`801`
- 特征数：`20531`
- 类别数：`5`
- leave-one-out：query 与 train 排除自身

### 1.2 距离口径

统一使用：

```text
D(x, q) = sum((x / 21 - q / 21)^2) / 20531
```

也就是：

- 先做全局缩放：`x_global = x / 21`
- 再做平方欧氏距离
- 最后除以 `20531`

### 1.3 最终采纳的 CKKS / compare benchmark profile

当前保留并采纳的 profile 是：

- `slot_count = 4096`
- `ring_dimension = 8192`
- `multiplicative_depth = 20`
- `scaleSign = 262144`
- `pLWE = 131072`
- `scaling_technique = FLEXIBLEAUTOEXT`
- `fhew_parameter_set = STD128`
- `chunk_count = 6`
- `chunk_layout = 4096 + 4096 + 4096 + 4096 + 4096 + 51`

说明：

- 这是当前工程上采纳的 compare / benchmark 基线。
- 文档中不再展开历史上的 `64 / 128 / 32768 / 8388608` 等试探过程。

---

## 2. 结果文件总览

### 2.1 CKKS 距离 / top-k / 分布分析结果

目录：

- `/home/u7231/kona-work/Kona/KNN-experiment-res/`

主要文件：

- `he_ckks_tcga_distance_bench.log`
- `he_ckks_tcga_query0_distances.csv`
- `he_ckks_tcga_query0_minimal.csv`
- `he_ckks_tcga_query0_sorted_by_distance.csv`
- `he_ckks_tcga_query0_topk_5.csv`
- `he_ckks_tcga_query0_topk_8.csv`
- `he_ckks_tcga_query0_topk_50.csv`
- `he_ckks_plaintext_distance_distribution_report.txt`
- `he_ckks_plaintext_distance_histogram.svg`
- `he_ckks_plaintext_distance_bell_curve.svg`
- `he_ckks_plaintext_distance_by_label_report.txt`
- `he_ckks_plaintext_distance_by_label_summary.csv`
- `he_ckks_plaintext_distance_sorted_histogram.svg`
- `he_ckks_vs_plain_distribution_report.txt`
- `he_ckks_distance_histogram.svg`
- `he_ckks_distance_bell_curve.svg`
- `he_ckks_abs_error_histogram.svg`
- `he_ckks_abs_error_bell_curve.svg`

### 2.2 packed compare fixture

目录：

- `/home/u7231/kona-work/Kona/HECompare/testdata/packed_compare_fixture_16/`

当前可用文件：

- `plaintext_cases.csv`
- `lhs_score.bin`
- `lhs_score.json`

说明：

- `plaintext_cases.csv` 是当前推荐使用的固定 fixture 输入源。
- 密文完整序列化导出目前未稳定打通，因此 packed compare 正确性验证建议采用：
  - 读取 `plaintext_cases.csv`
  - 启动时现场加密
  - 再做 packed compare-and-swap

---

## 3. 已跑通的测试 / benchmark 入口

### 3.1 基础与闭环测试

- `openfhe-smoke`
- `openfhe-polynomial-api-test`
- `openfhe-public-schemeswitch-test`
- `openfhe-ckks-validation-test`
- `openfhe-ckks-distance-compare-test`
- `openfhe-ckks-compare-chain-test`
- `openfhe-tcga-data-loader-test`
- `openfhe-tcga-full-feature-distance-test`
- `openfhe-tcga-distance-to-compare-test`

### 3.2 重点 benchmark / 分析入口

- `HECompare/tests/openfhe_tcga_ckks_distance_benchmark.cpp`
- `HECompare/tests/openfhe_ckks_single_compare_swap_bench.cpp`
- `HECompare/tests/openfhe_tcga_packed_compare_test.cpp`
- `Scripts/he_ckks_topk_summary.py`
- `Scripts/analyze_he_ckks_plaintext_distance_distribution.py`
- `Scripts/analyze_he_ckks_distance_by_label.py`
- `Scripts/analyze_he_ckks_vs_plain_distribution.py`

---

## 4. TCGA query0 的 CKKS 距离实验

### 4.1 小规模先验验证：先跑 12 个 train

日志：

- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_tcga_distance_bench.log`

关键结果：

- query 样本：`sample_0`
- query 标签：`PRAD`
- `TRAIN_DISTANCE_COUNT = 12`
- `KEYGEN_SECONDS = 5.035877503`
- `ENCRYPT_QUERY_SECONDS = 0.143110105`
- `DISTANCE_COMPUTE_TOTAL_SECONDS = 75.70155846`
- `AVG_DISTANCE_COMPUTE_MS = 6308.463205`
- `DECRYPT_TOTAL_SECONDS = 0.103904907`
- `AVG_DECRYPT_MS = 8.65874225`
- `MAX_ABS_ERROR = 1.3309336271971617e-12`
- `HE_CKKS_TCGA_DISTANCE_BENCH = PASS`

结论：

- 单 query、单次加密、多候选复用路径已经跑通。
- 单个距离在这组小样本测量下约 `6.308 s`。

### 4.2 全量结果：query0 对 800 个 train

主结果文件：

- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_tcga_query0_distances.csv`

补充日志：

- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_tcga_distance_remaining_from_13.log`

最终结果：

- CSV 总行数：`801`
  - `1` 行表头
  - `800` 行距离结果
- 每行字段：
  - `train_idx`
  - `train_sample_id`
  - `label`
  - `plaintext_distance`
  - `ckks_distance`
  - `abs_error`
  - `level`
  - `scale`

关键数值：

- `KEYGEN_SECONDS = 4.91769666`
- `ENCRYPT_QUERY_SECONDS = 0.156078733`
- `DISTANCE_COMPUTE_TOTAL_SECONDS = 4123.802943`
- `AVG_DISTANCE_COMPUTE_MS = 5440.373276`
- `DECRYPT_TOTAL_SECONDS = 6.339605947`
- `AVG_DECRYPT_MS = 8.363596236`
- `MAX_ABS_ERROR = 3.01111219291883e-12`
- `TOTAL_SECONDS = 4137.043302`
- `HE_CKKS_TCGA_DISTANCE_BENCH = PASS`

结论：

- 800 条 CKKS 距离结果已完整落盘。
- `ckks_distance` 与 `plaintext_distance` 偏差保持在 `1e-12` 量级。

---

## 5. query0 的 top-k 整理与投票结果

生成文件：

- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_tcga_query0_minimal.csv`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_tcga_query0_sorted_by_distance.csv`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_tcga_query0_topk_5.csv`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_tcga_query0_topk_8.csv`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_tcga_query0_topk_50.csv`

query 标签：

- `PRAD`

投票结果：

- `k = 5`
  - 投票结果：`PRAD`
  - 计数：`PRAD:5`
  - 正确性：`YES`
- `k = 8`
  - 投票结果：`PRAD`
  - 计数：`PRAD:8`
  - 正确性：`YES`
- `k = 50`
  - 投票结果：`PRAD`
  - 计数：`PRAD:50`
  - 正确性：`YES`

结论：

- query0 的 top-50 最近邻全部为 `PRAD`。

---

## 6. 距离分布分析

### 6.1 明文距离整体分布

文件：

- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_plaintext_distance_distribution_report.txt`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_plaintext_distance_histogram.svg`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_plaintext_distance_bell_curve.svg`

关键统计：

- `count = 800`
- `mean = 0.008565722781519`
- `variance = 5.934972865437084e-06`
- `stddev = 0.002436179973942`
- `min = 0.002261330528312`
- `q1 = 0.007537967735206`
- `median = 0.008974825839297`
- `q3 = 0.010219775791518`
- `max = 0.018334538958751`
- `skewness = -0.631169762499708`

### 6.2 按标签的明文距离分组

文件：

- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_plaintext_distance_by_label_report.txt`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_plaintext_distance_by_label_summary.csv`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_plaintext_distance_sorted_histogram.svg`

分组结论：

- `PRAD`
  - `count = 135`
  - `mean = 0.004258528036187`
  - `median = 0.004151998047912`
  - `min = 0.002261330528312`
  - `max = 0.009856877470534`
- `LUAD`
  - `count = 141`
  - `mean = 0.009236574027861`
- `BRCA`
  - `count = 300`
  - `mean = 0.008598682482016`
- `KIRC`
  - `count = 146`
  - `mean = 0.010988575879116`
  - `max = 0.018334538958751`
- `COAD`
  - `count = 78`
  - `mean = 0.010145938198021`

结论：

- query0 为 `PRAD`，其同类 `PRAD` 距离最小且分布显著更靠左。
- `KIRC` 的均值和最大值最高。

### 6.3 CKKS vs 明文分布对比与误差

文件：

- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_vs_plain_distribution_report.txt`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_distance_histogram.svg`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_distance_bell_curve.svg`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_abs_error_histogram.svg`
- `/home/u7231/kona-work/Kona/KNN-experiment-res/he_ckks_abs_error_bell_curve.svg`

关键统计：

- `plaintext mean = 0.008565722781519`
- `ckks mean = 0.008565722781530`
- `plaintext variance = 5.934972865437084e-06`
- `ckks variance = 5.934972865703223e-06`
- `abs error mean = 7.169931163543419e-13`
- `abs error max = 3.011112192918830e-12`

结论：

- `ckks_distance` 与 `plaintext_distance` 的整体分布几乎重合。
- 误差集中在 `1e-12` 量级。

---

## 7. 单次标量 compare-and-swap benchmark

入口：

- `/home/u7231/kona-work/Kona/HECompare/tests/openfhe_ckks_single_compare_swap_bench.cpp`

用途：

- 将一次性 setup 与真正在线 compare-and-swap 分开统计。

运行口径：

- `repeats = 5`

结果：

- `SETUP_MS = 498.106446`
- `ENCRYPTION_MS = 68.492455`
- `COMPARE_TOTAL_MS = 11122.093905`
- `COMPARE_AVG_MS = 2224.418781`
- `SWAP_TOTAL_MS = 887.994781`
- `SWAP_AVG_MS = 177.5989562`
- `DECRYPT_TOTAL_MS = 203.23753`
- `DECRYPT_AVG_MS = 40.647506`
- `ONLINE_TOTAL_MS = 12010.088686`
- `ONLINE_AVG_MS = 2402.0177372`
- `CKKS_SINGLE_COMPARE_SWAP = PASS`

说明：

- 这里的在线时间不包含密钥生成和 scheme-switch 预计算。

---

## 8. packed compare-and-swap benchmark

入口：

- `/home/u7231/kona-work/Kona/HECompare/tests/openfhe_tcga_packed_compare_test.cpp`

### 8.1 active_count = 1

- `setup_ms = 4510.048183`
- `compare_ms = 8479.406398`
- `swap_ms = 9397.130356`
- `PACKED_BINARY_COMPARE_AND_SWAP = PASS`

### 8.2 active_count = 16

- `setup_ms = 4814.98112`
- `compare_ms = 11405.083854`
- `swap_ms = 11667.658205`
- `PACKED_BINARY_COMPARE_AND_SWAP = PASS`

按 lane 摊薄：

- `per-lane compare ms ≈ 712.817741`
- `per-lane swap ms ≈ 729.228638`

### 8.3 active_count = 32

- `setup_ms = 4842.218342`
- `compare_ms = 14836.819568`
- `swap_ms = 15341.069782`
- `compare_max_error = 0.00039191515434588986`
- `score_pass = YES`
- `label_pass = YES`
- `PACKED_BINARY_COMPARE_AND_SWAP = PASS`
- `Maximum resident set size = 14941464 kB`
- `Exit status = 0`

按 lane 摊薄：

- `per-lane compare ms ≈ 463.6506115`
- `per-lane swap ms ≈ 479.4084307`

### 8.4 active_count = 64

- `setup_ms = 4364.491029`
- `compare_ms = 24761.278341`
- `swap_ms = 26349.044645`
- `compare_max_error = 0.0010660520160654663`
- `score_pass = YES`
- `label_pass = YES`
- `PACKED_BINARY_COMPARE_AND_SWAP = PASS`
- `Maximum resident set size = 15055812 kB`
- `Exit status = 0`

按 lane 摊薄：

- `per-lane compare ms ≈ 386.8949741`
- `per-lane swap ms ≈ 411.7038226`

结论：

- 总时间随 `active_count` 增大而上升。
- 但单 lane 摊薄成本从 `1 -> 16 -> 32 -> 64` 持续下降。
- `32` 与 `64` 内存都在 `15 GB` 左右，未出现线性翻倍。

---

## 9. 16 槽固定 fixture

目录：

- `/home/u7231/kona-work/Kona/HECompare/testdata/packed_compare_fixture_16/`

当前推荐输入：

- `/home/u7231/kona-work/Kona/HECompare/testdata/packed_compare_fixture_16/plaintext_cases.csv`

说明：

- 已包含 16 组人工构造的：
  - `lhs_score`
  - `rhs_score`
  - `lhs_label`
  - `rhs_label`
  - `expected_compare`
  - `expected_min_score`
  - `expected_max_score`
  - `expected_min_label`
  - `expected_max_label`
- 偶数槽满足 `lhs < rhs`
- 奇数槽满足 `lhs > rhs`

当前建议工作流：

1. 固定使用 `plaintext_cases.csv`
2. 测试启动时现场加密为 packed CKKS 密文
3. 跑 packed compare 或 compare-and-swap
4. 用 `expected_*` 字段直接做正确性核对
5. setup 与 online compare/swap 分开计时

---

## 10. 相关脚本与入口清单

### 10.1 脚本

- `Scripts/build-he.sh`
- `Scripts/run-he-ckks-distance-bench.sh`
- `Scripts/start-he-ckks-distance-remaining-detached.sh`
- `Scripts/check-he-ckks-distance-remaining.sh`
- `Scripts/he_ckks_topk_summary.py`
- `Scripts/analyze_he_ckks_plaintext_distance_distribution.py`
- `Scripts/analyze_he_ckks_distance_by_label.py`
- `Scripts/analyze_he_ckks_vs_plain_distribution.py`

### 10.2 核心测试 / benchmark

- `HECompare/tests/openfhe_tcga_ckks_distance_benchmark.cpp`
- `HECompare/tests/openfhe_ckks_single_compare_swap_bench.cpp`
- `HECompare/tests/openfhe_tcga_packed_compare_test.cpp`
- `HECompare/tests/openfhe_generate_packed_compare_fixture.cpp`

