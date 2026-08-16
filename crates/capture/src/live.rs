//! 实时 AF_PACKET 抓包（仅 Linux），零依赖，直接封装必要的系统调用。

use crate::Frame;
use std::io;
use std::mem;
use std::os::raw::{c_int, c_uint, c_ulong, c_void};
use std::ptr;
use std::time::{SystemTime, UNIX_EPOCH};

// Linux 操作系统接口常量（非运行配置）。
const AF_PACKET: c_int = 17;
const SOCK_RAW: c_int = 3;
const ETH_P_ALL_NET: c_int = 0x0300; // htons(ETH_P_ALL)
const SIOCGIFINDEX: c_ulong = 0x8933;

#[repr(C)]
struct SockAddrLl {
    sll_family: u16,
    sll_protocol: u16,
    sll_ifindex: c_int,
    sll_hatype: u16,
    sll_pkttype: u8,
    sll_halen: u8,
    sll_addr: [u8; 8],
}

#[repr(C)]
struct Ifreq {
    ifr_name: [u8; 16],
    ifr_ifindex: c_int,
}

unsafe extern "C" {
    fn socket(domain: c_int, kind: c_int, protocol: c_int) -> c_int;
    fn close(fd: c_int) -> c_int;
    fn bind(fd: c_int, addr: *const SockAddrLl, len: c_uint) -> c_int;
    fn ioctl(fd: c_int, request: c_ulong, ...) -> c_int;
    fn recvfrom(
        fd: c_int,
        buf: *mut c_void,
        len: usize,
        flags: c_int,
        src: *mut SockAddrLl,
        addrlen: *mut c_uint,
    ) -> isize;
}

// 实时抓包器：绑定指定网卡的 AF_PACKET 原始套接字。
pub struct LiveReader {
    fd: c_int,
    buf: Vec<u8>,
}

impl LiveReader {
    // 打开网卡；缺少 root/CAP_NET_RAW 权限时返回可读错误。
    pub fn open(interface: &str) -> io::Result<Self> {
        let fd = unsafe { socket(AF_PACKET, SOCK_RAW, ETH_P_ALL_NET) };
        if fd < 0 {
            return Err(with_capture_hint(io::Error::last_os_error()));
        }
        let reader = Self {
            fd,
            buf: vec![0u8; 65536],
        };
        let ifindex = match reader.interface_index(interface) {
            Ok(index) => index,
            Err(e) => {
                unsafe { close(fd) };
                return Err(e);
            }
        };
        let addr = SockAddrLl {
            sll_family: AF_PACKET as u16,
            sll_protocol: 0x0003u16.to_be(),
            sll_ifindex: ifindex,
            sll_hatype: 0,
            sll_pkttype: 0,
            sll_halen: 0,
            sll_addr: [0; 8],
        };
        let ret = unsafe { bind(fd, &addr, mem::size_of::<SockAddrLl>() as c_uint) };
        if ret < 0 {
            let err = with_capture_hint(io::Error::last_os_error());
            unsafe { close(fd) };
            return Err(err);
        }
        Ok(reader)
    }

    // 读取下一帧；时间戳为接收时刻，链路类型固定为以太网。
    pub fn next_frame(&mut self) -> io::Result<Option<Frame>> {
        loop {
            let n = unsafe {
                recvfrom(
                    self.fd,
                    self.buf.as_mut_ptr() as *mut c_void,
                    self.buf.len(),
                    0,
                    ptr::null_mut(),
                    ptr::null_mut(),
                )
            };
            if n < 0 {
                let err = io::Error::last_os_error();
                if err.kind() == io::ErrorKind::Interrupted {
                    continue;
                }
                return Err(err);
            }
            if n == 0 {
                continue;
            }
            let ts_millis = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|d| d.as_millis() as i64)
                .unwrap_or(0);
            return Ok(Some(Frame {
                ts_millis,
                linktype: 1,
                data: self.buf[..n as usize].to_vec(),
            }));
        }
    }

    // 通过 SIOCGIFINDEX 查询网卡索引。
    fn interface_index(&self, interface: &str) -> io::Result<c_int> {
        if interface.is_empty() || interface.len() >= 16 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "网卡名称长度无效",
            ));
        }
        let mut req = Ifreq {
            ifr_name: [0; 16],
            ifr_ifindex: 0,
        };
        req.ifr_name[..interface.len()].copy_from_slice(interface.as_bytes());
        let ret = unsafe { ioctl(self.fd, SIOCGIFINDEX, &mut req) };
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }
        if req.ifr_ifindex == 0 {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                format!("网卡 {interface} 不存在"),
            ));
        }
        Ok(req.ifr_ifindex)
    }
}

impl Drop for LiveReader {
    fn drop(&mut self) {
        unsafe { close(self.fd) };
    }
}

// 权限错误补充可操作的提示。
fn with_capture_hint(err: io::Error) -> io::Error {
    if err.kind() == io::ErrorKind::PermissionDenied {
        return io::Error::new(
            err.kind(),
            format!(
                "{err}：实时抓包需要 root 或 CAP_NET_RAW（sudo setcap cap_net_raw+ep 二进制文件）"
            ),
        );
    }
    err
}
