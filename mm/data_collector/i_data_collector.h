#pragma once

#include "mm/type/market_context.h"
#include "mm/type/fill.h"

class IDataCollector {
public:
    virtual void onMarketContext(const MarketContext& ctx) = 0;
    virtual void onFill(const Fill& fill) = 0;
    virtual ~IDataCollector() = default;
};

class IFastDataCollector {
public:
    virtual std::vector<double> onMarketContext(const MarketContext& ctx) = 0;
    virtual std::vector<double> onFill(const Fill& fill) = 0;
    virtual ~IFastDataCollector() = default;
};