# 项目演进路线图 (Roadmap)

## Phase 1: 骨架与重构 (当前状态)
- [x] 实施 v6.0 目录结构 (`capture/`, `engine/`, `presentation/`)。
- [x] 修复 CMake 构建系统。
- [ ] **[进行中]** 改造 `RawPacket` 以携带内核时间戳。
- [ ] 定义 `ICaptureDriver` 接口。

## Phase 2: 并行化改造 (多核升级)
- [ ] 实现 `RSSDispatcher` (软 Hash 分流器)。
- [ ] 根据 CPU 核心数自动创建 `WorkerThread` 池。
- [ ] 将单队列替换为 `std::vector<ThreadSafeQueue>` 队列组。
- [ ] 实现基础的数据聚合，防止 UI 冻结。

## Phase 3: 工业级性能 (极致优化)
- [ ] 引入 **对象池 (Object Pool)**，彻底消除 `new`/`delete`。
- [ ] 将互斥锁队列替换为 **无锁环形缓冲区** (`moodycamel::ConcurrentQueue`)。
- [ ] 实施 CPU 亲和性绑定 (Core Affinity)。
- [ ] 实施 "智能分诊" (多级优先级队列)。

## Phase 4: 智能化与扩展
- [ ] 支持 Lua 脚本编写自定义规则。
- [ ] 实现 Linux eBPF 驱动 (零拷贝)。
- [ ] 集成 AI 异常检测模块。