#pragma once

#include <vector>
#include <chrono>

//matching engime l2 snapshot
struct MEL2Snapshot {
    std::vector<std::pair<double, double>> asks; // price, amount
    std::vector<std::pair<double, double>> bids;    
};