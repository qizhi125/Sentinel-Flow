#include "sentinel/capture/AfXdpCapture.h"
#include "sentinel/common/utils/Logger.h"

#include <net/if.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

#include "xdp_prog.skel.h"

namespace sentinel::capture {

// ---- 构造 / 析构 ----

AfXdpCapture::AfXdpCapture() = default;

AfXdpCapture::~AfXdpCapture() {
    stop();
}

// ---- ICapture 接口 ----

void AfXdpCapture::set_packet_callback(PacketCallback cb) {
    packet_cb_ = std::move(cb);
}

bool AfXdpCapture::start(const std::string& device) {
    if (running_.load(std::memory_order_acquire)) {
        SENTINEL_WARN("AfXdpCapture::start: 已在运行中（{}）", device_);
        return false;
    }

    device_ = device;
    ctx_ = std::make_unique<XskContext>();

    unsigned int ifindex = if_nametoindex(device.c_str());
    if (ifindex == 0) {
        SENTINEL_ERROR("AfXdpCapture: 无法解析网卡接口 '{}'", device);
        ctx_.reset();
        return false;
    }
    SENTINEL_INFO("AfXdpCapture: 网卡 '{}' ifindex={}", device, ifindex);

    // UMEM 需要页面对齐的连续物理内存，内核与用户态共享此区域实现零拷贝。
    size_t const umem_total = XskContext::kUmemFrameCount * XskContext::kUmemFrameSize;
    int const ret = posix_memalign(&ctx_->umem_buffer, getpagesize(), umem_total);
    if (ret != 0 || !ctx_->umem_buffer) {
        SENTINEL_ERROR("AfXdpCapture: UMEM 内存分配失败（posix_memalign, 请求 {} MiB）",
                       umem_total / (1024 * 1024));
        ctx_.reset();
        return false;
    }
    std::memset(ctx_->umem_buffer, 0, umem_total); // 清零以利于调试
    SENTINEL_INFO("AfXdpCapture: UMEM 已分配 {} MiB @ {}", umem_total / (1024 * 1024),
                  ctx_->umem_buffer);

    struct xsk_umem_config umem_cfg = {};
    umem_cfg.fill_size     = XskContext::kRingSize;
    umem_cfg.comp_size     = XskContext::kRingSize;
    umem_cfg.frame_size    = XskContext::kUmemFrameSize;
    umem_cfg.frame_headroom = 0; // XDP 程序未预留 headroom
    umem_cfg.flags         = 0;

    if (xsk_umem__create(&ctx_->umem, ctx_->umem_buffer, umem_total,
                         &ctx_->fill, &ctx_->comp, &umem_cfg) != 0) {
        SENTINEL_ERROR("AfXdpCapture: xsk_umem__create 失败");
        std::free(ctx_->umem_buffer);
        ctx_->umem_buffer = nullptr;
        ctx_.reset();
        return false;
    }
    SENTINEL_INFO("AfXdpCapture: UMEM 已创建（fill_sz={}, comp_sz={}, frame_sz={}）",
                  umem_cfg.fill_size, umem_cfg.comp_size, umem_cfg.frame_size);

    struct xsk_socket_config sock_cfg = {};
    sock_cfg.rx_size      = XskContext::kRingSize;
    sock_cfg.tx_size      = XskContext::kRingSize;
    sock_cfg.libbpf_flags = 0;
    sock_cfg.xdp_flags    = 0;
    sock_cfg.bind_flags   = 0;

    if (xsk_socket__create(&ctx_->xsk, device.c_str(), 0, ctx_->umem,
                           &ctx_->rx, &ctx_->tx, &sock_cfg) != 0) {
        SENTINEL_ERROR("AfXdpCapture: xsk_socket__create 失败（设备 '{}', 队列 0）", device);
        xsk_umem__delete(ctx_->umem);
        ctx_->umem = nullptr;
        std::free(ctx_->umem_buffer);
        ctx_->umem_buffer = nullptr;
        ctx_.reset();
        return false;
    }
    SENTINEL_INFO("AfXdpCapture: AF_XDP 套接字已创建（rx_sz={}, tx_sz={}）",
                  sock_cfg.rx_size, sock_cfg.tx_size);

    // CO-RE 重定位并加载 BPF 程序至内核
    ctx_->skel = xdp_prog_bpf__open_and_load();
    if (!ctx_->skel) {
        SENTINEL_ERROR("AfXdpCapture: xdp_prog_bpf__open_and_load 失败");
        xsk_socket__delete(ctx_->xsk);
        ctx_->xsk = nullptr;
        xsk_umem__delete(ctx_->umem);
        ctx_->umem = nullptr;
        std::free(ctx_->umem_buffer);
        ctx_->umem_buffer = nullptr;
        ctx_.reset();
        return false;
    }
    SENTINEL_INFO("AfXdpCapture: BPF 骨架已加载");

    ctx_->xdp_link = bpf_program__attach_xdp(ctx_->skel->progs.xdp_pass_func, ifindex);
    if (!ctx_->xdp_link) {
        SENTINEL_ERROR("AfXdpCapture: bpf_program__attach_xdp 失败（ifindex={}）", ifindex);
        xdp_prog_bpf__destroy(ctx_->skel);
        ctx_->skel = nullptr;
        xsk_socket__delete(ctx_->xsk);
        ctx_->xsk = nullptr;
        xsk_umem__delete(ctx_->umem);
        ctx_->umem = nullptr;
        std::free(ctx_->umem_buffer);
        ctx_->umem_buffer = nullptr;
        ctx_.reset();
        return false;
    }
    SENTINEL_INFO("AfXdpCapture: XDP 程序已附加至 ifindex={}", ifindex);

    // 将 AF_XDP 套接字 fd 填入内核 xsks_map，供 XDP 程序通过 bpf_redirect_map 重定向报文
    int const xsk_fd = xsk_socket__fd(ctx_->xsk);
    int const map_fd = bpf_map__fd(ctx_->skel->maps.xsks_map);
    int const queue_key = 0;
    if (bpf_map_update_elem(map_fd, &queue_key, &xsk_fd, BPF_ANY) != 0) {
        SENTINEL_ERROR("AfXdpCapture: bpf_map_update_elem(xsks_map, q=0) 失败");
        bpf_link__destroy(ctx_->xdp_link);
        ctx_->xdp_link = nullptr;
        xdp_prog_bpf__destroy(ctx_->skel);
        ctx_->skel = nullptr;
        xsk_socket__delete(ctx_->xsk);
        ctx_->xsk = nullptr;
        xsk_umem__delete(ctx_->umem);
        ctx_->umem = nullptr;
        std::free(ctx_->umem_buffer);
        ctx_->umem_buffer = nullptr;
        ctx_.reset();
        return false;
    }
    SENTINEL_INFO("AfXdpCapture: XSK fd={} 已注册至 xsks_map[{}]", xsk_fd, queue_key);

    // 预填充 Fill Ring：将全部 UMEM 帧地址写入，未预填充的帧永不被内核使用
    {
        size_t frame_idx = 0;
        while (frame_idx < XskContext::kUmemFrameCount) {
            uint32_t batch_idx = 0;
            size_t const remaining = XskContext::kUmemFrameCount - frame_idx;
            size_t const batch = std::min(remaining, XskContext::kBatchSize);
            size_t const reserved = xsk_ring_prod__reserve(&ctx_->fill, batch, &batch_idx);
            if (reserved == 0) {
                continue;
            }
            for (size_t i = 0; i < reserved; i++) {
                uint64_t* const addr = xsk_ring_prod__fill_addr(&ctx_->fill, batch_idx + i);
                *addr = (frame_idx + i) * XskContext::kUmemFrameSize;
            }
            xsk_ring_prod__submit(&ctx_->fill, reserved);
            frame_idx += reserved;
        }
    }
    SENTINEL_INFO("AfXdpCapture: Fill Ring 已预填充 {} 帧", XskContext::kUmemFrameCount);

    running_.store(true, std::memory_order_release);
    capture_thread_ = std::thread(&AfXdpCapture::capture_loop, this);

    SENTINEL_INFO("AfXdpCapture: 已启动（设备 '{}', ifindex={}）", device, ifindex);
    return true;
}

void AfXdpCapture::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return; // 已停止，幂等
    }

    SENTINEL_INFO("AfXdpCapture: 正在停止...");

    // 按逆序释放资源：捕获线程 → XDP link → 套接字 → UMEM → 内存 → BPF 骨架
    running_.store(false, std::memory_order_release);
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }

    if (!ctx_) {
        return;
    }

    if (ctx_->xdp_link) {
        bpf_link__destroy(ctx_->xdp_link);
        ctx_->xdp_link = nullptr;
        SENTINEL_INFO("AfXdpCapture: XDP 链接已销毁");
    }

    if (ctx_->xsk) {
        xsk_socket__delete(ctx_->xsk);
        ctx_->xsk = nullptr;
        SENTINEL_INFO("AfXdpCapture: AF_XDP 套接字已销毁");
    }

    if (ctx_->umem) {
        xsk_umem__delete(ctx_->umem);
        ctx_->umem = nullptr;
        SENTINEL_INFO("AfXdpCapture: UMEM 已销毁");
    }

    if (ctx_->umem_buffer) {
        std::free(ctx_->umem_buffer);
        ctx_->umem_buffer = nullptr;
        SENTINEL_INFO("AfXdpCapture: UMEM 缓冲区已释放");
    }

    if (ctx_->skel) {
        xdp_prog_bpf__destroy(ctx_->skel);
        ctx_->skel = nullptr;
        SENTINEL_INFO("AfXdpCapture: BPF 骨架已销毁");
    }

    ctx_.reset();
    SENTINEL_INFO("AfXdpCapture: 已完全停止");
}

bool AfXdpCapture::set_filter(const std::string& expression) {
    // AF_XDP 使用 XDP 内核程序进行过滤 — BPF 表达式过滤器不适用于此路径。
    // 表达式过滤应在用户态 Pipeline 中通过 Aho-Corasick 规则匹配实现。
    (void)expression;
    return false;
}

std::vector<std::string> AfXdpCapture::list_devices() {
    // TODO(Phase 2.2): 通过 libbpf if_nametoindex 或 netlink 枚举支持 XDP 的网卡
    return {};
}

void AfXdpCapture::capture_loop() {
    // TODO(Phase 2.2): AF_XDP 高速轮询主循环（零拷贝：Fill Ring ↔ RX Ring ↔ UMEM）
    SENTINEL_INFO("AfXdpCapture: 捕获循环已启动（桩模式）");
    while (running_.load(std::memory_order_acquire)) {
    }
    SENTINEL_INFO("AfXdpCapture: 捕获循环已退出");
}

} // namespace sentinel::capture
