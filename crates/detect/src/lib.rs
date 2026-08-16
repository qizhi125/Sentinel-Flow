//! 检测插件。本切片只实现 JA3 检测器。

use sentinel_capture::Flow;
use std::collections::HashSet;

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

pub trait Detector {
    fn name(&self) -> &'static str;
    fn on_tls(&mut self, ts_millis: i64, flow: &Flow, hello: &[u8]);
    fn drain(&mut self) -> Vec<Alert>;
}

struct Ja3Rule {
    fingerprint: &'static str,
    name: &'static str,
    severity: u8,
}

// 注意：前四条为公开文档化的 JA3 样例；六条 "[占位]" 为 md5(名称) 的临时值，
// 发布前必须替换为权威 JA3 情报源。演示条目用于切片冒烟测试。
const BUILTIN_RULES: &[Ja3Rule] = &[
    Ja3Rule {
        fingerprint: "6734f37431670b3ab4292b8f60f29984",
        name: "TrickBot",
        severity: 8,
    },
    Ja3Rule {
        fingerprint: "4d7a28d6f2263ed61de88ca66eb011e3",
        name: "Emotet",
        severity: 8,
    },
    Ja3Rule {
        fingerprint: "51c64c77e60f3980eea90869b68c58a8",
        name: "Dridex/QakBot（共享指纹）",
        severity: 8,
    },
    Ja3Rule {
        fingerprint: "a0e9f5d64349fb13191bc781f81f42e1",
        name: "Meterpreter（共享指纹）",
        severity: 8,
    },
    Ja3Rule {
        fingerprint: "a436e38305c86d947f1b448d305dbb0e",
        name: "Sliver [占位]",
        severity: 7,
    },
    Ja3Rule {
        fingerprint: "1f3b7b6b752a03f510a6b0c56d523c81",
        name: "BumbleBee [占位]",
        severity: 7,
    },
    Ja3Rule {
        fingerprint: "2eac9ef0f759365d90fcd8005a16bfbc",
        name: "Gozi [占位]",
        severity: 7,
    },
    Ja3Rule {
        fingerprint: "91370ecc5f968515a5b465858366f09a",
        name: "AgentTesla [占位]",
        severity: 7,
    },
    Ja3Rule {
        fingerprint: "7280f59b457357f7a22d678dbfde64e0",
        name: "RedLine [占位]",
        severity: 7,
    },
    Ja3Rule {
        fingerprint: "673f1e68ceeb02700a142b069e30085e",
        name: "AsyncRAT [占位]",
        severity: 7,
    },
    Ja3Rule {
        fingerprint: "ea1e247991e541e39bf918cb7cfa5139",
        name: "SyntheticDemo-Beacon",
        severity: 6,
    },
];

#[derive(Default)]
pub struct Ja3Detector {
    pending: Vec<Alert>,
    seen: HashSet<(String, String, u16)>,
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
        for rule in BUILTIN_RULES {
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

    #[test]
    fn matches_demo_fingerprint() {
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

        let flow = Flow {
            src_ip: "192.168.1.10".into(),
            dst_ip: "8.8.8.8".into(),
            src_port: 50000,
            dst_port: 443,
        };
        let mut detector = Ja3Detector::default();
        detector.on_tls(1_000, &flow, &record);
        let alerts = detector.drain();
        assert_eq!(alerts.len(), 1);
        assert_eq!(alerts[0].rule, "SyntheticDemo-Beacon");
    }
}
