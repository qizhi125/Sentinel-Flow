//! 数据面入口：离线 pcap 或实时网卡输入，JA3 告警经 HTTP 输出。

use sentinel_api::Client;
use sentinel_capture::{flow5, tls_client_hello, LiveReader, PcapReader};
use sentinel_detect::{load_rules, Alert, Detector, Ja3Detector, Ja3Rule};
use std::{env, path::PathBuf, process::ExitCode};

fn usage() -> ExitCode {
    eprintln!(
        "用法: sentineld (--pcap <file.pcap> | --interface <网卡>) [--rules data/ja3_rules.tsv] [--api http://localhost:21318]"
    );
    ExitCode::FAILURE
}

fn main() -> ExitCode {
    let mut pcap: Option<PathBuf> = None;
    let mut interface: Option<String> = None;
    let mut api = String::from("http://localhost:21318");
    let mut rules_path = PathBuf::from("data/ja3_rules.tsv");
    let mut args = env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--pcap" => match args.next() {
                Some(value) => pcap = Some(PathBuf::from(value)),
                None => return usage(),
            },
            "--interface" => match args.next() {
                Some(value) => interface = Some(value),
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
    if pcap.is_some() == interface.is_some() {
        return usage();
    }

    let rules = match load_rules(&rules_path) {
        Ok(rules) => rules,
        Err(e) => {
            eprintln!("加载规则 {} 失败: {e}", rules_path.display());
            return ExitCode::FAILURE;
        }
    };
    if let Some(name) = interface {
        return run_live(name, api, rules);
    }
    let path = pcap.expect("已确认 pcap 模式");
    let mut reader = match PcapReader::open(&path) {
        Ok(reader) => reader,
        Err(e) => {
            eprintln!("打开 {} 失败: {e}", path.display());
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

// 实时模式：绑定网卡逐帧检测并即时上报，Ctrl-C 结束。
fn run_live(interface: String, api: String, rules: Vec<Ja3Rule>) -> ExitCode {
    let mut reader = match LiveReader::open(&interface) {
        Ok(reader) => reader,
        Err(e) => {
            eprintln!("打开网卡 {interface} 失败: {e}");
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
                for alert in detector.drain() {
                    emit_alert(&alert, &client);
                }
            }
            Ok(None) => continue,
            Err(e) => {
                eprintln!("抓包失败（已处理 {frames} 帧）: {e}");
                return ExitCode::FAILURE;
            }
        }
    }
}

// 打印并上报一条告警；上报失败只记录日志，不中断处理。
fn emit_alert(alert: &Alert, client: &Client) {
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
    if let Err(e) = client.post_alert(alert) {
        eprintln!("规则 \"{}\" 上报失败: {e}", alert.rule);
    }
}
