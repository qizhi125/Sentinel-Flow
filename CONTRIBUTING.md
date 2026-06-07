# Contributing to Sentinel-Flow

Thank you for your interest in contributing to Sentinel-Flow! 🛡️

## Branching Strategy

本项目采用简化 Git 分支模型。所有开发工作均在 `develop` 分支上集成，稳定发布部署至 `main` 分支。

| 分支 | 用途 | 直接提交 |
|------|------|----------|
| `main` | 生产稳定版（只读） | 禁止 |
| `develop` | 集成分支（所有开发的基准） | 仅限 CI/文档 |

### 功能分支命名规范

| 前缀 | 用途 | 示例 |
|------|------|------|
| `feat/*` | 新功能 | `feat/dpi-http-parser` |
| `fix/*` | Bug 修复 | `fix/spsc-race-condition` |
| `docs/*` | 文档重构 | `docs/system-restructuring` |
| `refactor/*` | 代码重构 | `refactor/lock-free-pipeline` |
| `chore/*` | 维护任务 | `chore/update-dependencies` |

### Pull Request 流程

- **所有 PR 必须基于 `develop` 分支**
- PR 标题遵循 Conventional Commits 规范（`feat:`, `fix:`, `docs:`, `refactor:`, `chore:` 等）
- 合并前必须通过所有 CI 检查（构建、测试、格式检查、lint）
- 功能分支在合并后应删除

## Code of Conduct

This project adheres to the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code.

## How to Contribute

### Reporting Bugs

- Use the GitHub Issues tracker
- Include: OS version, compiler version, steps to reproduce, expected vs actual behavior
- Attach relevant logs or PCAP samples (sanitized)

### Suggesting Features

- Open a GitHub Issue with the `enhancement` label
- Describe the use case and how it benefits the project
- Discuss before implementing to align with project roadmap

### Pull Request Process

1. Fork the repository and create a feature branch from `develop`
2. Ensure your code follows the project's C++20 style (Google C++ Style Guide)
3. Add unit tests for new functionality
4. Run `cmake --build build --target sentinel_tests -j$(nproc) && cd build && ctest --output-on-failure` and ensure all tests pass
5. Update documentation if your change affects user-facing behavior
6. Submit a PR against the `develop` branch

### Development Setup

```bash
# Install dependencies (Fedora)
sudo dnf install cmake gcc-c++ libpcap-devel sqlite-devel libbpf-devel clang

# Build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -j$(nproc)

# Run tests
./tests/sentinel_tests
```

### Code Style

- **C++**: C++20, Google C++ Style Guide
- **Go**: Standard `gofmt` formatting
- Use `#pragma once` for headers
- Prefer `snake_case` for functions, `PascalCase` for classes
- Add Doxygen-style comments for public APIs

### Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):
```
feat: add XDP zero-copy packet filtering
fix: resolve race condition in SPSC queue
docs: update architecture diagram
test: add AC automaton corner case test
```

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
