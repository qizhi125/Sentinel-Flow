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
