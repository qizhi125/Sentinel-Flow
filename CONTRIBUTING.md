# Contributing to Sentinel-Flow

Thank you for your interest in contributing to Sentinel-Flow! 🛡️

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
4. Run `make sentinel_tests` and ensure all tests pass
5. Update documentation if your change affects user-facing behavior
6. Submit a PR against the `develop` branch

### Development Setup

```bash
# Install dependencies (Fedora)
sudo dnf install cmake gcc-c++ libpcap-devel sqlite-devel libbpf-devel clang

# Build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

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
