# CLAUDE.md

## 1. Spatial Boundaries & Rule Inheritance
- **Current Project**: Sentinel-Flow
- **Global Constraints**: You MUST strictly comply with the global engineering blueprint located at `/home/qizhi/Projects/.global_rules.md`. Read it via `cat` before starting major tasks.
- **Language Enforcement**: All code comments, docstrings, `TODO` items, and internal system output MUST strictly use **Simplified Chinese**.

## 2. Architecture & Documentation Constraint (CRITICAL)
- **Single Source of Truth**: Do NOT store or assume project architecture, specific technical stacks, data flows, or system states within this `CLAUDE.md` file. All authoritative project information resides exclusively in `README.md` and the `docs/` directory. You must read those files to understand the current system architecture.
- **Architecture & Documentation Synchronization Rule**: Any confirmed architectural evolution, structural update, or design decision MUST be updated in the relevant documentation and planning files FIRST, before any code optimization or refactoring begins. Do not modify architecture without explicit user confirmation.
- **Pragmatic Documentation & Visual Engineering**: STRICTLY PROHIBITED to use marketing fluff, adjectives, or subjective terms (e.g., "hardcore", "ultimate", "intelligent", "lightweight", "seamless"). Documentation must rely on exact data structures, variable names, and Mermaid diagrams. Use Mermaid strictly for complex logic (e.g., C/Go boundary sequence diagrams, State Machines for AC Automaton, SPSC Queue memory layout) to replace verbose text descriptions.

## 3. Operational Boundaries

### Build & Execution Control Policy
- **Manual Execution Only**: You are strictly prohibited from executing any build commands (e.g., `cmake`, `make`, `go build`, `ninja`) or test/binary execution commands (e.g., `./build/...`, `ctest`) without the user's explicit, immediate permission for each specific command.
- **Role Definition**: Your role is that of a System Architect and Code Editor. You are responsible for generating precise, error-free commands and explaining their logic. You are NOT the CI/CD runner.
- **Environment Parity**: The user manages build consistency between IDE (CLion/GoLand) and CLI. You must defer all execution to the user to ensure the user's environment remains deterministic and synchronized.

### Engineering Philosophy
- **Concurrency & Performance Philosophy**: Assume high-performance, lock-free design intent by default for data-plane paths, but rely on actual documentation to dictate the specific implementation mechanism.

## 4. Version Control & Sync Workflow
- **Lazy Commit Policy**: Do NOT push to GitHub autonomously.
- **Temporary Log**: After every modification, you MUST append a concise summary of the change to `.pending_changes` in the project root.
- **Push Trigger**: Only push to GitHub when explicitly requested by the user.
- **Commit Message Style**: When requested to push, summarize all entries from `.pending_changes`, clear the file, and create a commit using this exact style:
    1. Start with core verb phrases.
    2. Focus on "what was done", keep it concise (max 3 lines).
    3. Include key technical nouns/project names only.
    4. Separate items with commas or newlines, no lists or bullet points.
    5. No fluff (e.g., "completed", "participated").
    6. Example:
       审计 Converter3D 代码一致性，修复文档脱节问题
       搭建无锁并发空间区块架构，引入 Go 控制面
       处理 ErosionAnalyzer 编译错误并替换概率体素网格

## 5. Available Automation Tools (Skills)
1. `audit-lock [dir]`: Check for unauthorized mutexes.
2. `env-verify`: Verify kernel/toolchain for eBPF.
3. `context-map [dir]`: Summarize directory topology.
4. `rg` / `sg`: Global search/AST-based refactoring.