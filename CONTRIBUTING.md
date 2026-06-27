# Contributing to Sentinel-Flow

感谢你对 Sentinel-Flow 的关注！🛡️

## 分支策略

本项目采用简化 Git 分支模型。功能开发在功能分支上进行，通过 PR 合并至 `main`。

| 分支 | 用途 | 直接提交 |
|------|------|----------|
| `main` | 稳定发布（只读） | 禁止 |

### 功能分支命名规范

| 前缀 | 用途 | 示例 |
|------|------|------|
| `feat/*` | 新功能 | `feat/dpi-http-parser` |
| `fix/*` | Bug 修复 | `fix/spsc-race-condition` |
| `docs/*` | 文档重构 | `docs/system-restructuring` |
| `refactor/*` | 代码重构 | `refactor/lock-free-pipeline` |
| `chore/*` | 维护任务 | `chore/update-dependencies` |

### Pull Request 流程

- 所有 PR 必须基于 `main` 分支
- PR 标题遵循 Conventional Commits 规范（`feat:`, `fix:`, `docs:`, `refactor:`, `chore:` 等）
- 合并前必须通过所有测试（C++ ctest + Go test）
- 功能分支在合并后应删除

## 行为准则

本项目遵循[贡献者公约](CODE_OF_CONDUCT.md)。参与本项目即表示你同意遵守该准则。

## 参与方式

### 报告 Bug

- 使用 GitHub Issues 追踪器
- 包含：OS 版本、编译器版本、复现步骤、预期行为 vs 实际行为
- 附上相关日志或 PCAP 样本（经过脱敏处理）

### 建议功能

- 以 `enhancement` 标签创建 GitHub Issue
- 描述使用场景及其对项目的价值
- 实现前先讨论，确保与项目路线图一致

### Pull Request 流程

1. Fork 仓库并从 `main` 创建功能分支
2. 确保代码遵循项目风格规范（C++20 Google Style、Go gofmt）
3. 为新功能添加单元测试
4. 运行全部测试并确保通过
5. 如变更影响用户行为，同步更新文档
6. 提交针对 `main` 分支的 PR

### 开发环境搭建

```bash
# 依赖安装 (Fedora)
sudo dnf install cmake gcc-c++ libpcap-devel sqlite-devel golang

# 依赖安装 (Ubuntu)
sudo apt-get install cmake g++ libpcap-dev libsqlite3-dev golang

# C++ 构建
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target sentinel_core -j$(nproc)

# C++ 测试
cmake --build build --target sentinel_tests -j$(nproc)
cd build && ctest --output-on-failure

# Go 构建
go mod tidy
go build -o sentinel-cli ./cmd/sentinel

# Go 测试
go test ./pkg/engine/ -v
```

### 代码风格

- **C++**: C++20, Google C++ Style Guide
- **Go**: `gofmt` 标准格式化
- 使用 `#pragma once` 替代传统 include guard（C++ 头文件）
- 偏好 `snake_case` 函数名，`PascalCase` 类名
- 公开 API 添加 Doxygen 风格注释

### 提交消息格式

遵循 [Conventional Commits](https://www.conventionalcommits.org/)：

```
feat: 增加 XDP 零拷贝包过滤
fix: 修复 SPSC 队列竞争条件
docs: 更新架构图
test: 添加 AC 自动机边界用例测试
```

## 许可证

贡献即表示你同意将贡献内容以 MIT 许可证授权。
