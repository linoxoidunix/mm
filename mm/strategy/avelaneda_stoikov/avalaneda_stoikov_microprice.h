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

class AvellanedaStoikovMicroPrice : public IStrategy {
private:
    ASParams params;
    double order_size;
    bool use_logging = false;
    double last_bid_quote = 0;
    double last_ask_quote = 0;
    
    mutable std::ofstream log_file;
    mutable bool log_initialized = false;
    mutable int log_counter = 0;
    const int LOG_INTERVAL = 100;
    
    void initLog() const {
        if (!log_initialized) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::tm* tm = std::localtime(&time_t);
            
            char filename[256];
            std::strftime(filename, sizeof(filename), "avelaneda_log_%Y%m%d_%H%M%S.csv", tm);
            
            log_file.open(filename);
            if (log_file.is_open()) {
                log_file << "timestamp,inv,best_bid,best_ask,market_mid,microprice,skew,reservation_price,"
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
    AvellanedaStoikovMicroPrice(ASParams p, double size, bool log_enabled = false) : 
    params(p),
    order_size(size),
    use_logging(log_enabled) {}
    
    ~AvellanedaStoikovMicroPrice() {
        if (log_file.is_open()) {
            log_file.close();
        }
    }

    // void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
    //     if (ctx.l2.bids.empty() || ctx.l2.asks.empty()) return;
        
    //     if (use_logging) initLog();

    //     double tick_size = 0.0000001;
    //     double best_bid = ctx.l2.bids[0].first;
    //     double best_ask = ctx.l2.asks[0].first;
    //     double market_mid = (best_bid + best_ask) / 2.0;
        
    //     double microprice = computeMicroprice(ctx.l2);
        
    //     double time_factor = 1.0;
        
    //     // Сдвиг из-за инвентаря
    //     double skew = -ctx.inventory * params.gamma * std::pow(params.sigma, 2) * time_factor;
        
    //     // // Ограничиваем skew (максимум 50 тиков)
    //     // double max_skew_ticks = 50.0;
    //     // double max_skew = max_skew_ticks * tick_size;
    //     // skew = std::clamp(skew, -max_skew, max_skew);
        
    //     // Резервационная цена
    //     double reservation_price = microprice + skew;
        
    //     // Оптимальный спред
    //     double spread = (2.0 / params.gamma) * std::log(1.0 + params.gamma / params.kappa);
        
    //     // Минимальный спред - НЕ МЕНЕЕ 2 тиков
    //     double min_spread = 2.0 * tick_size;
    //     spread = std::max(spread, min_spread);
        
    //     // Базовые котировки
    //     double bid_quote = reservation_price - spread / 2.0;
    //     double ask_quote = reservation_price + spread / 2.0;
        
    //     // Округление до тика
    //     bid_quote = std::floor(bid_quote / tick_size + 1e-9) * tick_size;
    //     ask_quote = std::ceil(ask_quote / tick_size - 1e-9) * tick_size;
        
    //     // ============================================================
    //     // КЛЮЧЕВОЕ ИСПРАВЛЕНИЕ: гарантируем, что bid НЕ ВЫШЕ best_bid
    //     // ============================================================
        
    //     // 1. Сначала корректируем bid и ask относительно рынка
    //     bid_quote = std::min(bid_quote, best_bid);           // bid не выше лучшего бида
    //     ask_quote = std::max(ask_quote, best_ask);           // ask не ниже лучшего аска
        
    //     // 2. Затем проверяем, что не пересекаем рынок
    //     double max_bid = best_ask - tick_size;               // максимально возможный bid
    //     double min_ask = best_bid + tick_size;               // минимально возможный ask
        
    //     bid_quote = std::min(bid_quote, max_bid);
    //     ask_quote = std::max(ask_quote, min_ask);
        
    //     // 3. Проверка, что котировки имеют смысл
    //     if (bid_quote >= ask_quote - tick_size) {
    //         // Спред схлопнулся - раздвигаем
    //         double mid = (best_bid + best_ask) / 2.0;
    //         bid_quote = mid - spread / 2.0;
    //         ask_quote = mid + spread / 2.0;
    //         bid_quote = std::floor(bid_quote / tick_size + 1e-9) * tick_size;
    //         ask_quote = std::ceil(ask_quote / tick_size - 1e-9) * tick_size;
    //         bid_quote = std::min(bid_quote, best_bid);
    //         ask_quote = std::max(ask_quote, best_ask);
    //         bid_quote = std::min(bid_quote, max_bid);
    //         ask_quote = std::max(ask_quote, min_ask);
    //     }
        
    //     // 4. Дополнительный отступ, если bid все еще слишком высок
    //     if (bid_quote > best_bid - tick_size) {
    //         bid_quote = best_bid - tick_size;
    //     }
    //     if (ask_quote < best_ask + tick_size) {
    //         ask_quote = best_ask + tick_size;
    //     }
        
    //     // Округляем еще раз после всех корректировок
    //     bid_quote = std::floor(bid_quote / tick_size + 1e-9) * tick_size;
    //     ask_quote = std::ceil(ask_quote / tick_size - 1e-9) * tick_size;
        
    //     // Проверка возможности отправки
    //     bool bid_send = (bid_quote < best_ask - tick_size && 
    //                      bid_quote < best_bid && 
    //                      ctx.inventory < params.max_inventory);
        
    //     bool ask_send = (ask_quote > best_bid + tick_size && 
    //                      ask_quote > best_ask && 
    //                      ctx.inventory > -params.max_inventory);
        
    //     // Логирование
    //     double calc_spread_ticks = spread / tick_size;
    //     double bid_dist = (best_bid - bid_quote) / tick_size;
    //     double ask_dist = (ask_quote - best_ask) / tick_size;
        
    //     log_counter++;
    //     // Логирование
    //     if (use_logging && ++log_counter % LOG_INTERVAL == 0 && log_file.is_open()) {
    //         log_file << ctx.current_timestamp << ","
    //                 << ctx.inventory << ","
    //                 << best_bid << ","
    //                 << best_ask << ","
    //                 << market_mid << ","
    //                 << microprice << ","
    //                 << skew << ","
    //                 << reservation_price << ","
    //                 << spread << ","
    //                 << calc_spread_ticks << ","
    //                 << bid_quote << ","
    //                 << ask_quote << ","
    //                 << bid_dist << ","
    //                 << ask_dist << ","
    //                 << (bid_send ? "YES" : "NO") << ","
    //                 << (ask_send ? "YES" : "NO") << "\n";
    //         log_file.flush();
    //     }
        
    //     api->cancelAll();
        
    //     if (bid_send) {
    //         api->submitLimitOrder(Side::kBid, bid_quote, order_size);
    //     }
        
    //     if (ask_send) {
    //         api->submitLimitOrder(Side::kAsk, ask_quote, order_size);
    //     }
    // }
    void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
        if (ctx.l2.bids.empty() || ctx.l2.asks.empty()) return;
        
        if (use_logging) initLog();

        const double tick_size = 0.0000001;
        const double best_bid = ctx.l2.bids[0].first;
        const double best_ask = ctx.l2.asks[0].first;
        
        // 1. Справедливая цена на основе дисбаланса объемов
        double microprice = computeMicroprice(ctx.l2);
        
        // 2. Сдвиг центра спреда (Reservation Price) на основе инвентаря
        // r = microprice - q * gamma * sigma^2
        double skew = -ctx.inventory * params.gamma * std::pow(params.sigma, 2);
        double reservation_price = microprice + skew;
        
        // 3. Расчет оптимального спреда
        // s = (2/gamma) * ln(1 + gamma/kappa)
        double spread = (2.0 / params.gamma) * std::log(1.0 + params.gamma / params.kappa);
        
        // Гарантируем минимальный спред в 2 тика для предотвращения self-trade
        spread = std::max(spread, 2.0 * tick_size);
        
        // 4. Формируем котировки вокруг reservation_price
        double bid_quote = reservation_price - spread / 2.0;
        double ask_quote = reservation_price + spread / 2.0;
        
        // 5. Округление до ближайшего тика (Bid вниз, Ask вверх для расширения спреда)
        bid_quote = std::floor(bid_quote / tick_size + kPriceEps) * tick_size;
        ask_quote = std::ceil(ask_quote / tick_size - kPriceEps) * tick_size;
        
        // 6. Passive Only: гарантируем, что мы не перебиваем лучшие цены и не бьем в спред
        // Мы хотим стоять в стакане, а не забирать ликвидность
        bid_quote = std::min(bid_quote, best_bid);
        ask_quote = std::max(ask_quote, best_ask);

        // 7. Проверка лимитов инвентаря и валидности цен
        bool bid_send = (ctx.inventory < params.max_inventory);
        bool ask_send = (ctx.inventory > -params.max_inventory);
        
        // Дополнительная защита: цена покупки всегда ниже цены продажи
        if (bid_quote >= ask_quote) {
            bid_send = false;
            ask_send = false;
        }

        // Логирование
        if (use_logging && ++log_counter % LOG_INTERVAL == 0 && log_file.is_open()) {
            log_file << ctx.current_timestamp << ","
                    << ctx.inventory << ","
                    << best_bid << ","
                    << best_ask << ","
                    << (best_bid + best_ask) / 2.0 << ","
                    << microprice << ","
                    << skew << ","
                    << reservation_price << ","
                    << spread << ","
                    << (spread / tick_size) << ","
                    << bid_quote << ","
                    << ask_quote << ","
                    << (best_bid - bid_quote) / tick_size << ","
                    << (ask_quote - best_ask) / tick_size << ","
                    << (bid_send ? "YES" : "NO") << ","
                    << (ask_send ? "YES" : "NO") << "\n";
            log_file.flush();
        }
        
        api->cancelAll();
        
        if (bid_send) {
            api->submitLimitOrder(Side::kBid, bid_quote, order_size);
        }
        
        if (ask_send) {
            api->submitLimitOrder(Side::kAsk, ask_quote, order_size);
        }
    }
};