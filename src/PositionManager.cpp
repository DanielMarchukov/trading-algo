#include "PositionManager.hpp"
#include "Utils.hpp"

void PositionManager::onFill(const Fill &fill) {
    SymbolKey key{};
    strncpy(key.value, fill.symbol, sizeof(key.value) - 1);

    if (fill.side == OrderSide::Buy) {
        positions_[key].fetch_add(fill.quantity, std::memory_order_relaxed);
    } else {
        positions_[key].fetch_sub(fill.quantity, std::memory_order_relaxed);
    }
}

int PositionManager::getPosition(std::string_view symbol) const {
    SymbolKey key{};
    strncpy(key.value, symbol.data(),
            std::min(symbol.size(), sizeof(key.value) - 1));
    const auto it = positions_.find(key);
    if (it == positions_.end()) {
        return 0;
    }

    return it->second.load(std::memory_order_relaxed);
}
