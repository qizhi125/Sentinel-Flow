#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <bpf/bpf_helpers.h>

// 定义 XSK Map，用于将流量重定向到用户态 AF_XDP 套接字
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, int);
    __type(value, int);
} xsks_map SEC(".maps");

// 定义 IP 黑名单 Map，最大支持 10 万条 IP 封禁规则
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 100000);
    __type(key, __u32);
    __type(value, __u8);
} blacklist_map SEC(".maps");

SEC("xdp")
int xdp_pass_or_drop(struct xdp_md *ctx) {
    // 边界检查
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    // 强转为以太网头部并校验边界
    struct ethhdr *eth = (struct ethhdr *)data;
    if ((void *)(eth + 1) > data_end) {
        return XDP_PASS;
    }

    // 仅对 IPv4 报文执行黑名单检查 (0x0800)
    if (eth->h_proto == __builtin_bswap16(ETH_P_IP)) {

        // 解析 IP 头部并校验边界（放入局部作用域）
        struct iphdr *iph = (struct iphdr *)(eth + 1);
        if ((void *)(iph + 1) <= data_end) {

            // 提取源 IP 并查询黑名单 Map
            __u32 src_ip = iph->saddr;
            __u8 *blocked = (__u8 *)bpf_map_lookup_elem(&blacklist_map, &src_ip);
            if (blocked && *blocked == 1) {
                // 命中黑名单，网卡底层直接丢弃
                return XDP_DROP;
            }
        }
    }

    // 将合法流量重定向到对应网卡队列的 AF_XDP 接收环
    int index = ctx->rx_queue_index;
    if (bpf_map_lookup_elem(&xsks_map, &index)) {
        return bpf_redirect_map(&xsks_map, index, XDP_DROP);
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";