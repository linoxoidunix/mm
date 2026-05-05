#pragma once

#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include "mm/type/external_trade.h"
#include "mm/type/l2snapshot.h"
#include "mm/constants/constants.h"

// Структура для уровня стакана
struct OrderBookLevel {
    double price;
    double volume;  // общий объем (чужие + наши)
    
    bool operator<(const OrderBookLevel& other) const {
        return price < other.price;
    }
};

// Результат применения события
struct OrderBookState {
    std::vector<OrderBookLevel> bids;  // отсортированы по убыванию цены
    std::vector<OrderBookLevel> asks;  // отсортированы по возрастанию цены
    int64_t timestamp;
};

class OrderBook {
public:
    OrderBook() = default;
    
    // Применить L2 снимок - заменить весь стакан
    void applyL2(const L2Snapshot& snap) {
        timestamp_ = snap.timestamp;
        
        // Очищаем текущий стакан
        bids_.clear();
        asks_.clear();
        
        // Загружаем bids (покупки) - сортируем по убыванию цены
        for (const auto& bid : snap.bids) {
            bids_.push_back({bid.first, bid.second});
        }
        std::sort(bids_.begin(), bids_.end(), 
            [](const OrderBookLevel& a, const OrderBookLevel& b) { 
                return a.price > b.price; 
            });
        
        // Загружаем asks (продажи) - сортируем по возрастанию цены
        for (const auto& ask : snap.asks) {
            asks_.push_back({ask.first, ask.second});
        }
        std::sort(asks_.begin(), asks_.end(),
            [](const OrderBookLevel& a, const OrderBookLevel& b) { 
                return a.price < b.price; 
            });
    }
    
    // Применить трейд - удалить ликвидность из стакана
    void applyTrade(const ExternalTrade& trade) {
        timestamp_ = trade.timestamp;
        
        if (trade.side == TradeSide::kBuy) {
            // Агрессивный покупатель снимает asks
            double remaining = trade.amount;
            
            for (size_t i = 0; i < asks_.size() && remaining > kVolEps; ) {
                double available = asks_[i].volume;
                
                if (available <= remaining + kVolEps) {
                    // Полностью снимаем уровень
                    remaining -= available;
                    asks_.erase(asks_.begin() + i);
                } else {
                    // Частично снимаем уровень
                    asks_[i].volume -= remaining;
                    remaining = 0;
                    ++i;
                }
            }
            
            // Если остался неудовлетворенный объем - он теряется (в реальности был бы проскальзывание)
            if (remaining > kVolEps) {
                // Можно залогировать
                // std::cout << "Warning: ExternalTrade of " << trade.amount 
                //           << " partially filled, " << remaining << " unfilled" << std::endl;
            }
        } 
        else if (trade.side == TradeSide::kSell) {
            // Агрессивный продавец снимает bids
            double remaining = trade.amount;
            
            for (size_t i = 0; i < bids_.size() && remaining > kVolEps; ) {
                double available = bids_[i].volume;
                
                if (available <= remaining + kVolEps) {
                    // Полностью снимаем уровень
                    remaining -= available;
                    bids_.erase(bids_.begin() + i);
                } else {
                    // Частично снимаем уровень
                    bids_[i].volume -= remaining;
                    remaining = 0;
                    ++i;
                }
            }
            
            if (remaining > kVolEps) {
                // std::cout << "Warning: ExternalTrade of " << trade.amount 
                //           << " partially filled, " << remaining << " unfilled" << std::endl;
            }
        }
    }
    
    // Получить текущее состояние стакана
    OrderBookState getState() const {
        return {bids_, asks_, timestamp_};
    }
    
    // Вывести состояние стакана (для отладки)
    void print() const {
        std::cout << "=== Order Book at " << timestamp_ << " ===" << std::endl;
        std::cout << "Bids (top 5):" << std::endl;
        for (size_t i = 0; i < std::min(bids_.size(), size_t(5)); ++i) {
            std::cout << "  Price: " << bids_[i].price 
                      << ", Volume: " << bids_[i].volume << std::endl;
        }
        std::cout << "Asks (top 5):" << std::endl;
        for (size_t i = 0; i < std::min(asks_.size(), size_t(5)); ++i) {
            std::cout << "  Price: " << asks_[i].price 
                      << ", Volume: " << asks_[i].volume << std::endl;
        }
    }
    
    // Геттеры
    const std::vector<OrderBookLevel>& getBids() const { return bids_; }
    const std::vector<OrderBookLevel>& getAsks() const { return asks_; }
    int64_t getTimestamp() const { return timestamp_; }
    
    // Лучшие цены
    double getBestBid() const { return bids_.empty() ? 0.0 : bids_[0].price; }
    double getBestAsk() const { return asks_.empty() ? 0.0 : asks_[0].price; }
    double getMidPrice() const { 
        if (bids_.empty() || asks_.empty()) return 0.0;
        return (getBestBid() + getBestAsk()) / 2.0;
    }
    
    // Объем на лучших ценах
    double getBestBidVolume() const { return bids_.empty() ? 0.0 : bids_[0].volume; }
    double getBestAskVolume() const { return asks_.empty() ? 0.0 : asks_[0].volume; }
    
private:
    std::vector<OrderBookLevel> bids_;  // sorted descending by price
    std::vector<OrderBookLevel> asks_;  // sorted ascending by price
    int64_t timestamp_ = 0;
};