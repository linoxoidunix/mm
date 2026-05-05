#pragma once

#include "mm/type/market_context.h"
#include "mm/strategy/i_strategy.h"
#include "cmath"
#include "mm/hftbacktester/hftbacktester.h"
#include "mm/strategy/avelaneda_stoikov/config.h"
#include "mm/constants/constants.h"
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>

class AvellanedaStoikov1 : public IStrategy {
private:
    ASParams params;
    double order_size;
    double last_bid_quote = 0;
    double last_ask_quote = 0;
    
    // Логирование в файл
    mutable std::ofstream log_file;
    mutable bool log_initialized = false;
     mutable int log_counter = 0;
    const int LOG_INTERVAL = 100;  // Логировать каждые 100 тиков
    
    void initLog() const {
        if (!log_initialized) {
            // Создаем файл с timestamp
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::tm* tm = std::localtime(&time_t);
            
            char filename[256];
            std::strftime(filename, sizeof(filename), "avelaneda_log_%Y%m%d_%H%M%S.csv", tm);
            
            log_file.open(filename);
            if (log_file.is_open()) {
                // Заголовки CSV
                log_file << "timestamp,inv,best_bid,best_ask,market_mid,skew,reservation_price,"
                        << "spread,spread_ticks,bid_quote,ask_quote,bid_dist,ask_dist,bid_send,ask_send\n";
            }
            log_initialized = true;
        }
    }

    double computeMicroprice(const MEL2Snapshot& snap) const {
        if (snap.bids.empty() || snap.asks.empty()) return 0.0;
        
        double best_bid = snap.bids[0].first;
        double best_ask = snap.asks[0].first;
        
        double bid_volume = snap.bids[0].second;
        double ask_volume = snap.asks[0].second;
        
        double total_volume = bid_volume + ask_volume;
        if (total_volume < kVolEps) return (best_bid + best_ask) / 2.0;
        
        return (best_bid * ask_volume + best_ask * bid_volume) / total_volume;
    }

public:
    AvellanedaStoikov1(ASParams p, double size) : params(p), order_size(size) {}
    
    ~AvellanedaStoikov1() {
        if (log_file.is_open()) {
            log_file.close();
        }
    }

    void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
        if (ctx.l2.bids.empty() || ctx.l2.asks.empty()) return;
        
        initLog();

        double tick_size = 0.0000001;
        double best_bid = ctx.l2.bids[0].first;
        double best_ask = ctx.l2.asks[0].first;
        double market_mid = (best_bid + best_ask) / 2.0;
        
        // Для крипты игнорируем время
        double time_factor = 1.0;
        
        // Сдвиг из-за инвентаря (skew)
        double skew = -ctx.inventory * params.gamma * std::pow(params.sigma, 2) * time_factor;
        
        // Ограничиваем skew
        double max_skew_ticks = 100.0;
        double max_skew = max_skew_ticks * tick_size;
        skew = std::clamp(skew, -max_skew, max_skew);
        
        // Резервационная цена
        double reservation_price = market_mid + skew;
        
        // Оптимальный спред
        double spread = (2.0 / params.gamma) * std::log(1.0 + params.gamma / params.kappa);
        
        // Минимальный спред
        double min_spread = (best_ask - best_bid) + 2 * tick_size;  // рыночный спред + 2 тика
        spread = std::max(spread, min_spread);
        
        // Котировки
        double bid_quote = reservation_price - spread / 2.0;
        double ask_quote = reservation_price + spread / 2.0;
        
        // Округление до тика
        bid_quote = std::floor(bid_quote / tick_size + kPriceEps) * tick_size;
        ask_quote = std::ceil(ask_quote / tick_size - kPriceEps) * tick_size;
        
        // Корректировка
        bid_quote = std::min(bid_quote, best_ask - tick_size);
        ask_quote = std::max(ask_quote, best_bid + tick_size);
        
        // Проверка возможности отправки
        bool bid_send = (bid_quote < best_ask - tick_size && ctx.inventory < params.max_inventory);
        bool ask_send = (ask_quote > best_bid + tick_size && ctx.inventory > -params.max_inventory);
        
        // Логирование в файл (каждый тик)
        double calc_spread_ticks = spread / tick_size;
        double bid_dist = (best_bid - bid_quote) / tick_size;
        double ask_dist = (ask_quote - best_ask) / tick_size;
        
        // В onMarketContext:
        // log_counter++;
        // if (log_counter % LOG_INTERVAL == 0) {
        //     log_file << ctx.current_timestamp << ","
        //             << ctx.inventory << ","
        //             << best_bid << ","
        //             << best_ask << ","
        //             << market_mid << ","
        //             << skew << ","
        //             << reservation_price << ","
        //             << spread << ","
        //             << calc_spread_ticks << ","
        //             << bid_quote << ","
        //             << ask_quote << ","
        //             << bid_dist << ","
        //             << ask_dist << ","
        //             << (bid_send ? "YES" : "NO") << ","
        //             << (ask_send ? "YES" : "NO") << "\n";
        // }
        
        // Отмена старых ордеров
        api->cancelAll();
        
        // Выставление новых ордеров
        if (bid_send) {
            api->submitLimitOrder(Side::kBid, bid_quote, order_size);
        }
        
        if (ask_send) {
            api->submitLimitOrder(Side::kAsk, ask_quote, order_size);
        }
    }
};