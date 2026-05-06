#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <deque>
#include <string>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <cstdint>
#include <random>

// Твои инклуды
#include "mm/type/external_trade.h"
#include "mm/type/l2snapshot.h"
#include "mm/type/order.h"
#include "mm/type/fill.h"
#include "mm/util/to_num.h"
#include "mm/strategy/i_strategy.h"
#include "mm/type/market_context.h"
#include "mm/data_collector/i_data_collector.h"
#include "mm/matching_engine/matching_engine.h"
#include "mm/history_manager/history_manager.h"

struct SimulationState {
    size_t current_l2_idx;
    size_t current_trade_idx;
    double current_cash;
    double current_inventory;
};

struct HFTBacktesterConfig{
    double inventory = 0.0;
    double cash = 1000000.0;
    double turnover = 0.0;
    double execution_probability = 1.0;
    bool use_probabilistic_execution = false;
};

class HFTBacktester {
private:
    HFTBacktesterConfig config;
    std::unique_ptr<IStrategy> strategy;
    std::unique_ptr<IFastDataCollector> data_collector;

    // Данные
    std::shared_ptr<HistoryManager> history_manager;
    bool use_external_history_manager = true;
    
    // Состояние ордеров
    std::deque<Order> active_orders;
    std::vector<Fill> fills;
    
    // Состояние портфеля
    int64_t next_order_id = 1;
    double inventory;
    double cash;
    double equity = 0.0;             // Текущая стоимость портфеля
    double total_profit = 0.0;       // Общая прибыль (realized + unrealized)uble total_pnl;
    double turnover;
    
    // Текущее состояние (обновляется событиями)
    L2Snapshot current_l2;
    int64_t start_timestamp = 0;
    int64_t current_timestamp = 0;
    MatchingEngine matching_engine;
   
    // История для отчетов
    std::vector<std::pair<int64_t, double>> inventory_history;
    std::vector<std::pair<int64_t, double>> portfolio_value;

    int total_orders_placed = 0;
    int total_fills = 0;

    double realized_pnl = 0.0;      // Реализованная прибыль
    double unrealized_pnl = 0.0;    // Нереализованная прибыль
    double avg_entry_price = 0.0;   // Средняя цена входа для позиции
    double total_buy_qty = 0.0;     // Общее количество купленного
    double total_buy_value = 0.0;   // Суммарная стоимость покупок
    double total_sell_qty = 0.0;    // Общее количество проданного
    double total_sell_value = 0.0;  // Суммарная стоимость продаж

public:
    HFTBacktester(HFTBacktesterConfig&& _config) : config(std::move(_config)) {
        use_external_history_manager = false;
        inventory_history.reserve(10000000);
        portfolio_value.reserve(10000000);
        
        inventory = config.inventory;
        cash = config.cash;
        turnover = config.turnover;
    }
    HFTBacktester(HFTBacktesterConfig&& _config, std::shared_ptr<HistoryManager> _history_manager) : config(std::move(_config)) {
        history_manager = _history_manager;
        inventory_history.reserve(10000000);
        portfolio_value.reserve(10000000);
        
        inventory = config.inventory;
        cash = config.cash;
        turnover = config.turnover;
    }

    void setStrategy(std::unique_ptr<IStrategy> s) { strategy = std::move(s); }
    void setDataCollector(std::unique_ptr<IFastDataCollector> f) { data_collector = std::move(f); }

    // ==============================
    // API ДЛЯ СТРАТЕГИИ
    // ==============================
    OrderRef submitLimitOrder(Side side, double price, double qty){
        Order order{LimitOrderRef(next_order_id++), current_timestamp, side, OrderType::kLimit, price, qty, 0};
        auto trades = matching_engine.addLimitOrder(order);
        total_orders_placed++;
        // Обрабатываем полученные трейды (наши ордера, которые исполнились)
        for (const auto& inner_trade : trades) {
            executeInnerTrade(inner_trade);
        }
        return order.order_id;
    }
    OrderRef submitMarketOrder(Side side, double qty){
        Order order{MarketOrderRef{next_order_id++}, current_timestamp, side, OrderType::kMarket, 0/*не используется цена*/, qty};
        auto trades = matching_engine.addMarketOrder(order);
        total_orders_placed++;
        for (const auto& inner_trade : trades) {
            executeInnerTrade(inner_trade);
        }
        return order.order_id;
    }

    void cancelAll(){
        matching_engine.cancelAllOurOrders();
    }

    bool cancelOrder(OrderRef order_ref){
        return matching_engine.cancelOrder(order_ref);
    }
   

    // ==============================
    // ЗАГРУЗКА ДАННЫХ
    // ==============================

    void loadL2Data(const std::string& filename) {
        if(!use_external_history_manager){
            if(!history_manager){
                history_manager = std::make_shared<HistoryManager>();
            }
            history_manager->loadL2Data(filename);
        }
    }

    void loadTradeData(const std::string& filename) {
        if(!use_external_history_manager){
            if(!history_manager){
                history_manager = std::make_shared<HistoryManager>();
            }
            history_manager->loadTradeData(filename);
        }
    }

    // ==============================
    // ЛОГИКА ИСПОЛНЕНИЯ
    // ==============================
    std::optional<SimulationState> collectData(double ratio = 1.0) {
        if(!history_manager)
            return std::nullopt;
        ResetStateBacktester();
        size_t limit = static_cast<size_t>((history_manager->getL2Size() + history_manager->getTradeSize()) * ratio);
        return runEngine(true, false, 0, 0, limit);
    }

    bool runStrategy() {        
        if(!history_manager)
            return false;
        ResetStateBacktester();
        size_t limit = static_cast<size_t>((history_manager->getL2Size() + history_manager->getTradeSize()));
        runEngine(false, true, 0, 0, limit);
        return true;
    }

    bool runStrategy(const SimulationState& state) {
        if(!history_manager)
            return false;
        ResetStateBacktester();
        size_t limit = static_cast<size_t>((history_manager->getL2Size() + history_manager->getTradeSize()));
        runEngine(false, true, state.current_l2_idx, state.current_trade_idx, limit);
        return true;
    }

    double getCash() const { return cash; }
    double getInventory() const { return inventory; }
    double getEquity() const { return equity; }
    double getTurnover() const { return turnover; }
    const std::vector<Fill>& getFills() const { return fills; }
    int getTotalOrdersPlaced() const { return total_orders_placed; }
    int getTotalFills() const { return total_fills; }
    double getRealizedPnL() const { return realized_pnl; }
    double getUnrealizedPnL() const { return unrealized_pnl; }
    double getAvgEntryPrice() const { return avg_entry_price; }

    //для дебага
    const MatchingEngine& getMatchingEngine() const { return matching_engine; }

    void generateReport() const {
        if (portfolio_value.empty()) {
            std::cout << "No data to generate report.\n";
            return;
        }

        double initial_capital = 1000000.00;
        double final_equity = portfolio_value.back().second;
        double total_return = final_equity - initial_capital;
        double return_pct = (total_return / initial_capital) * 100.0;

        // Расчет максимальной просадки (Max Drawdown)
        double max_equity_so_far = -1e18;
        double max_drawdown = 0.0;
        for (const auto& p : portfolio_value) {
            max_equity_so_far = std::max(max_equity_so_far, p.second);
            double drawdown = max_equity_so_far - p.second;
            max_drawdown = std::max(max_drawdown, drawdown);
        }
        double mdd_pct = (max_equity_so_far > 0) ? (max_drawdown / max_equity_so_far) * 100.0 : 0.0;

        // Простая волатильность (стандартное отклонение)
        double sum_equity = 0.0;
        for (const auto& p : portfolio_value) sum_equity += p.second;
        double mean_equity = sum_equity / portfolio_value.size();

        double variance = 0.0;
        for (const auto& p : portfolio_value) {
            variance += std::pow(p.second - mean_equity, 2);
        }
        variance /= portfolio_value.size();
        double stddev = std::sqrt(variance);

        // Анализ инвентаря
        double max_inv = 0.0, min_inv = 0.0, avg_inv = 0.0;
        for (const auto& inv : inventory_history) {
            max_inv = std::max(max_inv, inv.second);
            min_inv = std::min(min_inv, inv.second);
            avg_inv += std::abs(inv.second);
        }
        avg_inv /= (inventory_history.size() > 0 ? inventory_history.size() : 1);

        // Статистика по заполнениям
        double buy_vol = 0, sell_vol = 0;
        int buy_count = 0, sell_count = 0;
        for (const auto& f : fills) {
            if (f.side == Side::kBid) { 
                buy_vol += f.amount; 
                buy_count++; 
            } else { 
                sell_vol += f.amount; 
                sell_count++; 
            }
        }

        // Вывод отчета
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\n============================================================\n";
        std::cout << "              HFT DETAILED BACKTEST REPORT                   \n";
        std::cout << "============================================================\n";

        // 1. ФИНАНСОВЫЕ ПОКАЗАТЕЛИ
        std::cout << "\n[1. FINANCIAL PERFORMANCE]\n";
        std::cout << "  Initial Capital:        $" << initial_capital << "\n";
        std::cout << "  Final Equity:           $" << final_equity << "\n";
        std::cout << "  Total Net Profit:       $" << (total_return >= 0 ? "+" : "") << total_return << "\n";
        std::cout << "  Total Return:           " << return_pct << "%\n";
        std::cout << "  Total Turnover:         $" << turnover << "\n";

        // 2. РИСК-МЕТРИКИ
        std::cout << "\n[2. RISK METRICS]\n";
        std::cout << "  Max Drawdown:           $" << max_drawdown << " (" << mdd_pct << "%)\n";
        std::cout << "  Equity Volatility:      $" << stddev << "\n";

        // 3. АНАЛИЗ ИНВЕНТАРЯ
        std::cout << "\n[3. INVENTORY ANALYSIS]\n";
        std::cout << "  Max Long Exposure:      " << max_inv << " units\n";
        std::cout << "  Max Short Exposure:     " << min_inv << " units\n";
        std::cout << "  Average Absolute Inv:   " << avg_inv << " units\n";

        // 4. АКТИВНОСТЬ И ИСПОЛНЕНИЕ
        std::cout << "\n[4. EXECUTION & ACTIVITY]\n";
        std::cout << "  Total Orders Placed:    " << total_orders_placed << "\n";
        std::cout << "  Total Fills:            " << total_fills << "\n";
        std::cout << "  Fill Rate:              " << (total_orders_placed > 0 ? (100.0 * total_fills / total_orders_placed) : 0.0) << "%\n";
        std::cout << "  Buy Fills / Sell Fills: " << buy_count << " / " << sell_count << "\n";
        std::cout << "  Buy Volume / Sell Vol:  " << buy_vol << " / " << sell_vol << "\n";
        std::cout << "  Avg Profit per Fill:    $" << (total_fills > 0 ? total_return / total_fills : 0.0) << "\n";

        // 5. ДЕТАЛИЗАЦИЯ PNL
        double total_pnl = realized_pnl + unrealized_pnl;
        std::cout << "\n[5. PNL BREAKDOWN]\n";
        std::cout << "  Realized PnL:           $" << realized_pnl << "\n";
        std::cout << "  Unrealized PnL:         $" << unrealized_pnl << "\n";
        std::cout << "  Total PnL:              $" << total_pnl << "\n";
        
        // 6. ДЕТАЛИ ПОЗИЦИИ (если есть открытая позиция)
        if (std::abs(inventory) > kVolEps) {
            std::cout << "\n[6. OPEN POSITION DETAILS]\n";
            std::cout << "  Position Size:         " << std::abs(inventory) << " units\n";
            std::cout << "  Direction:             " << (inventory > 0 ? "LONG" : "SHORT") << "\n";
            std::cout << "  Avg Entry Price:       $" << avg_entry_price << "\n";
            
            double best_bid = matching_engine.getBestBid();
            double best_ask = matching_engine.getBestAsk();
            if (best_bid > kPriceEps && best_ask > kPriceEps) {
                double current_price = (best_bid + best_ask) / 2.0;
                double unrealized_per_unit = (inventory > 0) ? (current_price - avg_entry_price) : (avg_entry_price - current_price);
                std::cout << "  Current Market Price:  $" << current_price << "\n";
                std::cout << "  Unrealized per unit:   $" << unrealized_per_unit << "\n";
            }
        }
        
        // 7. ТЕКУЩЕЕ СОСТОЯНИЕ
        std::cout << "\n[7. CURRENT STATE]\n";
        std::cout << "  Current Cash:           $" << cash << "\n";
        std::cout << "  Current Inventory:      " << inventory << " units\n";
        
        std::cout << "\n============================================================\n";
    }

private:
    SimulationState runEngine(bool collect, bool execute_strategy, size_t _current_l2_idx, size_t _current_trade_idx, size_t limit) {
         if(!history_manager)
            return {_current_l2_idx, _current_trade_idx, cash, inventory};
        auto current_l2_idx = _current_l2_idx;
        auto current_trade_idx = _current_trade_idx;

        auto max_l2_idx = history_manager->getL2Size();
        auto max_trade_idx = history_manager->getTradeSize();

        if (current_l2_idx >= max_l2_idx && current_trade_idx >= max_trade_idx) {
            return { current_l2_idx, current_trade_idx, cash, inventory };
        }

        // Основной цикл - пока есть хотя бы один источник данных
        while (current_l2_idx < max_l2_idx || current_trade_idx < max_trade_idx) {
            if((current_l2_idx + current_trade_idx) >= limit){
                break;
            }
            bool has_l2 = (current_l2_idx < max_l2_idx);
            bool has_trade = (current_trade_idx < max_trade_idx);
            
            // Определяем, какое событие имеет меньший timestamp
            bool process_l2 = false;
            
            if (has_l2 && has_trade) {
                uint64_t l2_ts = history_manager->getL2(current_l2_idx).timestamp;
                uint64_t tr_ts = history_manager->getTrade(current_trade_idx).timestamp;

                if (l2_ts <= tr_ts) {
                    process_l2 = true;
                } else {
                    process_l2 = false;
                }
            } else if (has_l2) {
                process_l2 = true;
            } else {
                process_l2 = false;
            }
            
            if (process_l2) {
                // Обработка L2 снапшота
                const auto& l2 = history_manager->getL2(current_l2_idx);
                current_timestamp = l2.timestamp;
                
                // Обновляем matching engine новым снапшотом
                // Копируем asks и bids в векторы
                std::vector<std::pair<double, double>> asks;
                std::vector<std::pair<double, double>> bids;
                
                for (const auto& ask : l2.asks) {
                    asks.emplace_back(ask.first, ask.second);
                }
                for (const auto& bid : l2.bids) {
                    bids.emplace_back(bid.first, bid.second);
                }
                
                matching_engine.addSnapshot(std::move(asks), std::move(bids));
                
                // Уведомляем стратегию и коллектор
                if (collect || execute_strategy) {
                     auto l2 = matching_engine.fillL2Snapshot();
                    // Создаем контекст с текущим состоянием
                    MarketContext ctx{std::move(l2), inventory, cash, start_timestamp, current_timestamp };
                  
                    if (collect && data_collector) {
                        data_collector->onMarketContext(ctx);
                    }
                    if (execute_strategy && strategy) {
                        strategy->onMarketContext(ctx, this);
                    }
                }
                
                updatePortfolio();
                
                current_l2_idx++;
            } 
            else {
                // Обработка внешнего трейда
                const auto& trade = history_manager->getTrade(current_trade_idx);
                current_timestamp = trade.timestamp;
                
                // Создаем рыночный ордер из внешнего трейда
                Order externalMarket;
                externalMarket.order_id = ExternalOrderRef{static_cast<int64_t>(trade.row)};
                externalMarket.timestamp = trade.timestamp;
                externalMarket.side = (trade.side == TradeSide::kBuy) ? Side::kBid : Side::kAsk;
                externalMarket.type = OrderType::kMarket;
                externalMarket.amount = trade.amount;
                externalMarket.price = trade.price;
                externalMarket.filled_amount = 0;
                
                // Добавляем в matching engine как рыночный ордер
                auto trades = matching_engine.addMarketOrder(externalMarket);
                
                // Обрабатываем полученные трейды (наши ордера, которые исполнились)
                for (const auto& inner_trade : trades) {
                    executeInnerTrade(inner_trade);
                }
                
                // Уведомляем стратегию и коллектор
                if (collect || execute_strategy) {

                    auto l2 = matching_engine.fillL2Snapshot();
                    // Создаем контекст с текущим состоянием
                    //MarketContext ctx{std::move(l2), inventory, cash, start_timestamp, current_timestamp };?

                    
                    if (collect && data_collector) {
                        //data_collector->onMarketContext(ctx);?
                    }
                    if (execute_strategy && strategy) {
                        //strategy->onMarketContext(ctx, this);?
                    }
                }
                updatePortfolio();
                current_trade_idx++;
            }
        }

        return { 
            current_l2_idx, 
            current_trade_idx, 
            cash, 
            inventory 
        };
    }

    void ResetInventoryHistory(){
        inventory_history.clear();
    }

    void ResetPnlHistory(){
        portfolio_value.clear();
    }

    void ResetToDefaultConfig(){
        inventory = config.inventory;
        cash = config.cash;
        turnover = config.turnover;
    }

    void ResetStateBacktester(){
        current_l2 = {};
        start_timestamp = 0;
        current_timestamp = 0;
        matching_engine.clear();
        active_orders.clear();
        fills.clear();
        total_orders_placed = 0;
        total_fills = 0;
        inventory = config.inventory;
        cash = config.cash;
        turnover = config.turnover;
        inventory_history.clear();
        portfolio_value.clear();

        // Сброс PnL переменных
        realized_pnl = 0.0;
        unrealized_pnl = 0.0;
        avg_entry_price = 0.0;
        total_buy_qty = 0.0;
        total_buy_value = 0.0;
        total_sell_qty = 0.0;
        total_sell_value = 0.0;
    }

    // Универсальный метод executeInnerTrade:
    void executeInnerTrade(const InnerTrade& trade) {
        auto role = trade.getOurRole();
        
        if (role == InnerTrade::OurRole::kAggressor || role == InnerTrade::OurRole::kPassive) {
            Fill fill;
            fill.timestamp = trade.timestamp;
            fill.price = trade.price;
            fill.amount = trade.amount;
            fill.order_id = trade.getOurOrderId();
            
            double trade_pnl = 0.0;
            bool is_buy = determineIfWeAreBuying(trade, role);
            
            if (is_buy) {
                // МЫ ПОКУПАЕМ
                fill.side = Side::kBid;
                
                // Если есть открытая позиция на продажу (короткая), закрываем её
                if (inventory < -kVolEps) {
                    double close_amount = std::min(trade.amount, -inventory);
                    double open_amount = trade.amount - close_amount;
                    
                    // PnL от закрытия короткой позиции
                    trade_pnl = (avg_entry_price - trade.price) * close_amount;
                    realized_pnl += trade_pnl;
                    
                    // Обновляем статистику
                    total_sell_qty -= close_amount;
                    total_sell_value -= avg_entry_price * close_amount;
                    inventory += close_amount;
                    
                    // Если осталось, открываем новую длинную позицию
                    if (open_amount > kVolEps) {
                        inventory += open_amount;
                        total_buy_qty += open_amount;
                        total_buy_value += trade.price * open_amount;
                    }
                } 
                else {
                    // Просто увеличиваем длинную позицию
                    inventory += trade.amount;
                    total_buy_qty += trade.amount;
                    total_buy_value += trade.price * trade.amount;
                    trade_pnl = 0;
                }
                
                cash -= trade.amount * trade.price;
                
            } else {
                // МЫ ПРОДАЕМ
                fill.side = Side::kAsk;
                
                // Если есть открытая позиция на покупку (длинная), закрываем её
                if (inventory > kVolEps) {
                    double close_amount = std::min(trade.amount, inventory);
                    double open_amount = trade.amount - close_amount;
                    
                    // PnL от закрытия длинной позиции
                    trade_pnl = (trade.price - avg_entry_price) * close_amount;
                    realized_pnl += trade_pnl;
                    
                    // Обновляем статистику
                    total_buy_qty -= close_amount;
                    total_buy_value -= avg_entry_price * close_amount;
                    inventory -= close_amount;
                    
                    // Если осталось, открываем новую короткую позицию
                    if (open_amount > kVolEps) {
                        inventory -= open_amount;
                        total_sell_qty += open_amount;
                        total_sell_value += trade.price * open_amount;
                    }
                }
                else {
                    // Просто увеличиваем короткую позицию
                    inventory -= trade.amount;
                    total_sell_qty += trade.amount;
                    total_sell_value += trade.price * trade.amount;
                    trade_pnl = 0;
                }
                
                cash += trade.amount * trade.price;
            }
            
            // Обновляем среднюю цену входа
            updateAverageEntryPrice();
            
            fills.push_back(fill);
            turnover += trade.amount * trade.price;
            total_fills++;
            
            if (strategy) strategy->onFill(fill);
            if (data_collector) data_collector->onFill(fill);
            
        }
    }

    // Вспомогательный метод для определения направления
    bool determineIfWeAreBuying(const InnerTrade& trade, InnerTrade::OurRole role) {
        if (role == InnerTrade::OurRole::kAggressor) {
            return (trade.side == TradeSide::kBuy);
        } else {
            return (trade.side == TradeSide::kSell);
        }
    }

    // Обновление средней цены входа
    void updateAverageEntryPrice() {
        if (std::abs(inventory) < kVolEps) {
            avg_entry_price = 0.0;
            return;
        }
        
        if (inventory > 0) {
            // Длинная позиция
            avg_entry_price = (total_buy_qty > kVolEps) ? total_buy_value / total_buy_qty : 0.0;
        } else {
            // Короткая позиция
            avg_entry_price = (total_sell_qty > kVolEps) ? total_sell_value / total_sell_qty : 0.0;
        }
    }

    void updatePortfolio() {
        double best_bid = matching_engine.getBestBid();
        double best_ask = matching_engine.getBestAsk();
        
        if (best_bid < kPriceEps || best_ask < kPriceEps) {
            return;
        }
        
        double current_price = (best_bid + best_ask) / 2.0;
        
        // Расчет нереализованной PnL
        if (std::abs(inventory) > kVolEps) {
            if (inventory > 0) {
                // Длинная позиция
                unrealized_pnl = (current_price - avg_entry_price) * inventory;
            } else {
                // Короткая позиция
                unrealized_pnl = (avg_entry_price - current_price) * (-inventory);
            }
        } else {
            unrealized_pnl = 0.0;
        }
        
        // Общая PnL (реализованная + нереализованная)
        total_profit = realized_pnl + unrealized_pnl;
        
        // Для спота: total_pnl = cash - initial_cash + inventory * current_price
        // Для фьючерсов: total_pnl = realized_pnl + unrealized_pnl
        equity = config.cash + total_profit;

        // Сохраняем историю
        portfolio_value.push_back({current_timestamp, equity});
        inventory_history.push_back({current_timestamp, inventory});
    }

    void updateAvgEntryPrice() {
        if (std::abs(inventory) < kVolEps) {
            avg_entry_price = 0.0;
            return;
        }
        
        if (inventory > 0) {
            // Длинная позиция: средняя цена НЕЗАКРЫТЫХ покупок
            // total_buy_qty уменьшается при продажах (FIFO)
            if (total_buy_qty > kVolEps && total_buy_qty >= inventory) {
                avg_entry_price = total_buy_value / total_buy_qty;
            }
        } 
        else if (inventory < -kVolEps) {
            // Короткая позиция: средняя цена НЕЗАКРЫТЫХ продаж
            // total_sell_qty уменьшается при покупках (FIFO)
            if (total_sell_qty > kVolEps && total_sell_qty >= -inventory) {
                avg_entry_price = total_sell_value / total_sell_qty;
            }
        }
    }
};