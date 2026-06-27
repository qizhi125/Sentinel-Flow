# 变更日志草稿

- Phase 8.5 v4.3: 修复 UTF-8/ANSI 截断导致 �� 乱码，压缩 Row 2 三面板至统一 2 行高度
  - renderTable: 对所有告警字段 (Src/Dst/Message) 统一执行 sanitize -> safeTrunc 净化管线
  - sanitize 正则加固为 [^[:print:]]，剔除全部控制字符与 ANSI 原始序列
  - cardInfo: 移除标题行，双列布局，状态文本改为"引擎运行中"
  - cardCore: 移除标题行，双列 | 分隔布局
  - cardTraffic: 移除标题行，保留 sparkline + 水平协议行
  - 三个 hub 面板统一 clamp 至 Height(hubH=4) = 2 内容行 + 双边框

- Phase 8.6: 修复 AhoCorasick 未构建匹配逻辑错误，消灭 stdout 污染，实现可滚动视口
  - AhoCorasick::match() 双重重载均添加 if(!built_) 守卫子句，ctest NotBuilt 测试通过
  - C++ 侧 3 处 std::cout → std::cerr: PcapCapture.cpp (2 处), DatabaseManager.cpp (1 处)
  - Go 侧告警回调移除 fmt.Printf "[ALERT]" 热路径污染
  - Go 侧关机总结 6 处 fmt.Print* → fmt.Fprint*(os.Stderr, ...)
  - dashboard.go: 引入 bubbles/viewport 可滚动组件，固定顶部/页脚，仅表格区域滚动
  - renderTable 重构：移除 maxRows 参数与 Height/MaxHeight 约束，全量渲染交由视口裁剪
  - Update() 处理 WindowSizeMsg 初始化视口尺寸，KeyMsg 与 MouseMsg 路由至 viewport.Update()
  - go.mod 添加 github.com/charmbracelet/bubbles v0.20.0 依赖（用户需执行 go mod tidy 下载）

- Phase 8.7: 文档全面对齐实际代码实现，统一格式与术语
  - README.md: 插入快速开始章节（完整构建管线），修复 CLI 标志 (-r→-c)，简化架构图，更新目录树
  - docs/architecture.md: 精简 Mermaid 管线图移除未实现组件，修正 C API 定义、CLI 参数、线程模型、构建目标
  - docs/cgo_boundary.md: 完全重写以匹配 capi.h (6 函数+1 回调类型)，移除不存在结构体，更新 LDFLAGS
  - docs/lockfree_model.md: 修正组件文件路径，移除未实现的 EngineState/IP黑名单/CPU亲和性章节，注解设计预留
  - docs/setup.md: 移除 eBPF/XDP 可选依赖，修复构建目标名，添加 go mod tidy 步骤，纠正运行标志
  - docs/operations.md: 修复全部 CLI 标志引用，移除未实现功能的调优参数，精简部署场景，增加关机流程说明
  - CODE_OF_CONDUCT.md: 无需更改（标准贡献者公约）
  - CONTRIBUTING.md: 修复开发环境依赖清单、构建命令、测试路径，分支策略对齐实际 git 模型

- Phase 8.7 (补充): TUI 遥测日志 + Beautiful README 重构
  - cmd/sentinel/main.go: 引入 tea.LogToFile("sentinel-tui.log") 在 tea.NewProgram 前初始化 TUI 遥测日志
  - pkg/ui/dashboard.go: 添加 "log" 导入，修复 viewport 导入别名错误（tea 别名错误指向 viewport -> 修正回 bubbletea）
  - dashboard.go WindowSizeMsg: log.Printf 记录终端尺寸与视口计算值，辅助排查布局撕裂
  - dashboard.go alertMsg: log.Printf 记录告警规则 ID/协议/等级/载荷长度，辅助追踪 UTF-8 截断异常
  - README.md: 完整迁移至 docs/system_overview.md 保留为系统概览文档
  - 新建 README.md: Beautiful README 范式（shields.io 徽章 + 居中标题 + 快速开始管线 + 终端约束章节 + 文档导航表）
  - docs/assets/: 创建占位资源目录（banner/demo 图片占位）

- Phase 8.8: 视口层级重构 + 鼠标捕获修复 + README 工程化净化
  - dashboard.go: tea.NewProgram 增加 tea.WithMouseCellMotion() 启用鼠标滚轮事件
  - dashboard.go WindowSizeMsg: vpHeight 仅扣除 bannerH + footerH，移除 hubH/cardsH/graphH 扣除项
  - dashboard.go View(): 重构为仅横幅+页脚固定，中枢/KPI/流量图/告警表格统一加入 scrollContent 视口滚动区
  - dashboard.go: 移除未使用的 overheadH 常量，log.Printf 格式修正（%q 替换嵌入引号，参数对齐）
  - cmd/sentinel/main.go: 添加 tea.LogToFile("sentinel-tui.log") 遥测日志初始化
  - README.md: 移除全部 emoji 与主观营销用语，重写为客观工程文档语调，保留结构元素

- Phase 8.9: 纯 MVU 视口架构 + 动态响应间距 + 智能滚动
  - View() 净化: 移除 m.viewport.SetContent() 副作用，外层改用 fmt.Sprintf 显式换行拼接
  - syncViewportContent(): 新增辅助方法，在 Update() 中统一管理视口内容构建与 SetContent 调用
  - renderHeader(termH): 新增响应式顶部渲染，termH<30 紧凑模式，>=30 增加呼吸空间
  - renderFooter(termH): 新增响应式页脚渲染，termH<30 紧凑模式，>=30 页脚上方增加间距
  - renderHub/Cards/Graph: 函数签名均接受 termHeight 参数（预留后续卡片内部压缩）
  - WindowSizeMsg: 动态渲染 header/footer → lipgloss.Height() 测量 → vpHeight=终端高-headerH-footerH-2
  - WindowSizeMsg: viewport.YPosition = headerH+1，first-init 与 resize 分支均调用 syncViewportContent()
  - alertMsg: 智能滚动 — AtBottom() 检测 → syncViewportContent → 仅 isAtBottom 时 GotoBottom
  - tickMsg: 每次数据刷新追加 syncViewportContent() 调用，保持视口内容与统计面板同步
  - 移除废弃常量: bannerH, footerH (已被动态 lipgloss.Height 替代), overheadH
  - 移除重复 renderFooter(w int) (已被 renderFooter(termH int) 替代)

- Phase 9.0: 实现 UI 渲染节流与缓存机制.
  - 帧率限制: tick 间隔 500ms→100ms（~10 FPS），alertMsg 不再立即刷新视口（仅置 dirty 标记）
  - header/footer 缓存: 新增 renderCache 结构体，View() 仅在终端尺寸变更时重建 header/footer 的 Lipgloss 渲染
  - 批量更新: viewport.SetContent() 仅在 tickMsg 且 dirty 时调用，告警积攒到 100ms 窗口统一刷新
  - 智能滚动: 从 alert 时检查变为 tick 时检查 AtBottom()，保持自动跟踪行为
  - pps 乘数修正: ×2→×10 适配 100ms tick 间隔
  - 统一 pointer receiver: Init/Update/View 全部切为 *Model

- Phase 10.0: 从 Bubble Tea/Lipgloss 移植到 termui Widget/Grid 架构.
  - 依赖交换: 移除 bubbletea/bubbles/lipgloss，添加 gizak/termui/v3
  - 架构转换: MVU (Init/Update/View) → termui 增量渲染 (Init/Render/事件轮询)
  - Dashboard 直接持有 termui 控件指针 (Paragraph/Table/SparklineGroup)，通过属性更新而非字符串拼接
  - 告警表格: viewport.Model + 手动渲染 → widgets.Table（原生固定表头 + 滚动）
  - 布局: 手工 lipgloss 拼接 → ui.NewGrid + NewRow/NewCol 响应式比例网格
  - 刷新循环: tea.Tick 定时器 → time.Ticker + ui.PollEvents select 循环
  - 日志重定向: tea.LogToFile → os.OpenFile + log.SetOutput
  - 公共 API 保持兼容: NewDashboard/Start/Quit/SendAlert/UpdateRuleCount/TotalAlerts 签名不变

- Phase 10.1: 解耦重构 — 拆分 dashboard.go 为 components/ 包 + controller.go.
  - 新建 pkg/ui/components/: types.go(共享类型+Snapshot), utils.go(工具函数), banner.go, cards.go(中枢+KPI+Sparkline), table.go, footer.go
  - 每个组件仅暴露构造函数(New*)和纯刷新函数(Refresh*)，不持有内部状态
  - dashboard.go 精简至 189 行：仅含 Dashboard 结构体、buildLayout(Grid 组装)、公共 API(Start/Quit/SendAlert/UpdateRuleCount/TotalAlerts)
  - 新增 controller.go(113行)：runLoop(事件轮询)、updateStats(统计派生)、captureSnapshot(快照采集)、refreshAll(分发至组件)
  - 控制器持锁复制告警切片，委托 components.RefreshAlertTable 执行纯格式化；消除跨层 computeColumnWidths 重复
  - 通过 type alias(AlertRecord = components.AlertRecord) 保持 main.go 调用兼容

- Phase 10.3: 差异渲染架构 — 事件驱动 + 脏标记 + 值比较，仅对变更组件调用 ui.Render
  - controller.go: 移除 100ms 全量刷新 Ticker，替换为 1s statsTicker + renderCh 事件驱动循环
  - controller.go: renderMask 位掩码为每个控件独立标记脏状态（maskInfo/Core/Traffic/Tput/Queue/Buffer/Peak/Sparkline/Table）
  - controller.go: computeDiff 逐字段比较新旧 Snapshot 值，仅标记实际变更组件（PPS变→maskTput+maskCore，QueueDepth变→maskQueue）
  - controller.go: renderFrame 对掩码命中控件独立调用 ui.Render(widget)，ANSI 转义限定于该控件边界框
  - controller.go: handleResize 重建 Grid 布局 → renderFrame(maskAll) + ui.Render(grid) 全量重绘
  - controller.go: initStatsBaseline 避免首次 PPS 尖峰；updateStats PPS 公式调整为 1s 间隔（=cur-prevAlerted）
  - components/cards.go: RefreshHubCards→拆分为 RefreshInfoCard/RefreshCoreCard/RefreshTrafficCard 独立刷新
  - components/cards.go: RefreshKPICards→拆分为 RefreshThroughputCard/RefreshQueueCard/RefreshBufferCard/RefreshPeakCard 独立刷新
  - dashboard.go: 移除 dirty bool，新增 prevSnap Snapshot（值比较基线）与 renderCh chan renderMask（缓冲1 合并高频告警）
  - dashboard.go: SendAlert→渲染通道非阻塞推送 maskTable|maskCore|maskTraffic，UpdateRuleCount→maskInfo
  - 静默控件（bannerPara/footerPara）仅初始化一次，永不再渲染，彻底消除全屏闪烁

- Phase 10.4: 最小化 termui 诊断探针 — 旁路 Dashboard/Grid 全栈隔离问题层
  - cmd/sentinel/main.go: 主函数替换为 runDiagnosticProbe()，注释全部生产逻辑
  - 探针 1: Paragraph + SetRect + Border → 验证控件创建/定位/边框渲染管道
  - 探针 2: os.Getenv("TERM") → 实时显示终端类型，排除 TERM= 或 dumb 终端问题
  - 探针 3: PollEvents + KeyboardEvent → 验证事件轮询模型，确认终端接管正常
  - 探针直接在 main() 中调用（非 goroutine），排除 goroutine 调度导致的 termui 初始化失败

- Phase 11.0: 主线程 UI 所有权重构 — 修复白屏根因（goroutine 竞争终端原始模式）
  - cmd/sentinel/main.go: 移除诊断探针，恢复生产代码，重构 goroutine 所有权模型
  - 阶段 1（主线程）: 创建 Dashboard + Engine + 加载规则 + 构建自动机（UI 尚未接管终端）
  - 阶段 2（后台 goroutine）: eng.Start(CGO抓包)/watcher.Start(文件热重载)/signal.Notify→dash.Quit
  - 阶段 3（主线程阻塞）: dash.Start() → ui.Init() + buildLayout() + runLoop() 全链路在主 goroutine 执行
  - 阶段 4（UI 退出后）: eng.Stop() + 关机总结输出
  - 信号处理从主线程阻塞(<-sigCh)移至后台 goroutine，收到 SIGINT/SIGTERM 时调用 dash.Quit() 通知 runLoop 安全退出
  - 移除不再需要的 termui 直接导入(tui/twidgets)，Dashboard 封装层已完全隔离底层 UI 库

- Phase 12.0: 健康检查与生命周期加固 — 心跳探针 + 渲染 panic 恢复 + defer 清理链 + 线程模型文档
  - controller.go: 新增 5s heartbeatTicker，每次触发记录 [main-loop] 心跳（PPS/alerts/threatLvl）
  - controller.go: 新增 safeRender() 用 recover 包裹 ui.Render，捕获 termui 内部 panic 并记录遥测日志
  - controller.go: runLoop 所有分支（退出/Resize/渲染请求/quit）均追加 log.Printf 诊断点
  - controller.go: renderFrame 首尾追加 [render] 日志，diffAndRender 记录 force/diff/combined 掩码
  - dashboard.go: 新增 done 通道（Start 退出时 close），Done() 方法供外部观测 UI 生命周期
  - dashboard.go: Quit 注释标注"幂等，可从任意 goroutine 调用"
  - main.go: eng.Stop() 移至 defer（engStarted 标志控制），确保 panic 路径不泄漏引擎资源
  - main.go: dash.Start() 包裹于匿名函数 + recover，termui panic 不跳过 defer 清理链
  - main.go: 信号 goroutine 追加 log.Printf 记录收到的信号类型
  - main.go: 关机总结移除无效的"运行时间"行，仅保留累积告警计数
  - docs/architecture.md: 线程模型表重写为 6 行（主 goroutine/捕获/Pipeline/DB/文件监控/信号处理）
  - docs/architecture.md: 主线程约束说明（termui 要求主 goroutine 所有权），线程间通信 ASCII 图

- Phase 11.1: 全量回归审计 — 修复 Grid.SetRect 坐标传播缺陷（白屏根因）
  - 根因: termui Grid.Draw() 是子控件 SetRect 的唯一调用点。Phase 10.3 差异渲染仅调用 ui.Render(widget)，
    从未调用 ui.Render(grid)，导致所有控件保持默认 rect=(0,0,0,0) 零尺寸，渲染不可见
  - controller.go: 新增 renderFrameTextOnly() — 仅刷新控件文本，不调用 ui.Render
  - controller.go: runLoop 初始渲染改为 renderFrameTextOnly + safeRender("grid-init", d.grid)
    Grid.Draw() 为所有子控件分配物理坐标并执行首次全量绘制
  - controller.go: handleResize 改为 renderFrameTextOnly + safeRender("grid-resize", d.grid)
    先刷新内容再重建 Grid 布局，避免在旧坐标上绘制后瞬时闪烁
  - 差异渲染路径 diffAndRender→renderFrame 保持不变：控件已由初始 Grid 渲染分配 SetRect，
    后续 ui.Render(widget) 在该持久化矩形内执行差异更新
  - 审计确认: 无 ui.Clear 调用、无构造函数返回 nil、无 Z 序覆盖问题
    Phase 11.2: 修复双缺陷 — Grid 零矩形坐标 + 告警表格列缺失（白屏最终根因）
  - 缺陷 1 (dashboard.go buildLayout): 根 Grid 从未调用 SetRect，NewBlock() 默认 innerRect=(0,0,0,0)
    Grid.Draw() 基于零矩形计算 width=0+1=1 height=0+1=1，所有子控件获得 ~1×1px 坐标，完全不可见
    修复: d.grid.SetRect(0, 0, termW, termH) — 为整棵控件树建立物理坐标入口
  - 缺陷 2 (dashboard.go buildLayout): d.alertTable 列从 d.grid.Set() 参数列表中缺失
    6 列总额应为 1.0 (0.22+0.12+0.14+0.10+0.37+0.05)，实际仅 5 列 0.63
    修复: 补充 ui.NewCol(0.37, d.alertTable) 条目
  - handleResize 路径同步加固: 重建 Grid 后调用 d.grid.SetRect(0, 0, termW, termH)
    （此前已包含 alertTable 列，未受缺陷 2 影响）
  - 线程所有权验证通过: dash.Start() 在 main goroutine 阻塞调用，信号/引擎/监控均为后台 goroutine

- Phase 11.3: 全量审计与缺陷修复 — 3 子代理并行审查 + 6 项修复
  - [严重] main.go:144: 移除 engStarted=false，修复 eng.Stop() 永不执行的 use-after-free 缺陷
    eng.Close()→sentinel_engine_destroy 在未停止抓包线程的情况下释放 C 资源
  - [低] controller.go computeDiff: 移除被后续无条件 mask|=maskInfo 覆盖的冗余 RuleCount 检查
  - [低] controller.go computeDiff: 新增 TermW→maskTable 检测（resize 不再依赖告警到达才更新表格列宽）
  - [低] 移除 Snapshot.TermH 死字段（types.go/captureSnapshot/handleResize 三处），无 Refresh 函数读取
  - [低] 移除 handleResize 多余的 _ = termH（termH 在后续被使用，非未用变量）
  - [重构] dashboard.go: 提取 rebuildLayout() 消除 buildLayout 与 handleResize 间 33 行重复 Grid 代码
    handleResize 从 42 行缩减至 10 行（仅调用 rebuildLayout + 全量渲染）
  - 生成三层 CLAUDE.md 上下文文件：全局架构约束、C++ 无锁数据面规范、Go TUI 线程安全规范
    修复主线程所有权 bug：runtime.LockOSThread 移至 main() 顶部，eng.Start 移至后台 goroutine，提取 StatsLoop 至独立 goroutine

- Phase 0.2 宏观迁移审计：对比新旧仓库全量文件，生成 docs/migration_and_architecture_report.md（9 章：迁移状态/组件映射/技术债/构建差异/测试覆盖/后续建议）
- Phase 0.3 行业对齐与技术债修复：更新 pkg/CLAUDE.md（tview→termui/v3，事件驱动差异渲染模型），更新 libsentinel/CLAUDE.md（STL 限制分快速路径/慢速路径），生成 docs/industry_analysis_and_roadmap.md（NIDS 行业趋势+Sentinel-Flow 定位+4 阶段 WBS 框架）
- Phase 0.4 架构重对齐与高并发基准：全面重写 libsentinel/CLAUDE.md 为纯英文高并发架构规范（Share-Nothing per-CPU 独立管线、RCU 全局状态管理、spdlog 异步遥测、libbpf 内核级 RSS），全面重写 pkg/CLAUDE.md 为纯英文灵活指南（去除 renderCh/statsTick 硬编码变量名，强调通用 TUI 线程安全原则与非阻塞 CGO 隔离），更新 WBS 集成 spdlog/libbpf/RSS 子任务
- Phase 0.5 目录与文档标准化：创建 data/logs/docs/adrs/scripts/bpf 标准化目录结构（含 .gitkeep），全面更新 .gitignore（构建产物/运行时数据/遥测日志），为 8 份 docs*.md 注入标准化元数据头部（文档状态/最后更新/所属子系统），删除冗余文件（Untitled.md/docs/assets/），初始化 ADR 系统（0001: ADR 采纳决策; 0002: Share-Nothing Per-CPU 并发模型）*
- *Phase 0.5 路径修复：清理根目录误置文件 — 删除 2 个编译产物（sentinel/sentinel-cli），删除残留 src/main + src/ 目录，迁移 sentinel_alerts.db→data/，迁移 sentinel-tui.log→logs/，evidences/ 保留原位（root 所有权限制）*
- *Phase 1.1 spdlog 异步遥测集成：CMake FetchContent 拉取 spdlog v1.15.2，创建 Logger.h/Logger.cpp（异步双接收器 + overrun_oldest 溢出策略 + SENTINEL_* 宏），清除全部 6 个 .cpp 文件中的 std::cerr/std::cout 原始 I/O，capi_impl 集成 init/shutdown 生命周期钩子
- Phase 1.2 遥测数据通道与仪表盘真实数据集成：C API 新增 sentinel_engine_stats_t 结构体 + sentinel_stats_callback_t 回调 + set_stats_callback 导出，C++ 组件暴露 thread-safe 指标 getter（PcapCapture/DB/Pipeline），EngineContext 内置 1Hz 统计线程，Go CGO binding 新增 goStatsCallback + statsRegistry + SetStatsCallback，Dashboard 新增 UpdateTelemetry 原子存储，controller 消除模拟数学公式（queueDepth=pps*3%2048→engineQueueDepth.Load()），main.go 连接遥测回调链路
- 扩展告警事件为真实五元组结构体，消除 TUI 模拟端点，Payload 快照零分配栈传递
- 实现管线消费者线程异常恢复：atomic 错误标志 + try/catch 保护 + stats 遥测回传 + Go 侧触发安全关机
- 集成 Google Benchmark v1.8，新增 ObjectPool 与 SPSCQueue 基准测试（池耗尽断层量化、容量扫掠、多线程吞吐），解析 TD-009
- 基于 Benchmark 压测结果，将 SPSC 队列与对象池容量锁定为 8192，利用 L1/L2 缓存亲和性消除性能抖动，Phase 1 正式收尾。
- Phase 2.1: 实现 XDP 内核程序 xdp_prog.c（以太网/IPv4/TCP/UDP 解析 + AF_XDP 套接字映射重定向），CMake 集成 clang -target bpf 编译链，sentinel_core 依赖 bpf_prog 目标。
- Phase 2.2: 集成 libbpf/libxdp 依赖链（pkg-config + ${XDP_LDFLAGS}），bpftool gen skeleton 生成 xdp_prog.skel.h，新建 AfXdpCapture 骨架类（继承 ICapture + XskContext 封装 AF_XDP 五环组件），全部虚函数为桩实现。
- Phase 2.2 核心：实现 AfXdpCapture 完整 UMEM 映射与生命周期（posix_memalign 分配 32 MiB 页对齐 UMEM → xsk_umem__create → xsk_socket__create → xdp_prog_bpf__open_and_load → bpf_program__attach_xdp → bpf_map_update_elem 注册 XSK → Fill Ring 预填充 8192 帧 → 启动捕获线程），stop() 逆序清理全部 6 层资源（线程 join → bpf_link__destroy → xsk_socket__delete → xsk_umem__delete → free umem_buffer → xdp_prog_bpf__destroy），每步含 SENTINEL_INFO/ERROR 日志与完整错误回滚路径。
- 修复 SPSCQueue::push 按值传递导致 GCC 对 alignas(64) 结构体发出 ABI 警告的问题，拆分为 const T& / T&& 双重重载消除警告。
- Push 前全量工程标准化：重写 cgo_boundary.md（5 元组 sentinel_alert_event_t + sentinel_engine_stats_t + sentinel_stats_callback_t 完整规约 + 独立统计回调链路时序图 + libbpf/libxdp 链接库），重写 architecture.md（AF_XDP 双后端捕获层 + spdlog 可观测性子图 + SPSC/OptPool 8192 容量注释 + bpf_prog 构建目标 + AfXdpCapture 扩展状态），全部 Go 代码 gofmt 格式化，新增 .gitignore .pending_changes 规则，零死代码/零 std::cout 残留。
