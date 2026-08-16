//! 数据面入口：离线 pcap 输入，JA3 告警经 HTTP 输出。

use sentinel_api::Client;
use sentinel_capture::{flow5, tls_client_hello, PcapReader};
use sentinel_detect::{load_rules, Detector, Ja3Detector};
use std::{env, path::PathBuf, process::ExitCode};

fn usage() -> ExitCode {
    eprintln!(
        "用法: sentineld --pcap <file.pcap> [--rules data/ja3_rules.tsv] [--api http://localhost:21318]"
    );
    ExitCode::FAILURE
}

fn main() -> ExitCode {
    let mut pcap: Option<PathBuf> = None;
    let mut api = String::from("http://localhost:21318");
    let mut rules_path = PathBuf::from("data/ja3_rules.tsv");
    let mut args = env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--pcap" => match args.next() {
                Some(value) => pcap = Some(PathBuf::from(value)),
                None => return usage(),
            },
            "--api" => match args.next() {
                Some(value) => api = value,
                None => return usage(),
            },
            "--rules" => match args.next() {
                Some(value) => rules_path = PathBuf::from(value),
                None => return usage(),
            },
            _ => return usage(),
        }
    }
    let Some(path) = pcap else {
        return usage();
    };

    let mut reader = match PcapReader::open(&path) {
        Ok(reader) => reader,
        Err(e) => {
            eprintln!("打开 {} 失败: {e}", path.display());
            return ExitCode::FAILURE;
        }
    };
    let rules = match load_rules(&rules_path) {
        Ok(rules) => rules,
        Err(e) => {
            eprintln!("加载规则 {} 失败: {e}", rules_path.display());
            return ExitCode::FAILURE;
        }
    };
    let mut detector = Ja3Detector::new(rules);
    let client = Client::new(api);
    let mut frames = 0u64;

    loop {
        match reader.next_frame() {
            Ok(Some(frame)) => {
                frames += 1;
                if let Some(flow) = flow5(&frame.data, frame.linktype) {
                    if let Some(hello) = tls_client_hello(&frame.data, frame.linktype) {
                        detector.on_tls(frame.ts_millis, &flow, hello);
                    }
                }
            }
            Ok(None) => break,
            Err(e) => {
                eprintln!("读取 pcap 失败: {e}");
                return ExitCode::FAILURE;
            }
        }
    }

    let alerts = detector.drain();
    let mut ingested = 0usize;
    for alert in &alerts {
        println!(
            "ALERT ts={} rule=\"{}\" {}:{} -> {}:{} fp={}",
            alert.timestamp,
            alert.rule,
            alert.src_ip,
            alert.src_port,
            alert.dst_ip,
            alert.dst_port,
            alert.fingerprint
        );
        match client.post_alert(alert) {
            Ok(()) => ingested += 1,
            Err(e) => eprintln!("规则 \"{}\" 上报失败: {e}", alert.rule),
        }
    }
    println!(
        "frames={frames} alerts={} ingested={ingested}",
        alerts.len()
    );
    ExitCode::SUCCESS
}
