#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 计算 TLS ClientHello 的 JA3 指纹。`data` 必须指向完整的 TLS 握手记录，
// 成功时向 `out` 写入 32 个小写十六进制字符（含结尾 NUL）并返回 32，
// 输入非法或缓冲区小于 33 字节时返回 -1。
int sentinel_ja3_from_hello(const uint8_t* data, size_t len, char* out, size_t cap);

#ifdef __cplusplus
}
#endif

