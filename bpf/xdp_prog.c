// SPDX-License-Identifier: GPL-2.0
// Sentinel-Flow XDP 内核程序 — AF_XDP 零拷贝快速路径
//
// 功能：在 XDP 挂载点解析以太网/IPv4/TCP/UDP 头，将有效 IPv4 报文
//       通过 AF_XDP 套接字映射表重定向至用户态管线，绕过内核协议栈。
//
// 编译：clang -O2 -g -target bpf -c xdp_prog.c -o xdp_prog.o
// 骨架生成：bpftool gen skeleton xdp_prog.o > xdp_prog.skel.h

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>

// AF_XDP 套接字映射表 — 内核通过此表将报文重定向至用户态 AF_XDP 套接字。
// key: 队列索引（对应 CPU 核心），value: AF_XDP 套接字文件描述符。
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, int);
    __type(value, int);
} xsks_map SEC(".maps");

// 默认重定向队列索引 — 调用方通过 bpftool map update 覆盖为实际的 rx_queue_index。
// 使用 volatile 防止编译器将循环边界常量折叠。
static volatile int rx_queue_index = 0;

// 许可证声明 — 加载器要求必须提供。
char _license[] SEC("license") = "GPL";

// ---- XDP 入口 ----
//
// 在每个接收数据包上调用，运行于 NAPI 软中断上下文。
// 解析 L2→L3→L4 头部链，对有效 IPv4 报文执行 bpf_redirect_map
// 重定向至 AF_XDP 套接字（绕过内核协议栈）。
//
// 返回值：
//   XDP_REDIRECT  — 报文已重定向至用户态 AF_XDP 套接字（本程序通过 bpf_redirect_map 实现）
//   XDP_PASS      — 非 IPv4 或非 TCP/UDP 报文，交由内核协议栈正常处理
//   XDP_DROP      — 格式错误的报文，静默丢弃
SEC("xdp")
int xdp_pass_func(struct xdp_md *ctx) {
    // 获取数据边界（开始/结束指针）
    void *data     = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    // ---- L2: 以太网头 ----
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_DROP; // 报文截断

    // 仅处理 IPv4 — 非 IPv4 报文交由内核正常处理
    if (eth->h_proto != __constant_htons(ETH_P_IP))
        return XDP_PASS;

    // ---- L3: IPv4 头 ----
    struct iphdr *iph = (void *)(eth + 1);
    if ((void *)(iph + 1) > data_end)
        return XDP_DROP;

    // 基本健全性检查：忽略 IP 分片（第一片 fragments 外的分片不含 L4 头）
    // 注：frag_off 低 13 位为片段偏移量，第 14 位为 MF 标志。
    //     (& 0x3FFF) 提取片段偏移量，(iph->frag_off & 0x1FFF) 同时捕获偏移量和 MF。
    if (iph->frag_off & __constant_htons(0x3FFF))
        return XDP_PASS; // IP 分片 — 交由内核重组（AF_XDP 期望完整报文）

    // ---- L4: TCP/UDP 头 ----
    // 验证存在传输层头，但不对 L4 协议进行过滤 —
    // 所有 IPv4 报文（TCP/UDP/ICMP/SCTP）均重定向至用户态进行深度检测。
    if (iph->protocol == IPPROTO_TCP) {
        struct tcphdr *tcph = (void *)iph + sizeof(*iph);
        if ((void *)(tcph + 1) > data_end)
            return XDP_DROP;
    } else if (iph->protocol == IPPROTO_UDP) {
        struct udphdr *udph = (void *)iph + sizeof(*iph);
        if ((void *)(udph + 1) > data_end)
            return XDP_DROP;
    } else {
        // 非 TCP/UDP 但仍是 IPv4（例如 ICMP、SCTP、GRE）— 仍重定向
        // 以确保在最小解析开销下实现完整的可见性。
        if ((void *)iph + sizeof(*iph) > data_end)
            return XDP_DROP;
    }

    // ---- 重定向至用户态 AF_XDP 套接字 ----
    // bpf_redirect_map 将报文绕过内核协议栈，直接送入指定队列的
    // AF_XDP UMEM 接收环。用户态 Pipeline 线程从 Completion Ring 消费。
    //
    // 如果映射条目为空（用户态尚未创建套接字），bpf_redirect_map 返回
    // XDP_ABORTED 或静默丢弃（取决于内核版本），因此我们显式降级为 XDP_PASS。
    if (bpf_redirect_map(&xsks_map, rx_queue_index, XDP_DROP) == XDP_REDIRECT)
        return XDP_REDIRECT;

    // 重定向失败（例如 AF_XDP 套接字未绑定至该队列）。
    // 交由内核正常处理以避免静默丢包。
    return XDP_PASS;
}
