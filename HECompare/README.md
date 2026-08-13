# HECompare

这个目录用于承载 Kona 仓库里的 HE 对比组正式实现。

当前阶段先保持简单：

- `Machines/kona-he.cpp` 只放正式入口
- `HECompare/` 放 HE 相关实现和测试
- `docs/he/` 放中文设计、构建和实验文档

当前目录中应优先保留的内容：

- `CMakeLists.txt`
- `tests/openfhe_smoke.cpp`

后续再按功能逐步增加：

- `include/hecompare/`
- `src/`
- `tests/`

推进原则：

- 不要为了“看起来完整”提前创建大量空文件
- 先让环境冒烟、最小功能和最小 benchmark 跑稳
- 再按“距离 -> 比较 -> Top-k -> client/server”的顺序增加模块
