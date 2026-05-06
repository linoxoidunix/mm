#pragma once
#include <iostream>
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>
#include "mm/type/order.h"
#include "mm/type/inner_trade.h"
#include "mm/type/type_order_id.h"
#include "mm/type/me_l2snapshot.h"
#include "mm/constants/constants.h"
// Глобальная константа для сравнений

// Структура ордера в книге
struct BookOrder {
    OrderRef order_id;
    double price;
    double amount;
    double filled_amount;
    int64_t timestamp;
    Side side;
    OrderType type;  // Добавляем тип
    
    double remaining() const { return amount - filled_amount; }
    bool is_filled() const { return remaining() < kVolEps; }
};

// Уровень стакана с очередью ордеров
struct OrderBookLevel {
    double price;
    std::deque<BookOrder> orders;  // FIFO очередь
    
    double total_volume() const {
        double vol = 0;
        for (const auto& order : orders) {
            vol += order.remaining();
        }
        return vol;
    }
};

class MatchingEngine {
public:
    MatchingEngine() = default;

    //asks price, asks qty
    //bid price, bid qty
    void addSnapshot(std::vector<std::pair<double, double>> &&asks, std::vector<std::pair<double, double>> &&bids) {
        // Сохраняем наши ордера (быстрое копирование)
        std::vector<BookOrder> our_bids_orders;
        std::vector<BookOrder> our_asks_orders;
        
        // Сохраняем наши ордера из bids (уже отсортированы по убыванию цены)
        for (auto& level : bids_) {
            for (auto& order : level.orders) {
                if (order.order_id.isLimit() || order.order_id.isMarket()) {
                    our_bids_orders.push_back(order);
                }
            }
        }
        
        // Сохраняем наши ордера из asks (уже отсортированы по возрастанию цены)
        for (auto& level : asks_) {
            for (auto& order : level.orders) {
                if (order.order_id.isLimit() || order.order_id.isMarket()) {
                    our_asks_orders.push_back(order);
                }
            }
        }
        
        // Очищаем стакан
        bids_.clear();
        asks_.clear();
        
        // Для bids - идем параллельно по снапшоту и нашим ордерам (оба отсортированы по убыванию)
        bids_.reserve(std::max(bids.size(), our_bids_orders.size()));
        
        size_t snap_idx = 0;
        size_t our_idx = 0;
            
        while (snap_idx < bids.size() || our_idx < our_bids_orders.size()) {
            OrderBookLevel level;
            
            if (snap_idx < bids.size() && our_idx < our_bids_orders.size()) {
                double snap_price = bids[snap_idx].first;
                double our_price = our_bids_orders[our_idx].price;
                
                if (snap_price > our_price + kPriceEps) {
                    // Снапшот цена выше - добавляем его
                    level.price = snap_price;
                    BookOrder external;
                    external.order_id = ExternalOrderRef{0};
                    external.price = snap_price;
                    external.amount = bids[snap_idx].second;
                    external.filled_amount = 0;
                    external.side = Side::kBid;
                    external.type = OrderType::kLimit;
                    level.orders.push_back(external);
                    snap_idx++;
                    bids_.push_back(std::move(level));
                } 
                else if (our_price > snap_price + kPriceEps) {
                    // Наш ордер цена выше - добавляем его
                    level.price = our_price;
                    BookOrder our;
                    our.order_id = our_bids_orders[our_idx].order_id;
                    our.price = our_price;
                    our.amount = our_bids_orders[our_idx].amount;
                    our.filled_amount = our_bids_orders[our_idx].filled_amount;
                    our.side = Side::kBid;
                    our.type = OrderType::kLimit;
                    level.orders.push_back(our);
                    our_idx++;
                    bids_.push_back(std::move(level));
                } 
                else {
                    // Цены совпадают - объединяем в один уровень
                    level.price = snap_price;
                    
                    // ВАЖНО: сначала добавляем НАШ ордер (он уже здесь давно, а новый пришёл только что)
                    BookOrder our;
                    our.order_id = our_bids_orders[our_idx].order_id;
                    our.price = snap_price;
                    our.amount = our_bids_orders[our_idx].amount;
                    our.filled_amount = our_bids_orders[our_idx].filled_amount;
                    our.side = Side::kBid;
                    our.type = OrderType::kLimit;
                    level.orders.push_back(our);
                    
                    // Потом внешний ордер
                    BookOrder external;
                    external.order_id = ExternalOrderRef{0};
                    external.price = snap_price;
                    external.amount = bids[snap_idx].second;
                    external.filled_amount = 0;
                    external.side = Side::kBid;
                    external.type = OrderType::kLimit;
                    level.orders.push_back(external);
                    
                    snap_idx++;
                    our_idx++;
                    bids_.push_back(std::move(level));
                }
            } 
            else if (snap_idx < bids.size()) {
                // Только снапшот
                level.price = bids[snap_idx].first;
                BookOrder external;
                external.order_id = ExternalOrderRef{0};
                external.price = level.price;
                external.amount = bids[snap_idx].second;
                external.filled_amount = 0;
                external.side = Side::kBid;
                external.type = OrderType::kLimit;
                level.orders.push_back(external);
                snap_idx++;
                bids_.push_back(std::move(level));
            } 
            else {
                // Только наши ордера - нужно добавить в существующий уровень или создать новый
                double our_price = our_bids_orders[our_idx].price;
                
                // Проверяем, есть ли уже уровень с такой ценой
                bool found = false;
                for (auto& existing_level : bids_) {
                    if (std::abs(existing_level.price - our_price) < kPriceEps) {
                        // Добавляем наш ордер в существующий уровень
                        BookOrder our;
                        our.order_id = our_bids_orders[our_idx].order_id;
                        our.price = our_price;
                        our.amount = our_bids_orders[our_idx].amount;
                        our.filled_amount = our_bids_orders[our_idx].filled_amount;
                        our.side = Side::kBid;
                        our.type = OrderType::kLimit;
                        existing_level.orders.push_back(our);
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    // Создаем новый уровень
                    level.price = our_price;
                    BookOrder our;
                    our.order_id = our_bids_orders[our_idx].order_id;
                    our.price = our_price;
                    our.amount = our_bids_orders[our_idx].amount;
                    our.filled_amount = our_bids_orders[our_idx].filled_amount;
                    our.side = Side::kBid;
                    our.type = OrderType::kLimit;
                    level.orders.push_back(our);
                    
                    // Вставляем в правильное место
                    auto insert_pos = std::lower_bound(bids_.begin(), bids_.end(), level,
                        [](const OrderBookLevel& a, const OrderBookLevel& b) {
                            return a.price > b.price;
                        });
                    bids_.insert(insert_pos, std::move(level));
                }
                our_idx++;
            }
        }
        
        // Для asks - идем параллельно (оба отсортированы по возрастанию цены)
        asks_.reserve(std::max(asks.size(), our_asks_orders.size()));
        
        snap_idx = 0;
        our_idx = 0;
            
        while (snap_idx < asks.size() || our_idx < our_asks_orders.size()) {
            OrderBookLevel level;
            
            if (snap_idx < asks.size() && our_idx < our_asks_orders.size()) {
                double snap_price = asks[snap_idx].first;
                double our_price = our_asks_orders[our_idx].price;
                
                if (snap_price < our_price - kPriceEps) {
                    // Снапшот цена ниже - добавляем его
                    level.price = snap_price;
                    BookOrder external;
                    external.order_id = ExternalOrderRef{0};
                    external.price = snap_price;
                    external.amount = asks[snap_idx].second;
                    external.filled_amount = 0;
                    external.side = Side::kAsk;
                    external.type = OrderType::kLimit;
                    level.orders.push_back(external);
                    snap_idx++;
                    asks_.push_back(std::move(level));
                } 
                else if (our_price < snap_price - kPriceEps) {
                    // Наш ордер цена ниже - добавляем его
                    level.price = our_price;
                    BookOrder our;
                    our.order_id = our_asks_orders[our_idx].order_id;
                    our.price = our_price;
                    our.amount = our_asks_orders[our_idx].amount;
                    our.filled_amount = our_asks_orders[our_idx].filled_amount;
                    our.side = Side::kAsk;
                    our.type = OrderType::kLimit;
                    level.orders.push_back(our);
                    our_idx++;
                    asks_.push_back(std::move(level));
                } 
                else {
                    // Цены совпадают - объединяем
                    level.price = snap_price;
                    
                    // Внешний ордер
                    BookOrder external;
                    external.order_id = ExternalOrderRef{0};
                    external.price = snap_price;
                    external.amount = asks[snap_idx].second;
                    external.filled_amount = 0;
                    external.side = Side::kAsk;
                    external.type = OrderType::kLimit;
                    level.orders.push_back(external);
                    
                    // Наш ордер
                    BookOrder our;
                    our.order_id = our_asks_orders[our_idx].order_id;
                    our.price = snap_price;
                    our.amount = our_asks_orders[our_idx].amount;
                    our.filled_amount = our_asks_orders[our_idx].filled_amount;
                    our.side = Side::kAsk;
                    our.type = OrderType::kLimit;
                    level.orders.push_back(our);
                    
                    snap_idx++;
                    our_idx++;
                    asks_.push_back(std::move(level));
                }
            } 
            else if (snap_idx < asks.size()) {
                // Только снапшот
                level.price = asks[snap_idx].first;
                BookOrder external;
                external.order_id = ExternalOrderRef{0};
                external.price = level.price;
                external.amount = asks[snap_idx].second;
                external.filled_amount = 0;
                external.side = Side::kAsk;
                external.type = OrderType::kLimit;
                level.orders.push_back(external);
                snap_idx++;
                asks_.push_back(std::move(level));
            } 
            else {
                // Только наши ордера
                double our_price = our_asks_orders[our_idx].price;
                
                // Проверяем, есть ли уже уровень с такой ценой
                bool found = false;
                for (auto& existing_level : asks_) {
                    if (std::abs(existing_level.price - our_price) < kPriceEps) {
                        // Добавляем наш ордер в существующий уровень
                        BookOrder our;
                        our.order_id = our_asks_orders[our_idx].order_id;
                        our.price = our_price;
                        our.amount = our_asks_orders[our_idx].amount;
                        our.filled_amount = our_asks_orders[our_idx].filled_amount;
                        our.side = Side::kAsk;
                        our.type = OrderType::kLimit;
                        existing_level.orders.push_back(our);
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    // Создаем новый уровень
                    level.price = our_price;
                    BookOrder our;
                    our.order_id = our_asks_orders[our_idx].order_id;
                    our.price = our_price;
                    our.amount = our_asks_orders[our_idx].amount;
                    our.filled_amount = our_asks_orders[our_idx].filled_amount;
                    our.side = Side::kAsk;
                    our.type = OrderType::kLimit;
                    level.orders.push_back(our);
                    
                    // Вставляем в правильное место
                    auto insert_pos = std::lower_bound(asks_.begin(), asks_.end(), level,
                        [](const OrderBookLevel& a, const OrderBookLevel& b) {
                            return a.price < b.price;
                        });
                    asks_.insert(insert_pos, std::move(level));
                }
                our_idx++;
            }
        }
    }
    
    // Добавить лимитный ордер
    std::vector<InnerTrade> addLimitOrder(Order& order) {
        std::vector<InnerTrade> trades;
        
        if (order.side == Side::kBid) {
            trades = matchBuyOrder(order);
            if (order.amount - order.filled_amount > kVolEps) {
                addToBids(order);
            }
        } else {
            trades = matchSellOrder(order);
            if (order.amount - order.filled_amount > kVolEps) {
                addToAsks(order);
            }
        }
        
        return trades;
    }
    
    // Добавить рыночный ордер
    std::vector<InnerTrade> addMarketOrder(Order& order) {
        std::vector<InnerTrade> trades;
        
        if (order.side == Side::kBid) {
            // Рыночный ордер на покупку - исполняется по лучшему ask
            trades = matchMarketBuyOrder(order);
        } else {
            // Рыночный ордер на продажу - исполняется по лучшему bid
            trades = matchMarketSellOrder(order);
        }
        
        // Рыночные ордера не попадают в книгу (если не полностью исполнились)
        // В реальности, если не хватило ликвидности, ордер может быть отменен или превращен в лимитный
        auto delta = order.amount - order.filled_amount;
        if (delta > 1000000){
            //int x = 0;
        }
        if (delta > kVolEps) {
            // Можно либо отменить, либо превратить в лимитный по последней цене
            // std::cout << "Warning: Market order partially filled. " 
            //           << order.amount - order.filled_amount << " units unfilled." << std::endl;
        }
        
        return trades;
    }
    
    // Универсальный метод для добавления ордера
    std::vector<InnerTrade> addOrder(Order& order) {
        if (order.type == OrderType::kMarket) {
            return addMarketOrder(order);
        } else if (order.type == OrderType::kMarket){
            return addLimitOrder(order);
        }
    }
    
    // Отменить ордер
    bool cancelOrder(OrderRef order_id) {
        // Ищем в bids
        for (auto& level : bids_) {
            auto it = std::find_if(level.orders.begin(), level.orders.end(),
                [order_id](const BookOrder& o) { return o.order_id == order_id; });
            
            if (it != level.orders.end()) {
                level.orders.erase(it);
                if (level.orders.empty()) {
                    bids_.erase(std::remove_if(bids_.begin(), bids_.end(),
                        [](const OrderBookLevel& l) { return l.orders.empty(); }), bids_.end());
                }
                return true;
            }
        }
        
        // Ищем в asks
        for (auto& level : asks_) {
            auto it = std::find_if(level.orders.begin(), level.orders.end(),
                [order_id](const BookOrder& o) { return o.order_id == order_id; });
            
            if (it != level.orders.end()) {
                level.orders.erase(it);
                if (level.orders.empty()) {
                    asks_.erase(std::remove_if(asks_.begin(), asks_.end(),
                        [](const OrderBookLevel& l) { return l.orders.empty(); }), asks_.end());
                }
                return true;
            }
        }
        
        return false;
    }
    
    // Получить текущий стакан
    std::vector<std::pair<double, double>> getBids(int depth = 25) const {
        std::vector<std::pair<double, double>> result;
        for (size_t i = 0; i < std::min(bids_.size(), size_t(depth)); ++i) {
            result.emplace_back(bids_[i].price, bids_[i].total_volume());
        }
        return result;
    }
    
    std::vector<std::pair<double, double>> getAsks(int depth = 25) const {
        std::vector<std::pair<double, double>> result;
        for (size_t i = 0; i < std::min(asks_.size(), size_t(depth)); ++i) {
            result.emplace_back(asks_[i].price, asks_[i].total_volume());
        }
        return result;
    }
    
    // Получить лучшие цены
    double getBestBid() const { return bids_.empty() ? 0 : bids_[0].price; }
    double getBestAsk() const { return asks_.empty() ? 0 : asks_[0].price; }
    double getSpread() const { 
        if (bids_.empty() || asks_.empty()) return 0;
        return getBestAsk() - getBestBid();
    }
    
    // Очистить книгу
    void clear() {
        bids_.clear();
        asks_.clear();
    }

    // Отменить все наши ордера (быстрая версия)
    void cancelAllOurOrders() {
        // Проходим по всем уровням bids
        for (auto& level : bids_) {
            auto& orders = level.orders;
            // Удаляем все наши ордера за один проход
            for (auto it = orders.begin(); it != orders.end(); ) {
                if (it->order_id.isLimit() || it->order_id.isMarket()) {
                    it = orders.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // Проходим по всем уровням asks
        for (auto& level : asks_) {
            auto& orders = level.orders;
            for (auto it = orders.begin(); it != orders.end(); ) {
                if (it->order_id.isLimit() || it->order_id.isMarket()) {
                    it = orders.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // Удаляем пустые уровни
        removeEmptyLevels();
    }
    
    void print() const {
        std::cout << "=== Order Book ===" << std::endl;
        std::cout << "Bids (top 5):" << std::endl;
        for (size_t i = 0; i < std::min(bids_.size(), size_t(5)); ++i) {
            std::cout << "  Price: " << bids_[i].price 
                      << ", Volume: " << bids_[i].total_volume() << std::endl;
        }
        std::cout << "Asks (top 5):" << std::endl;
        for (size_t i = 0; i < std::min(asks_.size(), size_t(5)); ++i) {
            std::cout << "  Price: " << asks_[i].price 
                      << ", Volume: " << asks_[i].total_volume() << std::endl;
        }
        std::cout << "Spread: " << getSpread() << std::endl;
    }

        // Для максимальной производительности - заполнить готовый снимок
    MEL2Snapshot fillL2Snapshot() const {
        MEL2Snapshot snapshot;
        snapshot.asks.clear();
        snapshot.bids.clear();
        
        snapshot.asks.reserve(asks_.size());
        for (const auto& level : asks_) {
            snapshot.asks.emplace_back(level.price, level.total_volume());
        }
        
        snapshot.bids.reserve(bids_.size());
        for (const auto& level : bids_) {
            snapshot.bids.emplace_back(level.price, level.total_volume());
        }
        return snapshot;
    }
    
private:
    std::vector<OrderBookLevel> bids_;  // sorted descending by price
    std::vector<OrderBookLevel> asks_;  // sorted ascending by price
    
    // Добавить лимитный ордер в bids
    void addToBids(const Order& order) {
        BookOrder book_order{
            order.order_id,
            order.price,
            order.amount,
            order.filled_amount,
            order.timestamp,
            order.side,
            order.type
        };

        // Ищем позицию, где цена <= order.price (bids: по убыванию)
        auto it = std::lower_bound(bids_.begin(), bids_.end(), order.price,
            [](const OrderBookLevel& level, double price) {
                return level.price > price;
            });

        // Проверяем, есть ли уже уровень с этой ценой
        if (it != bids_.end() && std::abs(it->price - order.price) < kPriceEps) {
            it->orders.push_back(book_order);
        } else {
            // Вставляем новый уровень
            OrderBookLevel new_level{order.price, {book_order}};
            bids_.insert(it, new_level);
        }
    }
    
    void addToAsks(const Order& order) {
        BookOrder book_order{
            order.order_id, order.price, order.amount,
            order.filled_amount, order.timestamp, order.side, order.type
        };

        // Ищем позицию, где цена >= order.price
        auto it = std::lower_bound(asks_.begin(), asks_.end(), order.price,
            [](const OrderBookLevel& level, double price) {
                return level.price < price; 
            });

        // Проверяем, нашли ли мы существующий уровень цены
        if (it != asks_.end() && std::abs(it->price - order.price) < kPriceEps) {
            it->orders.push_back(book_order);
        } else {
            // Создаем новый уровень и вставляем его в правильную позицию
            OrderBookLevel new_level{order.price, {book_order}};
            asks_.insert(it, new_level); 
        }
    }
    
    std::vector<InnerTrade> matchBuyOrder(Order& order) {
        std::vector<InnerTrade> trades;
        double remaining = order.remaining();
        
        for (auto it = asks_.begin(); it != asks_.end() && remaining > kVolEps; ) {
            auto& level = *it;
            
            if (order.price < level.price - kPriceEps) {
                break;
            }
            
            while (!level.orders.empty() && remaining > kVolEps) {
                auto& book_order = level.orders.front();
                double order_remaining = book_order.remaining();
                
                if (order_remaining <= kVolEps) {
                    level.orders.pop_front();
                    continue;
                }
                
                // Вычисляем, сколько можем исполнить
                double fill_amount = std::min(remaining, order_remaining);
                
                InnerTrade trade;
                trade.timestamp = std::max(order.timestamp, book_order.timestamp);
                trade.side = TradeSide::kBuy;
                trade.price = level.price;
                trade.amount = fill_amount;
                trade.aggressor = order.order_id;
                trade.passive = book_order.order_id;
                trades.push_back(trade);
                
                // Обновляем ордера
                order.filled_amount += fill_amount;
                book_order.filled_amount += fill_amount;
                remaining -= fill_amount;
                
                // ВАЖНО: Обновляем объем в уровне
                // book_order.filled_amount уже обновлен, объем уменьшится при вызове remaining()
                
                if (book_order.is_filled()) {
                    level.orders.pop_front();
                }
            }
            
            // Если уровень опустел - удаляем его
            if (level.orders.empty() || level.total_volume() <= kVolEps) {
                it = asks_.erase(it);
            } else {
                ++it;
            }
        }
        
        return trades;
    }

    std::vector<InnerTrade> matchSellOrder(Order& order) {
        std::vector<InnerTrade> trades;
        double remaining = order.remaining();
        
        for (auto it = bids_.begin(); it != bids_.end() && remaining > kVolEps; ) {
            auto& level = *it;
            
            if (order.price > level.price + kPriceEps) {
                break;
            }
            
            while (!level.orders.empty() && remaining > kVolEps) {
                auto& book_order = level.orders.front();
                double order_remaining = book_order.remaining();
                
                if (order_remaining <= kVolEps) {
                    level.orders.pop_front();
                    continue;
                }
                
                double fill_amount = std::min(remaining, order_remaining);
                
                InnerTrade trade;
                trade.timestamp = std::max(order.timestamp, book_order.timestamp);
                trade.side = TradeSide::kSell;
                trade.price = level.price;
                trade.amount = fill_amount;
                trade.aggressor = order.order_id;
                trade.passive = book_order.order_id;
                trades.push_back(trade);
                
                order.filled_amount += fill_amount;
                book_order.filled_amount += fill_amount;
                remaining -= fill_amount;
                
                if (book_order.is_filled()) {
                    level.orders.pop_front();
                }
            }
            
            if (level.orders.empty() || level.total_volume() <= kVolEps) {
                it = bids_.erase(it);
            } else {
                ++it;
            }
        }
        
        return trades;
    }
    
    /**
     * Матчинг рыночного ордера на покупку (Market Buy).
     * Если order.price > 0, она используется как контрольная цена (из публичного трейда)
     * для проверки наличия ликвидности на этом уровне.
     */
    std::vector<InnerTrade> matchMarketBuyOrder(Order& order) {
        std::vector<InnerTrade> trades;
        double remaining = order.remaining();
        
        // Используем цену ордера как референсную, если она задана
        const bool has_ref_price = (order.price > kPriceEps);
        
        // Если есть референсная цена, проверяем наличие уровня
        if (has_ref_price) {
            bool has_matching_level = false;
            for (const auto& level : asks_) {
                if (std::abs(level.price - order.price) < kPriceEps) {
                    has_matching_level = true;
                    break;
                }
            }
            
            // Нет такого уровня в стакане → невозможно исполнить трейд
            if (!has_matching_level) {
                //такое возможно если стакан не успел одновиться
                return trades; // Пустой вектор → трейд не исполняется
            }
        }

        for (auto it = asks_.begin(); it != asks_.end() && remaining > kVolEps; ) {
            auto& level = *it;
            
            // Логируем "удар" по уровню, если цена отличается от той, что была в публичном трейде
            if (has_ref_price) {
                double slippage = level.price - order.price;
                // Логируем только значимые отклонения (например, более kPriceEps)
                if (std::abs(slippage) > kPriceEps) {
                    //
                    // std::cout << "[Matching] Market Buy " << order.order_id.getOrderId() 
                    //         << " hitting Ask Level " << level.price 
                    //         << " | Ref Price: " << order.price 
                    //         << " | Slippage: " << std::fixed << std::setprecision(8) << slippage 
                    //         << std::endl;
                }
            }

            while (!level.orders.empty() && remaining > kVolEps) {
                auto& book_order = level.orders.front();
                double order_remaining = book_order.remaining();
                
                // Пропускаем "пыль" в стакане
                if (order_remaining <= kVolEps) {
                    level.orders.pop_front();
                    continue;
                }
                
                double fill_amount = std::min(remaining, order_remaining);
                
                // Формируем внутреннюю сделку
                InnerTrade trade;
                trade.timestamp = std::max(order.timestamp, book_order.timestamp);
                trade.side = TradeSide::kBuy;
                trade.price = level.price;
                trade.amount = fill_amount;
                trade.aggressor = order.order_id;
                trade.passive = book_order.order_id;
                trades.push_back(trade);
                
                // Обновляем состояние ордеров
                order.filled_amount += fill_amount;
                book_order.filled_amount += fill_amount;
                remaining -= fill_amount;
                
                // Если лимитка в стакане полностью исполнена — удаляем её
                if (book_order.is_filled() || book_order.remaining() <= kVolEps) {
                    level.orders.pop_front();
                }
            }
            
            // Если на уровне не осталось лимиток — удаляем уровень целиком
            if (level.orders.empty()) {
                it = asks_.erase(it);
            } else {
                ++it;
            }
        }
        return trades;
    }
    
    /**
     * Матчинг рыночного ордера на продажу (Market Sell).
     * Если order.price > 0, она используется как контрольная цена (из публичного трейда)
     * для проверки наличия ликвидности на этом уровне.
     */
    std::vector<InnerTrade> matchMarketSellOrder(Order& order) {
        std::vector<InnerTrade> trades;
        double remaining = order.remaining();
        
        // Используем цену ордера как референсную, если она задана
        const bool has_ref_price = (order.price > kPriceEps);
        
        // Если есть референсная цена, проверяем наличие уровня в bids
        if (has_ref_price) {
            bool has_matching_level = false;
            for (const auto& level : bids_) {
                if (std::abs(level.price - order.price) < kPriceEps) {
                    has_matching_level = true;
                    break;
                }
            }
            
            // Нет такого уровня в стакане → невозможно исполнить трейд
            if (!has_matching_level) {
                // Такое возможно если стакан не успел обновиться
                return trades; // Пустой вектор → трейд не исполняется
            }
        }

        // Итерируемся по бидам (покупателям)
        for (auto it = bids_.begin(); it != bids_.end() && remaining > kVolEps; ) {
            auto& level = *it;
            
            // Логируем "удар" по уровню Bid-ов
            if (has_ref_price) {
                // Для продажи проскальзывание положительное, если цена уровня НИЖЕ референса
                double slippage = order.price - level.price;
                if (std::abs(slippage) > kPriceEps) {
                    // std::cout << "[Matching] Market Sell " << order.order_id.getOrderId() 
                    //         << " hitting Bid Level " << level.price 
                    //         << " | Ref Price: " << order.price 
                    //         << " | Slippage: " << std::fixed << std::setprecision(8) << slippage 
                    //         << std::endl;
                }
            }

            while (!level.orders.empty() && remaining > kVolEps) {
                auto& book_order = level.orders.front();
                double order_remaining = book_order.remaining();
                
                // Чистим стакан от микро-остатков
                if (order_remaining <= kVolEps) {
                    level.orders.pop_front();
                    continue;
                }
                
                double fill_amount = std::min(remaining, order_remaining);
                
                InnerTrade trade;
                trade.timestamp = std::max(order.timestamp, book_order.timestamp);
                trade.side = TradeSide::kSell;
                trade.price = level.price;
                trade.amount = fill_amount;
                trade.aggressor = order.order_id;
                trade.passive = book_order.order_id;
                trades.push_back(trade);
                
                order.filled_amount += fill_amount;
                book_order.filled_amount += fill_amount;
                remaining -= fill_amount;
                
                if (book_order.is_filled() || book_order.remaining() <= kVolEps) {
                    level.orders.pop_front();
                }
            }
            
            // Если уровень пуст, удаляем его
            if (level.orders.empty()) {
                it = bids_.erase(it);
            } else {
                ++it;
            }
        }
        
        // Проверка на нехватку ликвидности
        // она будет очень часто так как стакан обновляется очень редко
        // if (remaining > kVolEps && !has_ref_price) {
        //     // Только для реальных рыночных ордеров (без референсной цены)
        //     std::cerr << "Warning: Market Sell order " << order.order_id.getOrderId() 
        //             << " partially filled. Remaining: " << remaining 
        //             << " units. Bids exhausted." << std::endl;
        // }
        
        return trades;
    }

    void removeEmptyLevels() {
        bids_.erase(
            std::remove_if(bids_.begin(), bids_.end(),
                [](const OrderBookLevel& level) { return level.orders.empty(); }),
            bids_.end());
        
        asks_.erase(
            std::remove_if(asks_.begin(), asks_.end(),
                [](const OrderBookLevel& level) { return level.orders.empty(); }),
            asks_.end());
    }

    double getLevelVolume(const OrderBookLevel& level) const {
        double vol = 0;
        for (const auto& order : level.orders) {
            vol += order.remaining();
        }
        return vol;
    }
};