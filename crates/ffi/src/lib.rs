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
    use sentinel_capture::{tls_client_hello, PcapReader};
    use std::path::PathBuf;

    fn fixture_path() -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../data/testdata/curl-enabled-tls13.pcap")
    }

    // 期望值由独立实现 stats/sim/ja3_verify.py 对同一真实抓包交叉验证得到：
    // JA3 字段串 "771,4866-4867-4865-255,0-11-10-13172-16-22-23-49-13-43-45-51-21,29-23-30-25-24,0-1-2"。
    #[test]
    fn ja3_matches_independent_reference() {
        let mut reader = PcapReader::open(&fixture_path()).expect("打开 pcap");
        loop {
            match reader.next_frame() {
                Ok(Some(frame)) => {
                    if let Some(hello) = tls_client_hello(&frame.data, frame.linktype) {
                        let ja3 = ja3_from_hello(hello).expect("有效 ClientHello");
                        assert_eq!(ja3, "eeadfd2f446a45ded96f804720a0c75b");
                        return;
                    }
                }
                Ok(None) => break,
                Err(e) => panic!("读取 pcap 失败: {e}"),
            }
        }
        panic!("夹具中未找到 ClientHello");
    }

    #[test]
    fn rejects_malformed_input() {
        assert!(ja3_from_hello(&[0x16, 0x03, 0x03]).is_none());
    }
}
