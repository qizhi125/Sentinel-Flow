//! 最小化的经典 pcap 读取器与 L2-L4 偏移解析，供第一条切片使用。
//! 实时 AF_XDP 抓包在后续切片中替换本模块。

use std::fs::File;
use std::io::{self, BufReader, Read, Write};
use std::net::Ipv4Addr;
use std::path::Path;

// 单帧数据：时间戳（毫秒）、链路类型与原始字节。
pub struct Frame {
    pub ts_millis: i64,
    pub linktype: u32,
    pub data: Vec<u8>,
}

// IPv4/TCP 五元组中的 IP 与端口部分。
pub struct Flow {
    pub src_ip: String,
    pub dst_ip: String,
    pub src_port: u16,
    pub dst_port: u16,
}

pub struct PcapReader {
    reader: BufReader<File>,
    little_endian: bool,
    nano_ts: bool,
    linktype: u32,
    snaplen: u32,
}

impl PcapReader {
    // 打开经典 pcap 文件并解析 24 字节全局头。
    pub fn open(path: &Path) -> io::Result<Self> {
        let file = File::open(path)?;
        let mut reader = BufReader::new(file);
        let mut header = [0u8; 24];
        reader.read_exact(&mut header)?;
        let magic = u32_be(&header[0..4]);
        let (little_endian, nano_ts) = match magic {
            0xa1b2c3d4 => (false, false),
            0xa1b23c4d => (false, true),
            0xd4c3b2a1 => (true, false),
            0x4d3cb2a1 => (true, true),
            _ => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "不支持的 pcap magic（本切片不支持 pcapng）",
                ))
            }
        };
        let snaplen = read_u32(&header[16..20], little_endian);
        let linktype = read_u32(&header[20..24], little_endian);
        Ok(Self {
            reader,
            little_endian,
            nano_ts,
            linktype,
            snaplen,
        })
    }

    pub fn linktype(&self) -> u32 {
        self.linktype
    }

    // 读取下一帧；文件结束返回 Ok(None)。
    pub fn next_frame(&mut self) -> io::Result<Option<Frame>> {
        let mut rec = [0u8; 16];
        match self.reader.read_exact(&mut rec) {
            Ok(()) => {}
            Err(e) if e.kind() == io::ErrorKind::UnexpectedEof => return Ok(None),
            Err(e) => return Err(e),
        }
        let ts_sec = read_u32(&rec[0..4], self.little_endian) as i64;
        let ts_frac = read_u32(&rec[4..8], self.little_endian) as i64;
        let incl_len = read_u32(&rec[8..12], self.little_endian) as usize;
        // 注意：对损坏文件做上限保护，避免异常大的内存分配。
        if incl_len == 0 || incl_len > 16 * 1024 * 1024 {
            return Ok(None);
        }
        if self.snaplen != 0 && incl_len > self.snaplen as usize {
            return Ok(None);
        }
        let mut data = vec![0u8; incl_len];
        self.reader.read_exact(&mut data)?;
        let frac_norm = if self.nano_ts {
            ts_frac / 1_000_000
        } else {
            ts_frac / 1_000
        };
        Ok(Some(Frame {
            ts_millis: ts_sec * 1_000 + frac_norm,
            linktype: self.linktype,
            data,
        }))
    }
}

// 提取帧的 IPv4/TCP 五元组；非 TCP 流量返回 None。
pub fn flow5(data: &[u8], linktype: u32) -> Option<Flow> {
    tcp_payload(data, linktype).map(|(flow, _)| flow)
}

// 启发式提取 TLS ClientHello：TCP 载荷以记录头 0x16 0x03 开头。
// 返回的切片覆盖整条 TLS 记录。
pub fn tls_client_hello(data: &[u8], linktype: u32) -> Option<&[u8]> {
    let (_, payload) = tcp_payload(data, linktype)?;
    (payload.len() >= 5 && payload[0] == 0x16 && payload[1] == 0x03).then_some(payload)
}

// 不同链路类型到 IP 头的字节偏移。
fn l2_offset(linktype: u32) -> Option<usize> {
    match linktype {
        1 => Some(14),   // 以太网
        113 => Some(16), // Linux cooked capture v1
        101 => Some(0),  // 裸 IP
        0 => Some(4),    // BSD 回环 / NULL
        _ => None,
    }
}

// 解析 IPv4/TCP 头并返回五元组与 TCP 载荷切片。
fn tcp_payload(data: &[u8], linktype: u32) -> Option<(Flow, &[u8])> {
    let offset = l2_offset(linktype)?;
    let ip = data.get(offset..)?;
    if ip.len() < 20 || ip[0] >> 4 != 4 {
        return None;
    }
    let ihl = (ip[0] & 0x0f) as usize * 4;
    if ihl < 20 || ip.len() < ihl + 20 || ip[9] != 6 {
        return None;
    }
    let total_len = u16_be(&ip[2..4]) as usize;
    if total_len < ihl + 20 {
        return None;
    }
    let tcp_start = offset + ihl;
    let end = (offset + total_len).min(data.len());
    let tcp = data.get(tcp_start..end)?;
    if tcp.len() < 20 {
        return None;
    }
    let data_offset = (tcp[12] >> 4) as usize * 4;
    if data_offset < 20 || tcp.len() < data_offset {
        return None;
    }
    let flow = Flow {
        src_ip: Ipv4Addr::from(u32::from_be_bytes([ip[12], ip[13], ip[14], ip[15]])).to_string(),
        dst_ip: Ipv4Addr::from(u32::from_be_bytes([ip[16], ip[17], ip[18], ip[19]])).to_string(),
        src_port: u16_be(&tcp[0..2]),
        dst_port: u16_be(&tcp[2..4]),
    };
    Some((flow, &tcp[data_offset..]))
}

fn u16_be(b: &[u8]) -> u16 {
    u16::from_be_bytes([b[0], b[1]])
}

fn u32_be(b: &[u8]) -> u32 {
    u32::from_be_bytes([b[0], b[1], b[2], b[3]])
}

fn read_u32(b: &[u8], little_endian: bool) -> u32 {
    if little_endian {
        u32::from_le_bytes([b[0], b[1], b[2], b[3]])
    } else {
        u32_be(b)
    }
}

pub mod synth {
    // 垂直切片冒烟测试用的合成演示流量。

    use super::*;

    // 写一个包含单帧 Ethernet/IPv4/TCP 的经典 pcap，
    // 载荷为最小 TLS ClientHello（密码套件 0x1301，无扩展）。
    pub fn write_demo_pcap(path: &Path) -> io::Result<()> {
        let frame = demo_frame();
        let mut buf = Vec::with_capacity(24 + 16 + frame.len());
        buf.extend_from_slice(&0xa1b2c3d4u32.to_be_bytes());
        buf.extend_from_slice(&2u16.to_be_bytes());
        buf.extend_from_slice(&4u16.to_be_bytes());
        buf.extend_from_slice(&0u32.to_be_bytes());
        buf.extend_from_slice(&0u32.to_be_bytes());
        buf.extend_from_slice(&65535u32.to_be_bytes());
        buf.extend_from_slice(&1u32.to_be_bytes());
        buf.extend_from_slice(&1u32.to_be_bytes());
        buf.extend_from_slice(&0u32.to_be_bytes());
        buf.extend_from_slice(&(frame.len() as u32).to_be_bytes());
        buf.extend_from_slice(&(frame.len() as u32).to_be_bytes());
        buf.extend_from_slice(&frame);
        File::create(path)?.write_all(&buf)
    }

    fn demo_frame() -> Vec<u8> {
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

        let ip_len = 20 + 20 + record.len();
        let mut frame = Vec::new();
        frame.extend_from_slice(&[0u8; 12]);
        frame.extend_from_slice(&0x0800u16.to_be_bytes());
        frame.push(0x45);
        frame.push(0);
        frame.extend_from_slice(&(ip_len as u16).to_be_bytes());
        frame.extend_from_slice(&0u16.to_be_bytes());
        frame.extend_from_slice(&0u16.to_be_bytes());
        frame.push(64);
        frame.push(6);
        frame.extend_from_slice(&0u16.to_be_bytes());
        frame.extend_from_slice(&[192, 168, 1, 10]);
        frame.extend_from_slice(&[8, 8, 8, 8]);
        frame.extend_from_slice(&50000u16.to_be_bytes());
        frame.extend_from_slice(&443u16.to_be_bytes());
        frame.extend_from_slice(&0u32.to_be_bytes());
        frame.extend_from_slice(&0u32.to_be_bytes());
        frame.push(0x50);
        frame.push(0x18);
        frame.extend_from_slice(&65535u16.to_be_bytes());
        frame.extend_from_slice(&0u16.to_be_bytes());
        frame.extend_from_slice(&0u16.to_be_bytes());
        frame.extend_from_slice(&record);
        frame
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_synthetic_pcap_and_extracts_hello() {
        let path = std::env::temp_dir().join(format!("sentinel-demo-{}.pcap", std::process::id()));
        synth::write_demo_pcap(&path).unwrap();

        let mut reader = PcapReader::open(&path).unwrap();
        assert_eq!(reader.linktype(), 1);
        let frame = reader.next_frame().unwrap().expect("one frame");
        assert_eq!(frame.ts_millis, 1_000);

        let flow = flow5(&frame.data, frame.linktype).expect("five-tuple");
        assert_eq!(flow.src_ip, "192.168.1.10");
        assert_eq!(flow.dst_ip, "8.8.8.8");
        assert_eq!(flow.src_port, 50000);
        assert_eq!(flow.dst_port, 443);

        let hello = tls_client_hello(&frame.data, frame.linktype).expect("hello");
        assert_eq!(&hello[..3], &[0x16, 0x03, 0x03]);
        let _ = std::fs::remove_file(&path);
    }
}
