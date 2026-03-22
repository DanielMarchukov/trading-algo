#pragma once

#include <memory>
#include <spdlog/sinks/sink.h>

namespace logging {

void init(const std::shared_ptr<spdlog::sinks::sink> &sink = nullptr);
void shutdown();

} // namespace logging
