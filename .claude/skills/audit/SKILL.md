______________________________________________________________________

## name: audit description: Audit the codebase against engineering standards and testing rules

Audit the rich-on-paper codebase against the project's engineering standards. Use the Agent tool to run **four parallel
agents** for maximum speed, then synthesize results.

## Important: avoid false positives

Before reporting any finding, check:

1. **Is it already tracked in the Taskwarrior backlog?** Run `task project:rop status:pending` and check. If tracked,
   note the task ID but do not report it as a new finding.
1. **Was it already investigated and resolved?** Check `git log --oneline --all --grep="<keyword>"` for relevant
   commits. Example: TCP_NODELAY was investigated in PR #121 and confirmed to be enabled by default in both libcurl
   (since 7.50.2) and ixwebsocket — do not flag it.
1. **Is it a deliberate design decision?** Check annotations and comments. Example: LockFreeMPSCQueue allocates per-push
   — this is documented as a known trade-off.

## Phase 1 — Launch four audit agents in parallel

### Agent 1: C++ Standards & Low-Latency Audit

Read the rules:

- `.claude/rules/cpp-standards.md`
- `.claude/rules/low-latency.md`

Then read every `.hpp` and `.cpp` file under `include/` and `src/`. For each file, check:

The hot path is **network-to-network** — from WebSocket market data ingestion through to REST order submission:

1. AlpacaWebSocketSource (ingestion)
1. AlpacaMsgpackDecoder (decode)
1. ZmqMarketEventSink (transport publish)
1. MarketEventConsumer (transport receive + strategy + risk)
1. SimpleMarketMakingStrategy (signal generation)
1. RiskManager (validation)
1. LockFreeMPSCQueue / SPSCQueue (order queue)
1. OrderGateway (order execution via REST)

Steps 1-7 are the **inner hot path**. Step 8 is the **outer hot path** (network I/O unavoidable, but everything around
it must be optimized).

**Inner hot path rules** (steps 1-7 — zero overhead):

- No heap allocation (`new`, `push_back` that may reallocate, `std::string` construction)
- No virtual dispatch — templates/CRTP/concepts only
- No `std::shared_ptr` — atomic refcount is 10-20ns per access. Use raw non-owning pointers with documented lifetime.
- No mutex — atomics and lock-free structures only
- No exceptions on normal path — `std::expected` or error codes
- All functions should be `noexcept` where they do not use exceptions internally. Flag any hot-path function missing
  `noexcept`.
- No `std::string`, `std::map`, `std::unordered_map` — use fixed-width buffers and flat containers
- No syscalls (no logging, no I/O, no `std::cout`/`std::cerr`)
- All structs: `alignas(64)` + `static_assert` on size
- `static_assert(std::is_trivially_copyable_v<T>)` for types passed through lock-free queues
- `[[likely]]`/`[[unlikely]]` on cold error branches

**Outer hot path rules** (step 8 — OrderGateway):

- Connection reuse (`cpr::Session`, not per-request TCP+TLS)
- `TCP_NODELAY`: already enabled by default in libcurl (7.50.2+) and ixwebsocket. Confirmed in PR #121 — do NOT flag as
  missing.
- `TCP_QUICKACK` on Linux (set via `CURLOPT_SOCKOPTFUNCTION`)
- Pre-built JSON payloads where possible
- Rate limit tracking (`X-Ratelimit-Remaining`)
- Minimize allocations in order serialization

**Cold path** (FillListener fill processing, logging, startup/shutdown, reconciliation, PendingOrderTracker):

- Normal C++ OK, but still check:
  - No `catch (...) {}` that silently swallows — must log with context (component, operation, symbol)
  - `[[nodiscard]]` on functions returning values
  - `const` correctness
  - `enum class` not plain enums
  - No `#define` constants — use `constexpr`
  - No raw `new`/`delete` in application code (lock-free queue internals are the sole documented exception)

**Universal rules** (all files):

- Fixed-width integers (`uint64_t`, `int64_t` not `int`, `long`)
- Platform guards: `#if defined(_WIN32)` / `__linux__` / `__APPLE__`
- `(std::min)`/`(std::max)` parenthesized for MSVC compat
- `#pragma once` for all headers
- One class per file, `PascalCase.hpp` / `PascalCase.cpp`
- Naming: classes `PascalCase`, functions `camelCase`, members `snake_case_`, constants `kPascalCase` or `constexpr`

Report each violation as: `file:line — [RULE] description`

### Agent 2: Architecture & Design Audit

Read the rules:

- `.claude/rules/architecture.md`

Then read all `.hpp` and `.cpp` files. Check:

**Ownership & lifetime:**

- `TradingEngine` owns all components via `std::unique_ptr`
- Hot-path components (inner + outer) use raw non-owning pointers with lifetime guaranteed by owning parent
- `std::shared_ptr` is banned on the hot path (atomic refcount overhead). Flag any `shared_ptr` in hot-path code.
- Cold-path `shared_ptr` acceptable only when genuinely shared across independent owners

**Thread model:**

- Core 0 (outer hot path + cold): OrderGateway, FillListener
- Core 1 (inner hot path — ingestion): MarketDataPipeline
- Cores 2+ (inner hot path — strategy): MarketEventConsumer
- No hot-path thread may block another

**Dependency direction:**

- No circular dependencies
- Strategy knows nothing about execution
- MarketEventConsumer depends on Strategy (template), RiskManager, order callback
- RiskManager depends on PositionManager (raw pointer)

**Network (outer hot path):**

- Connection reuse (no per-request TCP+TLS)
- REST rate limit tracking (`X-Ratelimit-Remaining`)
- WebSocket reconnect: exponential backoff with jitter
- REST reconciliation after WebSocket reconnect

**Error handling:**

- No silent `catch (...) {}` — forbidden
- Cold path: `std::runtime_error` with descriptive messages
- Hot path: no exceptions at all, `noexcept` where possible

**Order lifecycle:**

- Closed loop: place -> track -> fill/cancel confirmation
- PendingOrderTracker clears slots on fill/cancel
- OrderGateway cancel-before-replace for same (symbol, side)

Report each violation as: `file:line — [ARCH] description`

### Agent 3: Test, Fuzzing & Sanitizer Audit

Read the rules:

- `.claude/rules/testing.md`

Then read every `test_*.cpp` file under `tests/` and every `fuzz_*.cpp` file under `fuzz/`. For each test file, check:

**Deduplication:**

- Two or more `TEST_F` calling the same method with the same or trivially different inputs — flag as duplicate
- Copy-pasted setup code — should use fixture `SetUp()` or factory helper
- Tests differing only by input values — should use `TEST_P`
- Tests differing only by type — should use `TYPED_TEST`
- Identical mock configurations testing different assertions — should be merged

**Coverage gaps:**

- All public methods of each component tested
- Error paths tested (exceptions, null, invalid input)
- Boundary conditions (max values, overflow/underflow)
- Concurrency scenarios where applicable
- Concurrency tests use `std::promise`/`std::future` with timeouts for hang detection
- `ThreadGuard` RAII wrappers used for all test threads
- `noexcept` functions tested to confirm they don't throw

**Fuzz testing:**

- Fuzz harnesses exist under `fuzz/` for all deserialization entry points:
  - `fuzz_msgpack_decoder` — binary msgpack (hot path)
  - `fuzz_trade_update_parser` — JSON trade updates (cold)
  - `fuzz_risk_manager` — Order struct validation (hot path)
- Each harness links with `-fsanitize=fuzzer,address,undefined`
- Seed corpus exists under `fuzz/corpus/<target>/`
- New deserialization code must have a corresponding fuzz target

**Sanitizer builds:**

- ASAN + UBSAN: runs on every push (CI `sanitizer-asan` job)
- TSAN: runs nightly (separate incompatible build)
- Fuzz testing: runs on every push with Clang (CI `fuzz-testing` job, 60+60+30s per target)
- Sanitizer CMake options present (SANITIZE_ADDRESS, SANITIZE_THREAD, SANITIZE_UNDEFINED, ENABLE_FUZZING)
- `static_assert` on struct sizes, alignment, trivially_copyable for all types in lock-free queues or wire formats

Report each finding as:

- `file:line — [DUP] description` for duplicates
- `file:line — [GAP] description` for coverage gaps
- `file — [FUZZ] description` for missing fuzz targets
- `file — [SANITIZER] description` for sanitizer issues

### Agent 4: Documentation & Developer Experience Audit

Read the following files (if they exist):

- `README.md`
- `CONTRIBUTING.md`
- `CHANGELOG.md`
- `LICENSE`
- Any `scripts/` or `setup/` or `bootstrap` files
- `CMakeLists.txt` (top-level, for build instructions context)
- `vcpkg.json` (for dependency list)

Then **web search** for best practices on:

- Portfolio/showcase C++ project READMEs (targeting hiring managers, recruiters, and technical reviewers)
- One-click developer environment setup scripts for cross-platform C++ projects
- Open-source project documentation standards (badges, architecture diagrams, quick-start)

Check the following:

**README as public-facing portfolio document:**

- Project title with concise tagline communicating what it does and why it matters
- Impact/performance metrics (target latency, throughput, architecture highlights) — recruiters need to see numbers
- Architecture overview — either inline diagram (Mermaid/ASCII) or link to diagram file
- Tech stack summary (C++23, ZeroMQ, TBB, Alpaca, etc.)
- Quick-start: clone, setup, build, run in under 5 commands
- Link to setup script for zero-effort environment bootstrap
- Badges: CI status, coverage, language standard, license
- Screenshots or terminal output demos if applicable
- Clear separation: "what this is" (top) vs "how to develop" (further down)
- No stale or placeholder content
- Professional tone — no "TODO" or "WIP" markers visible in the main README

**One-click setup scripts:**

- Script exists for bootstrapping a fresh development environment (install vcpkg, dependencies, configure cmake)
- Cross-platform coverage: Linux, macOS, Windows (or WSL)
- Script is idempotent (safe to re-run)
- Script checks for prerequisites and gives clear error messages if missing (e.g., CMake version, compiler version)
- Script installs all vcpkg dependencies automatically
- Script configures and builds the project
- Script runs tests to verify the setup works

**Other documentation:**

- `CONTRIBUTING.md` exists with build instructions, code style, PR process
- License file present
- Inline code comments where architecture is non-obvious (not excessive — only where logic is surprising)
- API/component documentation for key interfaces (`IRestClient`, `MarketEventConsumer`, `TradingEngine`)

Report each finding as:

- `file — [DOC] description` for documentation gaps
- `[SETUP] description` for missing setup automation
- `[README] description` for README improvements

## Phase 2 — Synthesize

After all four agents complete, combine results into a single report grouped by severity:

### Report format

**CRITICAL** — violations on the hot path (inner or outer) that directly impact latency: heap alloc, virtual dispatch,
shared_ptr, mutex, missing noexcept, missing alignment, connection-per-request

**WARNING** — standards violations that should be fixed but don't block: naming, missing nodiscard, missing const,
silent catch

**TEST** — test deduplication issues, coverage gaps, missing fuzz targets

**DEVEX** — documentation gaps, missing setup automation, README improvements

**INFO** — suggestions and minor improvements

Cross-reference every finding against the Taskwarrior backlog (`task project:rop status:pending`). For each finding,
note whether an existing task covers it, or flag as untracked.

Skip findings that are:

- Already tracked in the backlog (just note the task ID)
- Confirmed resolved in git history (check with `git log`)
- Documented design trade-offs (e.g., MPSC queue per-push allocation, TCP_NODELAY handled by libraries)

End with a summary: total violations by category, and the top 3 highest-impact items to fix first.
