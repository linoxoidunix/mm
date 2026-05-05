#pragma once

#include "mm/type/trade_side.h"
#include "mm/type/type_order_id.h"
#include <chrono>

struct ExternalTrade {
    uint64_t row = 0;
    int64_t timestamp = 0;
    TradeSide side = TradeSide::kBuy;
    double price = 0.0;
    double amount = 0.0;
    
    // ExternalTrade() = default;
};