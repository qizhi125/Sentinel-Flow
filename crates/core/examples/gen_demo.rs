//! 生成垂直切片冒烟测试用的合成 pcap。

use std::{env, path::PathBuf};

fn main() {
    let path = env::args()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/tmp/sentinel-demo.pcap"));
    sentinel_capture::synth::write_demo_pcap(&path).expect("写演示 pcap 失败");
    println!("已写入 {}", path.display());
}
