# AGENT.md — Codex Operating Rules for Sentinel-Flow

Binding ground rules for Codex in this repository. Principles come from the
Karpathy-inspired agent guidelines, Linus Torvalds, Bjarne Stroustrup, and Rob
Pike; the project-specific guardrails reflect the locked architecture.

## 1. Project in One Paragraph

Sentinel-Flow is a Linux-only NIDS for personal and edge nodes. The Rust data
plane captures traffic (offline pcap today, XDP+AF_XDP via aya-rs later) and
runs detectors; a C++20 library supplies algorithms (JA3 today, Aho-Corasick /
PCRE2 / Hyperscan later) through a C ABI; the Go control plane ingests alerts
over HTTP and streams them to an embedded web UI over SSE. The current slice:
offline pcap → TLS ClientHello → JA3 → alert → SSE timeline.

## 2. Engineering Principles

### 2.1 Karpathy — Think Before Coding

- State assumptions explicitly. When uncertain, ask — do not silently pick an interpretation.
- Surface confusion and tradeoffs; push back when a simpler approach exists.
- Stop when confused: name what is unclear and request clarification before writing code.

### 2.2 Karpathy — Simplicity First

- Write the minimum code that solves the problem. Nothing speculative.
- No unrequested features, single-use abstractions, fake "flexibility", or error handling for impossible scenarios.
- If a construct would make a senior engineer call it overcomplicated, simplify it.

### 2.3 Karpathy — Surgical Changes

- Touch only what the task requires. No drive-by refactoring, formatting, or comment edits.
- Match the existing style, even where you would do it differently.
- If you find unrelated dead code, mention it — do not delete it.
- Remove only orphans your own change created; leave pre-existing code alone.

### 2.4 Karpathy — Goal-Driven Execution

- Transform tasks into verifiable goals: reproduce the bug with a failing test first, then make it pass.
- For multi-step work, state a plan with a verify check per step, and loop until every check passes.
- Weak criteria ("make it work") are not acceptable; define observable success criteria up front.

### 2.5 Linus Torvalds — Clarity and Restraint

- "Talk is cheap. Show me the code." Prefer concrete, minimal changes over elaborate discussion.
- Keep code simple and readable: shallow nesting, small functions, obvious control flow.
- Avoid cleverness; good taste means the simplest correct solution.
- Prefer fixing the problem over assigning blame; do not rewrite code that works.

### 2.6 Bjarne Stroustrup — C++ Discipline

- Make simple things simple; express intent directly in code.
- Manage resources with RAII. Never leak; never pass raw owning pointers across interfaces.
- Prefer compile-time checks, strong types, and precise interfaces over loose pointer+length pairs.
- Assume concurrency: avoid data races, minimize shared mutable state, prefer immutable snapshots.
- Do not optimize prematurely — make it correct and simple first, measure, then tune.
- Stay within ISO C++20; platform tricks belong only at the C ABI boundary.

### 2.7 Rob Pike — Go Discipline

- Clear is better than clever; less is exponentially more.
- Don't communicate by sharing memory; share memory by communicating.
- Errors are values: check and handle them gracefully; do not ignore or log-and-continue blindly.
- The bigger the interface, the weaker the abstraction — keep interfaces small.
- Make the zero value useful; keep names short, clear, and package-consistent.
- Gofmt is the law; do not hand-format.
- Cgo is not Go: minimize foreign-code surface, copy at the boundary, never retain foreign pointers.

## 3. Repository-Specific Guardrails

### Language boundaries (mandatory)

- C++ exposes only `extern "C"` functions. It owns no sockets, threads, or
  memory: callers allocate output buffers, and C++ bounds-checks before writing.
- Rust owns sockets, rings, threads, eBPF loading, and process lifecycle. Every
  C++ call is wrapped in a safe Rust function in `crates/ffi`.
- Go only speaks HTTP. It never calls C++ or libpcap directly.
- Rust ↔ Go carries alerts/events/stats over HTTP+JSON only — never per-packet
  data. The packet hot path stays inside Rust.

### Build

- `make build` runs `cargo build` then `go build`. `crates/ffi/build.rs`
  compiles the C++ static library with `-Wall -Wextra -Werror`, so any C++
  warning or error fails the entire build.
- `make test` runs the Rust and Go test suites.

### Versioning

- Base is 0.1.1. Patch per iteration, minor for a major feature, major only
  after the first 1.0 release.

### Comment policy (mandatory)

- One line before each non-trivial function stating its purpose and return meaning.
- Name the algorithm and its input/output before complex algorithms (Aho-Corasick, JA3).
- `// 注意：` before any unsafe block or boundary condition that is easy to misread.
- No line-by-line comments and no explanations of obvious statements.

### Detection honesty

- Fingerprint and rule entries must mark authoritative sources versus
  placeholders. Never fabricate threat data or claim a shared fingerprint
  belongs to a single family.

### Kernel and deployment

- Linux only, kernel >= 5.15 with BTF/CO-RE. eBPF/AF_XDP is an optional fast
  path, not a 1.0 requirement; pcap is the baseline.
- The future eBPF container deployment requires
  `--net=host --cap-add NET_RAW,NET_ADMIN,BPF`.

### Archive

- `.archive/` holds the pre-pivot prototype. It is git-ignored, read-only
  reference material, and must never be built or extended.

## 4. Definition of Done

- Success criteria were defined before coding and are verified by the build and tests.
- The diff contains only requested changes; every changed line traces to the task.
- `make build`, `cargo test`, and `go test ./go/...` pass; `cargo fmt --check`
  and `gofmt -l` are clean.
- Assumptions are recorded in the commit/PR; genuine ambiguity was asked, not guessed.
- No unrelated refactoring, dead-code deletion, or comment churn.

## 5. Sources

- [multica-ai/andrej-karpathy-skills](https://github.com/multica-ai/andrej-karpathy-skills)
- Linux kernel coding style (Linus Torvalds)
- C++ Core Guidelines (Bjarne Stroustrup, Herb Sutter, et al.)
- Go Proverbs (Rob Pike)
