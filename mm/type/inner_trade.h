#pragma once

#include "mm/type/trade_side.h"
#include "mm/type/type_order_id.h"
#include <chrono>

struct InnerTrade {
    uint64_t row = 0;
    int64_t timestamp = 0;
    TradeSide side = TradeSide::kBuy;
    double price = 0.0;
    double amount = 0.0;
    
    OrderRef aggressor;
    OrderRef passive;
    
    InnerTrade() = default;
    
    InnerTrade(uint64_t r, int64_t ts, TradeSide s, double p, double a, const OrderRef& agg, const OrderRef& pass)
        : row(r), timestamp(ts), side(s), price(p), amount(a), aggressor(agg), passive(pass) {}
    
    // Вспомогательные методы для работы с OrderRef
    bool isOurOrder(const OrderRef& ref) const {
        return ref.isLimit() || ref.isMarket();
    }
    
    bool isOurTrade() const {
        return isOurOrder(aggressor) || isOurOrder(passive);
    }
    
    int64_t getOurOrderId() const {
        if (aggressor.isLimit() || aggressor.isMarket()) {
            return aggressor.getOrderId();
        }
        if (passive.isLimit() || passive.isMarket()) {
            return passive.getOrderId();
        }
        return 0;
    }
    
    enum class OurRole { kNone, kAggressor, kPassive, kBoth };
    
    OurRole getOurRole() const {
        bool isAggressorOur = aggressor.isLimit() || aggressor.isMarket();
        bool isPassiveOur = passive.isLimit() || passive.isMarket();
        
        if (isAggressorOur && isPassiveOur) return OurRole::kBoth;
        if (isAggressorOur) return OurRole::kAggressor;
        if (isPassiveOur) return OurRole::kPassive;
        return OurRole::kNone;
    }
};