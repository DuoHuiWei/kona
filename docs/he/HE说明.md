# HE说明

本文记录 Kona 仓库里接入 HE 对比组时，应该长期记住的事情。这里尽量写稳定约束，不写短期临时决定。

## 1. 目标定位

这里的 HE 指的是一个独立的单服务器 client-server 对比组，不是把 OpenFHE 直接塞进现有双服务器 MPC 主协议。

当前阶段的主要目标是：

- 保持 Kona 主线可继续按原方式编译和跑实验
- 在同一仓库内增加一个可独立编译的 HE 可执行目标
- 优先复用 Kona 现有的数据、计时、序列化和 benchmark 输出习惯
- 先把环境、构建、最小可运行目标接稳，再逐步补正式业务逻辑

## 2. 工作流约束

- 源代码的长期修改发生在 WSL 仓库里：`/home/u7231/kona-work/Kona`
- 编译和实验主要发生在 Docker 容器中
- 不要在 WSL 本机直接安装 Kona/Garnet 依赖来替代容器环境
- 不要只在容器里改代码而不回写到 WSL 仓库

这意味着：

- OpenFHE 能不能用，最终要看 Docker 里能不能找到它
- `find_package(OpenFHE)` 能成功，不等于 Kona 的 `Makefile` 目标已经自动可用
- 容器里的实验环境和仓库里的源码组织必须一起设计

## 3. 当前构建现实

Kona 当前主构建系统是根目录 `Makefile`，不是统一的 CMake 工程。

所以现在要区分两件事：

1. OpenFHE 自己的推荐接法：
   `find_package(OpenFHE CONFIG REQUIRED)`
2. Kona 当前工程的接法：
   在 `Makefile` 里给单独目标补 include 和 link 规则

这两者并不冲突。当前更贴合工程现实的方式是：

- `Makefile` 继续负责 Kona 主工程
- OpenFHE 作为外部库被某个新目标单独链接
- 新目标先最小化，再逐步复用 Kona 组件

## 4. 为什么要单独做 `kona-he.x`

新增 `kona-he.x` 的意义不是为了另起一套工程，而是为了把风险隔离开。

这样做的好处：

- 不污染 `kona.x`
- 不需要立刻把整个工程迁移到 CMake
- OpenFHE 只影响新的 HE 目标
- 后续如果更换 HE 实现或者参数，不会牵连原始 Kona 基线

因此，现阶段应优先把：

- `kona.x`
- `kona-he.x`

看成两个并列目标，而不是让后者立即改写前者。

## 5. 当前目录语义

当前推荐的收敛结构是：

```text
Kona/
├── Dockerfile.openfhe
├── Machines/
│   └── kona-he.cpp
├── HECompare/
│   ├── CMakeLists.txt
│   ├── README.md
│   └── tests/
│       └── openfhe_smoke.cpp
└── docs/
    └── he/
        ├── README.md
        ├── HE说明.md
        ├── HE规划建议.md
        └── 复用和文件结构建议.md
```

这里的核心思想是：

- `Machines/` 只保留一个正式入口
- `HECompare/` 集中放 HE 代码和测试
- `docs/he/` 集中放项目内文档
- `references/HE/` 以后只保留外部参考材料

## 6. 当前已经验证过的事情

截至 2026-07-31，已经单独验证过以下环境链路：

- `kona-openfhe:1.5.1` 镜像可成功构建
- OpenFHE 1.5.1 已安装到 `/opt/openfhe`
- `OpenFHEConfig.cmake` 可在容器中找到
- `find_package(OpenFHE CONFIG REQUIRED)` 可成功
- BinFHE smoke 可输出正确结果 `1`
- `make kona-he.x` 可通过 `Makefile` 编译并运行
- 新镜像中的原始 Kona 基线仍可编译并运行 Arcene smoke

这说明当前最小环境接入已经成立。

## 7. 当前还没有做的事情

下面这些事情还没有完成，不要默认它们已经具备：

- HE 正式 KNN 距离计算
- HE 客户端/服务器协议
- 与 Kona 数据读取逻辑的正式复用封装
- HE benchmark 字段和现有 Kona 输出的完全对齐
- HE 与 Cong/PCR/DCF 的公平对比实验矩阵
- `HECompare/include`、`HECompare/src` 等正式模块化目录还没有建立

也就是说，当前只是“环境和最小目标可用”，还不是“HE 对比组已经实现完成”。

## 8. 近期最重要的工程原则

- 先让新目标稳定编译和运行，再谈业务复杂度
- 先做最小复用，再做深层复用
- 先保住原 Kona 基线，再扩大 HE 改动范围
- 先固定 benchmark 口径，再做大规模实验

如果未来某一步开始让事情变复杂，优先回到这四条判断是否还成立。
