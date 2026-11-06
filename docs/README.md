# Docs Directory

这个目录用于存放正式设计文档、实验计划、运行说明和结果解释。

当前四方案实验只有 `Machines/knn-bench.cpp` 和 `Scripts/run-knn-experiment.py`。直接启动Python脚本，使用 `--scheme` 选择方案。

当前入口索引：

- [Bench与实验脚本分类清单](bench_inventory_zh.md)：各入口用途、数量、48组历史主实验及清理候选。
- [Bench统一记录口径](bench_recording_protocol.md)：准备项、计时边界、双方时间/字节与轮数记录要求。
- [矩阵公共模块与统一bench](matrix_modules.md)：当前C++模块和调用方式。
- [统一网络Profile](network_profiles.md)：5个网络档位、统一配置入口及实际验证结果。

建议放入的内容：

- `kona_he_design_zh.md`：HE 对比组设计说明
- `kona_he_benchmark_plan_zh.md`：实验矩阵、指标口径和运行命令
- 后续的结果解读、复现实验说明和对比分析文档

不建议放入的内容：

- 早期头脑风暴草稿
- 与具体实现强绑定、还未稳定的零散备注

约定：

- 前期方案和思路草稿优先放在 `references/`
- 在方案稳定后，再整理成这里的正式文档

- [ConstantPP与Shao相关入口的在线统计和运行方法](reference_benchmarking.md)

- [Bench固定CPU绑定规则、迁移清单与核验](bench_cpu_affinity.md)
