#pragma once

#include "mm/type/side.h"
#include <chrono>

struct Fill {
    int64_t timestamp;
    int64_t order_id;
    Side side;
    double price;
    double amount;
    double cash_delta;
    double inventory_delta;
};