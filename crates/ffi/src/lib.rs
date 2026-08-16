//! C++ 算法库的 C ABI 绑定与安全封装。

use std::os::raw::c_int;

unsafe extern "C" {
    // 注意：C++ 侧对 data 的读取与 out 的写入都做了边界检查。
    fn sentinel_ja3_from_hello(data: *const u8, len: usize, out: *mut u8, cap: usize) -> c_int;
}

// 返回 32 位 JA3 十六进制指纹；输入不是 ClientHello 时返回 None。
pub fn ja3_from_hello(hello: &[u8]) -> Option<String> {
    if hello.len() < 5 {
        return None;
    }
    let mut out = vec![0u8; 33];
    // 注意：hello 在整个调用期间有效，out 是可写的 33 字节缓冲，
    // C 函数写入前会检查自身边界，因此该 unsafe 调用是安全的。
    let n = unsafe {
        sentinel_ja3_from_hello(hello.as_ptr(), hello.len(), out.as_mut_ptr(), out.len())
    };
    if n != 32 {
        return None;
    }
    out.truncate(32);
    String::from_utf8(out).ok()
}
