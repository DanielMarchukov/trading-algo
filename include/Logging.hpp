#pragma once

namespace logging {

/// Call once from main(), before any threads are spawned.
void init();

/// Register all named loggers with null sinks (for test binaries).
void initForTests();

/// Flush all loggers and drop the spdlog registry.
void shutdown();

} // namespace logging
