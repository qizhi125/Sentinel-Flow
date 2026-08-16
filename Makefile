# 根构建入口。可用 make build GO=/path/to/go 覆盖工具路径。
CARGO ?= cargo
GO ?= go
BIN_DIR := bin

.PHONY: build web daemon test clean

# 先编译 C++ 静态库（由 crates/ffi/build.rs 驱动），再构建 Rust 与 Go。
build:
	$(CARGO) build --workspace
	$(GO) build -o $(BIN_DIR)/sentinel-web ./go/cmd/sentinel-web

# 运行已构建的控制面（依赖 build，保证 bin/sentinel-web 存在）。
web: build
	./$(BIN_DIR)/sentinel-web

daemon: build
	$(CARGO) run --bin sentineld -- $(ARGS)

test:
	$(CARGO) test --workspace
	$(GO) test ./go/...

clean:
	$(CARGO) clean
	rm -rf $(BIN_DIR)
