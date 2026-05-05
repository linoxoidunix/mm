#pragma once

#include <chrono>
#include "mm/type/side.h"
#include "mm/type/order_type.h"
#include "mm/type/type_order_id.h"

struct Order {
    OrderRef order_id = ExternalOrderRef(-1);
    int64_t timestamp = 0;
    Side side = Side::kUnknown; // "bid" or "ask"
    OrderType type = OrderType::kUnknown;
    double price = 0;
    double amount = 0;
    double filled_amount = 0;
    double remaining() const { return amount - filled_amount; }
    bool is_filled() const { return remaining() < 1e-12; }
};