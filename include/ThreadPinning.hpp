#pragma once

#include <cstdint>
#include <thread>

void pin_thread_to_core(std::thread &t, uint32_t core_id);
