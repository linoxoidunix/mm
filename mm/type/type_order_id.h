#pragma once

#include <variant>
#include <cstdint>
#include <compare>

// Типы ордеров, участвующих в сделке
struct LimitOrderRef {
    int64_t order_id;
    
    auto operator<=>(const LimitOrderRef&) const = default;
};

struct MarketOrderRef {
    int64_t order_id;
    
    auto operator<=>(const MarketOrderRef&) const = default;
};

struct ExternalOrderRef {
    int64_t order_id;  // Отрицательный ID для внешних
    
    auto operator<=>(const ExternalOrderRef&) const = default;
};

// Вариантный тип для ордера
struct OrderRef {
    std::variant<LimitOrderRef, MarketOrderRef, ExternalOrderRef> data;
    
    OrderRef() = default;
    OrderRef(LimitOrderRef ref) : data(ref) {}
    OrderRef(MarketOrderRef ref) : data(ref) {}
    OrderRef(ExternalOrderRef ref) : data(ref) {}
    
    // Получить order_id (общий для всех типов)
    int64_t getOrderId() const {
        if (std::holds_alternative<LimitOrderRef>(data)) {
            return std::get<LimitOrderRef>(data).order_id;
        }
        if (std::holds_alternative<MarketOrderRef>(data)) {
            return std::get<MarketOrderRef>(data).order_id;
        }
        if (std::holds_alternative<ExternalOrderRef>(data)) {
            return std::get<ExternalOrderRef>(data).order_id;
        }
        return 0;
    }
    
    // Проверить, является ли ордер нашим (Limit или Market)
    bool isOurOrder() const {
        return std::holds_alternative<LimitOrderRef>(data) || 
               std::holds_alternative<MarketOrderRef>(data);
    }
    
    // Проверить тип ордера
    bool isLimit() const { return std::holds_alternative<LimitOrderRef>(data); }
    bool isMarket() const { return std::holds_alternative<MarketOrderRef>(data); }
    bool isExternal() const { return std::holds_alternative<ExternalOrderRef>(data); }
    
    // Сравнение по order_id
    bool operator==(const OrderRef& other) const {
        return getOrderId() == other.getOrderId();
    }
    
    bool operator!=(const OrderRef& other) const {
        return getOrderId() != other.getOrderId();
    }
    
    bool operator<(const OrderRef& other) const {
        return getOrderId() < other.getOrderId();
    }
    
    bool operator>(const OrderRef& other) const {
        return getOrderId() > other.getOrderId();
    }
    
    // Сравнение с int64_t
    bool operator==(int64_t id) const {
        return getOrderId() == id;
    }
    
    bool operator!=(int64_t id) const {
        return getOrderId() != id;
    }
    
    bool operator<(int64_t id) const {
        return getOrderId() < id;
    }
    
    bool operator>(int64_t id) const {
        return getOrderId() > id;
    }
};

// Для удобства использования с std::hash (если нужно в unordered_map/set)
namespace std {
    template<>
    struct hash<OrderRef> {
        size_t operator()(const OrderRef& ref) const {
            return hash<int64_t>()(ref.getOrderId());
        }
    };
}