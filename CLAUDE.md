# Rich on Paper — Claude Code Instructions

## Project Overview

Ultra-low-latency paper-trading engine. Pure C++23, Alpaca Markets integration. Target: sub-50μs hot path.

This is a **greenfield project**, not yet live. Refactors and breaking changes are welcome when they improve the design.
No backwards compatibility constraints.

## Build & Test

```bash
# Configure
cmake -B build -S . \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build build

# Test
cd build && ctest --output-on-failure
```

## Critical: Low-Latency by Default

Every code change must respect the hot path / cold path boundary. Before writing any C++ code, check
`.claude/rules/low-latency.md`.

The hot path is **network-to-network**: WebSocket market data ingestion through to REST order submission.

Key non-negotiables:

- **No virtual dispatch on the hot path** — templates/CRTP
- **No heap allocation on the hot path** — pre-allocate at startup
- **No `std::shared_ptr` on the hot path** — atomic refcount overhead. Use raw non-owning pointers.
- **No mutex on the hot path** — atomics and lock-free structures
- **No exceptions on the hot path** — use error codes, mark functions `noexcept`
- **Cache-line align** all hot-path structs: `alignas(64)`, `static_assert(sizeof(T) == expected)`
- **Measure before/after** every optimization

When unsure if something is hot path: if it runs between WebSocket recv and REST order send, it's hot path.

## C++ Conventions

- C++23 standard (`-std=c++23`)
- LLVM clang-format style (see `.clang-format`)
- `#pragma once` for header guards
- One class per file, `PascalCase.hpp` / `PascalCase.cpp`
- `static_assert` all struct sizes and alignment
- `[[nodiscard]]` on functions returning values
- Parenthesize `(std::min)` / `(std::max)` for MSVC compat
- Platform guards: `#if defined(_WIN32)` / `__linux__` / `__APPLE__`

## Project Management

Uses **Taskwarrior** with `project:rop` for all tasks.

- `/task-next` — pick up next task, create branch
- `/task-done` — complete current task, show what unblocked
- `/task-status` — project progress report
- T-shirt estimates: XS, S, M, L, XL
- Priorities: H (critical path), M (important), L (nice-to-have)

### Git Discipline

- Branch per task: `<github-issue-number>/<short-slug>`
- Commit messages: `[#<github-issue-number>] <imperative description>`
- Find the issue number: `gh issue list --search "<keywords>"`
- One logical change per commit
- Explain **why** not **what**
- **Never force push.** Always add fixup commits on top. Multiple commits per PR is fine.
- **Before every commit/push**, run `pre-commit run --all-files` and stage any auto-fixes. Do not push code that fails
  pre-commit checks.

### Pull Requests

- Always reference the matching GitHub issue with `Closes #N` or `Fixes #N` in the PR body. Find the issue number via
  `gh issue list --search "<keywords>"` before creating the PR.

## Web Research

When looking up library APIs, usage patterns, or implementation details, use **Context7 MCP** (`resolve-library-id` then
`query-docs`) to fetch up-to-date documentation. This applies to agents, skills, and interactive sessions alike.

When implementing low-latency patterns, performance optimizations, or financial protocol integrations, **always search
the web** for current best practices. Key topics to research as needed:

- Lock-free queue designs (Vyukov MPSC, Disruptor pattern)
- Linux kernel bypass (io_uring, DPDK, AF_XDP)
- Alpaca Markets API (REST v2, WebSocket streaming, msgpack framing)
- Cache-oblivious algorithms and data structures
- Modern C++ template metaprogramming patterns
- FIX protocol and market microstructure

## CI Pipeline

GitHub Actions matrix: Ubuntu/macOS/Windows x GCC/Clang/MSVC x Debug/Release. Coverage via Codecov (80% target). CodeQL
security scanning. Pre-commit hooks enforced. ASAN+UBSAN on every push, TSAN nightly. libFuzzer fuzz testing on every
push (Clang-only, 60+60+30s bounded runs).
