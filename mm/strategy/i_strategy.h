#pragma once

#include "mm/type/market_context.h"
#include "mm/type/fill.h"

class HFTBacktester;

// Абстрактный класс стратегии
class IStrategy {
public:
    virtual ~IStrategy() = default;

    // Вызывается при каждом обновлении стакана
    virtual void onMarketContext(const MarketContext& ctx, HFTBacktester* api) = 0;
    
    // Вызывается при исполнении ордера
    virtual void onFill(const Fill& fill) {}
};