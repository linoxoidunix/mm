#pragma once

#include <vector>
#include <chrono>

struct L2Snapshot {
    uint64_t row = 0;
    int64_t timestamp = 0;
    std::vector<std::pair<double, double>> asks; // price, amount
    std::vector<std::pair<double, double>> bids;
    
    L2Snapshot() : timestamp(0) {}
};