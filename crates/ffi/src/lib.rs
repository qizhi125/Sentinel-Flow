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

#[cfg(test)]
mod tests {
    use super::*;

    fn minimal_hello_record() -> Vec<u8> {
        let mut hello = Vec::new();
        hello.extend_from_slice(&[0x03, 0x03]);
        hello.extend_from_slice(&[0u8; 32]);
        hello.push(0);
        hello.extend_from_slice(&2u16.to_be_bytes());
        hello.extend_from_slice(&0x1301u16.to_be_bytes());
        hello.push(1);
        hello.push(0);

        let hs_len = hello.len() as u32;
        let mut record = Vec::new();
        record.extend_from_slice(&[0x16, 0x03, 0x03]);
        record.extend_from_slice(&((hs_len + 4) as u16).to_be_bytes());
        record.push(0x01);
        record.extend_from_slice(&hs_len.to_be_bytes()[1..]);
        record.extend_from_slice(&hello);
        record
    }

    #[test]
    fn ja3_matches_known_md5() {
        let ja3 = ja3_from_hello(&minimal_hello_record()).expect("valid ClientHello");
        assert_eq!(ja3, "ea1e247991e541e39bf918cb7cfa5139");
    }

    #[test]
    fn rejects_malformed_input() {
        assert!(ja3_from_hello(&[0x16, 0x03, 0x03]).is_none());
    }
}
