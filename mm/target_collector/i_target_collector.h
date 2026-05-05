#pragma once

#include "mm/type/market_context.h"
#include "mm/type/fill.h"

class ITargetCollector {
public:
    virtual void onMarketContext(const MarketContext& ctx) = 0;
    virtual void onFill(const Fill& fill) = 0;
    virtual ~ITargetCollector() = default;
};

class IFastTargetCollector {
public:
    virtual std::vector<double> onMarketContext(const MarketContext& ctx) = 0;
    virtual std::vector<double> onFill(const Fill& fill) = 0;
    virtual ~IFastTargetCollector() = default;
};