//! 把告警 POST 到 Go 控制面的最小 HTTP/1.1 客户端。

use sentinel_detect::Alert;
use std::io::{self, Read, Write};
use std::net::TcpStream;

pub struct Client {
    base: String,
}

impl Client {
    pub fn new(base: impl Into<String>) -> Self {
        Self { base: base.into() }
    }

    // 发送一条告警到 /v1/ingest；非 2xx 响应返回错误。
    pub fn post_alert(&self, alert: &Alert) -> io::Result<()> {
        let body = alert_json(alert);
        let (host, port) = split_host_port(&self.base)?;
        let mut stream = TcpStream::connect((host.as_str(), port))?;
        let request = format!(
            "POST /v1/ingest HTTP/1.1\r\nHost: {host}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
            body.len(),
            body
        );
        stream.write_all(request.as_bytes())?;
        let mut response = String::new();
        stream.read_to_string(&mut response)?;
        let status = response.lines().next().unwrap_or_default();
        if !status.starts_with("HTTP/1.1 200") && !status.starts_with("HTTP/1.1 204") {
            return Err(io::Error::other(format!("ingest 响应异常: {status}")));
        }
        Ok(())
    }
}

// 从 http://host:port 形式的地址中拆出主机与端口。
fn split_host_port(base: &str) -> io::Result<(String, u16)> {
    let rest = base
        .strip_prefix("http://")
        .or_else(|| base.strip_prefix("https://"))
        .unwrap_or(base);
    let host_port = rest.split('/').next().unwrap_or(rest);
    match host_port.rsplit_once(':') {
        Some((host, port)) => {
            let port = port
                .parse::<u16>()
                .map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "端口格式错误"))?;
            Ok((host.to_string(), port))
        }
        None => Ok((host_port.to_string(), 80)),
    }
}

fn json_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 8);
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}

// 手工构造告警 JSON，避免引入序列化依赖。
fn alert_json(a: &Alert) -> String {
    format!(
        concat!(
            r#"{{"timestamp":{},"src_ip":"{}","dst_ip":"{}","src_port":{},"dst_port":{},"rule":"{}","severity":{},"fingerprint":"{}"}}"#
        ),
        a.timestamp,
        json_escape(&a.src_ip),
        json_escape(&a.dst_ip),
        a.src_port,
        a.dst_port,
        json_escape(&a.rule),
        a.severity,
        json_escape(&a.fingerprint)
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Read;
    use std::net::TcpListener;
    use std::thread;

    // 按 Content-Length 读完整条请求，避免与客户端互相等待。
    fn read_request(stream: &mut std::net::TcpStream) -> String {
        let mut buf = Vec::new();
        let mut chunk = [0u8; 1024];
        loop {
            let n = stream.read(&mut chunk).expect("读取请求");
            if n == 0 {
                break;
            }
            buf.extend_from_slice(&chunk[..n]);
            if let Some(head) = buf.windows(4).position(|w| w == b"\r\n\r\n") {
                let head_end = head + 4;
                let content_length = String::from_utf8_lossy(&buf[..head_end])
                    .lines()
                    .find_map(|line| {
                        line.to_ascii_lowercase()
                            .strip_prefix("content-length:")
                            .and_then(|v| v.trim().parse::<usize>().ok())
                    })
                    .unwrap_or(0);
                if buf.len() >= head_end + content_length {
                    break;
                }
            }
        }
        String::from_utf8_lossy(&buf).into_owned()
    }

    // 本地回环服务器校验客户端请求并返回 204。
    #[test]
    fn posts_alert_to_local_server() {
        let listener = TcpListener::bind("127.0.0.1:0").expect("监听端口");
        let addr = listener.local_addr().expect("获取地址");
        let server = thread::spawn(move || {
            let (mut stream, _) = listener.accept().expect("接受连接");
            let request = read_request(&mut stream);
            assert!(request.starts_with("POST /v1/ingest HTTP/1.1"));
            assert!(request.contains("\"rule\":\"test-rule\""));
            stream
                .write_all(b"HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n")
                .expect("写响应");
        });
        let alert = Alert {
            timestamp: 1,
            src_ip: "10.0.0.1".into(),
            dst_ip: "10.0.0.2".into(),
            src_port: 40000,
            dst_port: 443,
            rule: "test-rule".into(),
            severity: 6,
            fingerprint: "a".repeat(32),
        };
        Client::new(format!("http://{addr}"))
            .post_alert(&alert)
            .expect("发送告警");
        server.join().expect("服务线程");
    }
}
