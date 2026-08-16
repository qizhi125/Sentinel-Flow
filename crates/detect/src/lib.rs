//! 检测插件。本切片只实现 JA3 检测器。

use sentinel_capture::Flow;
use std::collections::HashSet;
use std::fs;
use std::io;
use std::path::Path;

// 一条结构化告警：五元组、时间、规则名、严重度与命中指纹。
pub struct Alert {
    pub timestamp: i64,
    pub src_ip: String,
    pub dst_ip: String,
    pub src_port: u16,
    pub dst_port: u16,
    pub rule: String,
    pub severity: u8,
    pub fingerprint: String,
}

// 一条 JA3 规则：指纹、名称、严重度与情报来源。
pub struct Ja3Rule {
    pub fingerprint: String,
    pub name: String,
    pub severity: u8,
    pub source: String,
}

pub trait Detector {
    fn name(&self) -> &'static str;
    fn on_tls(&mut self, ts_millis: i64, flow: &Flow, hello: &[u8]);
    fn drain(&mut self) -> Vec<Alert>;
}

pub struct Ja3Detector {
    rules: Vec<Ja3Rule>,
    pending: Vec<Alert>,
    seen: HashSet<(String, String, u16)>,
}

impl Ja3Detector {
    pub fn new(rules: Vec<Ja3Rule>) -> Self {
        Self {
            rules,
            pending: Vec::new(),
            seen: HashSet::new(),
        }
    }
}

// 加载制表符分隔的规则文件：fingerprint<TAB>name<TAB>severity<TAB>source。
// 空行与以 # 开头的行忽略；字段数或指纹格式错误时返回错误。
pub fn load_rules(path: &Path) -> io::Result<Vec<Ja3Rule>> {
    let text = fs::read_to_string(path)?;
    let mut rules = Vec::new();
    for (index, line) in text.lines().enumerate() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let fields: Vec<&str> = line.split('\t').collect();
        if fields.len() != 4 {
            return Err(io::Error::other(format!(
                "规则文件第 {} 行字段数错误（应为 4 列）",
                index + 1
            )));
        }
        let fingerprint = fields[0].trim().to_string();
        if fingerprint.len() != 32 || !fingerprint.chars().all(|c| c.is_ascii_hexdigit()) {
            return Err(io::Error::other(format!(
                "规则文件第 {} 行指纹格式错误",
                index + 1
            )));
        }
        let severity = fields[2]
            .trim()
            .parse::<u8>()
            .map_err(|_| io::Error::other(format!("规则文件第 {} 行严重度格式错误", index + 1)))?;
        rules.push(Ja3Rule {
            fingerprint,
            name: fields[1].trim().to_string(),
            severity,
            source: fields[3].trim().to_string(),
        });
    }
    Ok(rules)
}

impl Detector for Ja3Detector {
    fn name(&self) -> &'static str {
        "ja3"
    }

    fn on_tls(&mut self, ts_millis: i64, flow: &Flow, hello: &[u8]) {
        let Some(fingerprint) = sentinel_ffi::ja3_from_hello(hello) else {
            return;
        };
        let key = (fingerprint.clone(), flow.src_ip.clone(), flow.src_port);
        if !self.seen.insert(key) {
            return;
        }
        for rule in &self.rules {
            if rule.fingerprint == fingerprint {
                self.pending.push(Alert {
                    timestamp: ts_millis,
                    src_ip: flow.src_ip.clone(),
                    dst_ip: flow.dst_ip.clone(),
                    src_port: flow.src_port,
                    dst_port: flow.dst_port,
                    rule: rule.name.to_string(),
                    severity: rule.severity,
                    fingerprint,
                });
                break;
            }
        }
    }

    fn drain(&mut self) -> Vec<Alert> {
        std::mem::take(&mut self.pending)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use sentinel_capture::{flow5, tls_client_hello, PcapReader};
    use std::path::PathBuf;

    fn data_dir() -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../data")
    }

    #[test]
    fn loads_rules_from_data_file() {
        let rules = load_rules(&data_dir().join("ja3_rules.tsv")).expect("加载规则");
        assert!(!rules.is_empty());
        for rule in &rules {
            assert_eq!(rule.fingerprint.len(), 32);
            assert!(!rule.name.is_empty());
            assert!(!rule.source.is_empty());
        }
    }

    // 夹具为 Arkime 测试库中的真实良性抓包，不应命中任何 C2 规则。
    #[test]
    fn benign_fixture_produces_no_alert() {
        let fixture = data_dir().join("testdata/curl-enabled-tls13.pcap");
        let rules = load_rules(&data_dir().join("ja3_rules.tsv")).expect("加载规则");
        let mut detector = Ja3Detector::new(rules);
        let mut reader = PcapReader::open(&fixture).expect("打开 pcap");
        let mut found_hello = false;
        loop {
            match reader.next_frame() {
                Ok(Some(frame)) => {
                    if let Some(flow) = flow5(&frame.data, frame.linktype) {
                        if let Some(hello) = tls_client_hello(&frame.data, frame.linktype) {
                            detector.on_tls(frame.ts_millis, &flow, hello);
                            found_hello = true;
                        }
                    }
                }
                Ok(None) => break,
                Err(e) => panic!("读取 pcap 失败: {e}"),
            }
        }
        assert!(found_hello, "夹具中应至少有一个 ClientHello");
        assert!(detector.drain().is_empty(), "良性流量不应命中 C2 规则");
    }
}
