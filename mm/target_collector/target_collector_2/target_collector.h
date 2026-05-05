#pragma once

#include <vector>
#include <deque>
#include <cmath>
#include <cstdint>

#include "mm/target_collector/i_target_collector.h"
#include "mm/type/fill.h"
#include "mm/type/market_context.h"
#include "mm/type/me_l2snapshot.h"

struct TargetCollectorConfig {
    double max_inventory = 0;
    //size_t reserve_size = 10000000; // Предварительное выделение памяти под 10 млн тиков
};

class MyTargetCollector : public IFastTargetCollector {
public:
    MyTargetCollector(TargetCollectorConfig&& _config) : config(std::move(_config)) {
    }
    
    std::vector<double> onMarketContext(const MarketContext& ctx) override {
        if (ctx.l2.bids.empty() || ctx.l2.asks.empty()) return {};

        double mid = (ctx.l2.bids[0].first + ctx.l2.asks[0].first) / 2.0;
        
        //std::vector<double> result = { static_cast<float>(mid), static_cast<float>(computeMicroprice(ctx.l2))};
        //return result;
        return {};
    }

    std::vector<double> onFill(const Fill& fill) override {
        // В данном случае onFill не нужен, так как мы договорились 
        // считать таргеты через смещение цены (mid/micro) в Python
        return {};
    }

private:
    TargetCollectorConfig config;
};