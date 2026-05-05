#pragma once

#include "mm/type/market_context.h"
#include "mm/strategy/i_strategy.h"
#include "cmath"
#include "mm/hftbacktester/hftbacktester.h"
#include "mm/strategy/avelaneda_stoikov/config.h"
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>

class AvellanedaStoikovClassicGmn : public IStrategy {
private:
    ASParams params;
    double order_size;
    
    mutable std::ofstream log_file;
    mutable bool log_initialized = false;
    mutable int log_counter = 0;
    const int LOG_INTERVAL = 100;
    
    // void initLog() const {
    //     if (!log_initialized) {
    //         auto now = std::chrono::system_clock::now();
    //         auto time_t = std::chrono::system_clock::to_time_t(now);
    //         std::tm* tm = std::localtime(&time_t);
    //         char filename[256];
    //         std::strftime(filename, sizeof(filename), "classic_as_log_%Y%m%d_%H%M%S.csv", tm);
            
    //         log_file.open(filename);
    //         if (log_file.is_open()) {
    //             log_file << "timestamp,inv,best_bid,best_ask,market_mid,inventory_bias,reservation_price,"
    //                     << "spread,spread_ticks,bid_quote,ask_quote,bid_dist,ask_dist,bid_send,ask_send\n";
    //         }
    //         log_initialized = true;
    //     }
    // }

public:
    AvellanedaStoikovClassicGmn(ASParams p, double size) : params(p), order_size(size) {}
    
    ~AvellanedaStoikovClassicGmn() {
        if (log_file.is_open()) log_file.close();
    }

    void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
        if (ctx.l2.bids.empty() || ctx.l2.asks.empty()) return;
        //initLog();

        const double tick_size = 0.0000001; 
        const double best_bid = ctx.l2.bids[0].first;
        const double best_ask = ctx.l2.asks[0].first;
        
        // --- ОТЛИЧИЕ ОТ MICROPRICE ВЕРСИИ ---
        // 1. Используем обычный Mid-price вместо Microprice
        double mid_price = (best_bid + best_ask) / 2.0;
        
        // 2. Расчет Reservation Price (r)
        // r = s - q * gamma * sigma^2 * (T-t)
        // Здесь (T-t) обычно принимается за 1 или сокращается в бесконечном горизонте
        double inventory_bias = ctx.inventory * params.gamma * std::pow(params.sigma, 2);
        double reservation_price = mid_price - inventory_bias; 

        // 3. Расчет оптимального спреда
        // s = (2/gamma) * ln(1 + gamma/kappa)
        double optimal_spread = (2.0 / params.gamma) * std::log(1.0 + params.gamma / params.kappa);
        optimal_spread = std::max(optimal_spread, 2.0 * tick_size); 

        // 4. Определение котировок относительно Reservation Price
        double raw_bid = reservation_price - (optimal_spread / 2.0);
        double raw_ask = reservation_price + (optimal_spread / 2.0);

        // 5. Округление и Execution Policy (Passive Only)
        double bid_quote = std::floor(raw_bid / tick_size) * tick_size;
        double ask_quote = std::ceil(raw_ask / tick_size) * tick_size;

        // Не позволяем себе стать тейкером
        bid_quote = std::min(bid_quote, best_bid); 
        ask_quote = std::max(ask_quote, best_ask);

        // 6. Проверки риск-менеджмента
        bool can_buy = (ctx.inventory < params.max_inventory);
        bool can_sell = (ctx.inventory > -params.max_inventory);
        bool bid_valid = (bid_quote > 0) && (bid_quote < best_ask);
        bool ask_valid = (ask_quote > best_bid);

        // Логирование
        // if (++log_counter % LOG_INTERVAL == 0 && log_file.is_open()) {
        //     log_file << ctx.current_timestamp << ","
        //             << ctx.inventory << ","
        //             << best_bid << "," << best_ask << ","
        //             << mid_price << ","
        //             << inventory_bias << ","
        //             << reservation_price << ","
        //             << optimal_spread << ","
        //             << (optimal_spread / tick_size) << ","
        //             << bid_quote << "," << ask_quote << ","
        //             << (best_bid - bid_quote) / tick_size << ","
        //             << (ask_quote - best_ask) / tick_size << ","
        //             << (can_buy && bid_valid ? "YES" : "NO") << ","
        //             << (can_sell && ask_valid ? "YES" : "NO") << "\n";
        // }

        // 7. Работа с ордерами
        api->cancelAll();

        if (can_buy && bid_valid) {
            api->submitLimitOrder(Side::kBid, bid_quote, order_size);
        }
        
        if (can_sell && ask_valid) {
            api->submitLimitOrder(Side::kAsk, ask_quote, order_size);
        }
    }
};