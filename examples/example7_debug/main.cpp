// hft_backtester_complete.cpp
// Complete HFT Backtester for CMF Entrance Exam
// Implements Avellaneda-Stoikov (2008) with microprice extension (2018)

#include <iostream>
#include <filesystem>
#include "mm/hftbacktester/hftbacktester.h"
#include "mm/strategy/avelaneda_stoikov/avalaneda_stoikov_microprice.h"
#include "mm/history_manager/history_manager.h"
#include "mm/strategy/avelaneda_stoikov/config.h"

void createTestL2File(const std::string& filename, 
                       const std::vector<std::tuple<int64_t, 
                       std::vector<std::pair<double, double>>, 
                       std::vector<std::pair<double, double>>>>& snapshots,
                       int max_depth = 0) { // 0 означает "автоматически по максимальному"
    
    // 1. Если max_depth не задан, находим самый глубокий стакан в данных
    if (max_depth == 0) {
        for (const auto& [ts, asks, bids] : snapshots) {
            max_depth = std::max({max_depth, (int)asks.size(), (int)bids.size()});
        }
    }

    std::ofstream file(filename);
    
    // 2. Генерируем заголовок динамически
    file << "row,timestamp";
    for (int i = 0; i < max_depth; i++) {
        file << ",ask_price" << i << ",ask_vol" << i 
             << ",bid_price" << i << ",bid_vol" << i;
    }
    file << "\n";
    
    // 3. Заполняем данными
    for (size_t idx = 0; idx < snapshots.size(); ++idx) {
        const auto& [ts, asks, bids] = snapshots[idx];
        file << idx + 1 << "," << ts;
        
        for (int i = 0; i < max_depth; i++) {
            // Данные асков
            if (i < (int)asks.size()) {
                file << "," << asks[i].first << "," << asks[i].second;
            } else {
                file << ",0.0,0.0"; // Добиваем пустоту нулями
            }

            // Данные бидов
            if (i < (int)bids.size()) {
                file << "," << bids[i].first << "," << bids[i].second;
            } else {
                file << ",0.0,0.0";
            }
        }
        file << "\n";
    }
    file.close();
}

void createTestTradeFile(const std::string& filename, 
                         const std::vector<std::tuple<int64_t, TradeSide, double, double>>& trades) {
    std::ofstream file(filename);
    file << "row,timestamp,side,price,amount\n";
    for (size_t i = 0; i < trades.size(); ++i) {
        auto [ts, side, price, amount] = trades[i];
        file << i + 1 << "," << ts << ",";
        file << (side == TradeSide::kBuy ? "buy" : "sell") << ",";
        file << price << "," << amount << "\n";
    }
    file.close();
}

 class BuySellStrategy : public IStrategy {
    public:
        enum State { INIT, BOUGHT, DONE } state = INIT;
        double tick_size = 0.1;
        
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
            if (state == INIT && !ctx.l2.bids.empty()) {
                // Ставим лимитку на покупку лучше рынка: 100.0 + 0.1 = 100.1
                double buy_price = ctx.l2.bids[0].first + tick_size;
                std::cout << "TS: " << ctx.current_timestamp << " | Sending LIMIT BUY: " << buy_price << std::endl;
                api->submitLimitOrder(Side::kBid, buy_price, 50);
                state = BOUGHT;
            }
            else if (state == BOUGHT && ctx.inventory >= 50.0 && !ctx.l2.asks.empty()) {
                // После покупки ставим лимитку на продажу лучше рынка: 110.0 - 0.1 = 109.9
                double sell_price = ctx.l2.asks[0].first - tick_size;
                std::cout << "TS: " << ctx.current_timestamp << " | Sending LIMIT SELL: " << sell_price << std::endl;
                api->submitLimitOrder(Side::kAsk, sell_price, 50);
                state = DONE;
            }
        }
        
        void onFill(const Fill& fill) override {
            std::cout << "  >>> FILL: " << fill.amount << " @ " << fill.price 
                      << " " << (fill.side == Side::kBid ? "BUY" : "SELL") << std::endl;
        }
    };

// ==============================
// Main Function
// ==============================

// int main(int argc, char* argv[]) {
//      std::cout << "\n=== Test 4: Buy and Sell Cycle (Tick 0.1) ===" << std::endl;
    
   
    
//     auto hm = std::make_shared<HistoryManager>();
    
//     // Снапшоты: {timestamp, asks, bids}
//     std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
//         {1000, {{105.0, 1000}}, {{100.0, 1000}}}, // ТУТ: поставим Bid 100.1
//         {3000, {{110.0, 1000}}, {{105.0, 1000}}}  // ТУТ: уже купили, ставим Ask 109.9
//     };
//     createTestL2File("test_cycle_l2.csv", snapshots);
//     hm->loadL2Data("test_cycle_l2.csv");
    
//     // Трейды для исполнения наших ордеров
//     std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
//         {2000, TradeSide::kSell, 100.1, 50}, // Исполняет покупку по 100.1
//         {4000, TradeSide::kBuy, 109.9, 50}   // Исполняет продажу по 109.9
//     };
//     createTestTradeFile("test_cycle_trade.csv", trades);
//     hm->loadTradeData("test_cycle_trade.csv");
    
//     HFTBacktesterConfig config;
//     config.cash = 1000000.0;
//     config.inventory = 0.0;
//     HFTBacktester backtester(std::move(config), hm);
    
//     backtester.setStrategy(std::make_unique<BuySellStrategy>());
//     backtester.runStrategy();
    
//     // === ТОЧНЫЙ АНАЛИТИЧЕСКИЙ РАСЧЕТ ===
    
//     // 1. Покупка: 50 @ 100.1 = 5005.0 (Cash становится 994995.0)
//     // 2. Продажа: 50 @ 109.9 = 5495.0 (Cash становится 994995.0 + 5495.0 = 1000490.0)
    
//     double expected_cash = 1000490.0;
//     double expected_inv = 0.0;
    
//     // Поскольку инвентарь 0, PnL обязан быть равен Cash
//     double expected_pnl = expected_cash;
    
//     // Проверка логики прибыли: (109.9 - 100.1) * 50 = 9.8 * 50 = 490.0
    
//     std::cout << "\n=== FINAL CHECKS ===" << std::endl;
//     std::cout << std::fixed << std::setprecision(2);
//     std::cout << "Final Cash: " << backtester.getCash() << std::endl;
//     std::cout << "Final PnL:  " << backtester.getEquity() << std::endl;
    
//     // EXPECT_DOUBLE_EQ(backtester.getInventory(), expected_inv);
//     // EXPECT_NEAR(backtester.getCash(), expected_cash, 1e-7);
//     // EXPECT_NEAR(backtester.getEquity(), expected_pnl, 1e-7);
//     // EXPECT_EQ(backtester.getTotalFills(), 2);
    
//     std::filesystem::remove("test_cycle_l2.csv");
//     std::filesystem::remove("test_cycle_trade.csv");
    
//     return 0;
// }

int main(int argc, char* argv[]) {
    // Стратегия для продажи с отменой
    class SimpleSellCancelStrategy : public IStrategy {
    public:
        bool order_sent = false;
        bool cancel_done = false;
        
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
            if (!order_sent && !ctx.l2.bids.empty()) {
                api->submitLimitOrder(Side::kAsk, 100.0, 100);
                order_sent = true;
                std::cout << "SELL order sent at " << ctx.current_timestamp << std::endl;
            }
            
            if (order_sent && !cancel_done && ctx.current_timestamp > 2000) {
                api->cancelAll();
                cancel_done = true;
                std::cout << "Order cancelled at " << ctx.current_timestamp << std::endl;
            }
        }
    };
    
    std::cout << "\n=== Test SellWithCancelAfterPartialFill START ===" << std::endl;
    
    HFTBacktesterConfig config;
    config.cash = 1000000.0;
    config.inventory = 100.0;  // Начинаем с 100 единиц
    HFTBacktester backtester(std::move(config));
    
    std::cout << "Initial state: cash=" << backtester.getCash() 
              << ", inventory=" << backtester.getInventory() << std::endl;
    
    auto strategy = std::make_unique<SimpleSellCancelStrategy>();
    backtester.setStrategy(std::move(strategy));
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{101.0, 1000}}, {{100.0, 30}}},
        {3000, {{103.0, 1000}}, {{102.0, 1000}}}
    };
    
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kBuy, 100.0, 30}  // Покупатель снимает наши 30
    };
    
    std::cout << "Creating test files..." << std::endl;
    createTestL2File("test_sell_cancel_l2.csv", snapshots);
    createTestTradeFile("test_sell_cancel_trade.csv", trades);
    
    std::cout << "Loading L2 data..." << std::endl;
    backtester.loadL2Data("test_sell_cancel_l2.csv");
    
    std::cout << "Loading Trade data..." << std::endl;
    backtester.loadTradeData("test_sell_cancel_trade.csv");
    
    std::cout << "Running strategy..." << std::endl;
    backtester.runStrategy();
    
    std::cout << "\n=== Final State ===" << std::endl;
    std::cout << "Final cash: " << backtester.getCash() << std::endl;
    std::cout << "Final inventory: " << backtester.getInventory() << std::endl;
    std::cout << "Total orders placed: " << backtester.getTotalOrdersPlaced() << std::endl;
    std::cout << "Total fills: " << backtester.getTotalFills() << std::endl;
    
    const auto& fills = backtester.getFills();
    std::cout << "Fills count: " << fills.size() << std::endl;
    for (size_t i = 0; i < fills.size(); ++i) {
        std::cout << "  Fill " << i << ": order_id=" << fills[i].order_id
                  << ", side=" << (fills[i].side == Side::kBid ? "BUY" : "SELL")
                  << ", amount=" << fills[i].amount
                  << ", price=" << fills[i].price << std::endl;
    }
    
    // Должно исполниться 30 единиц продажи
    //EXPECT_DOUBLE_EQ(backtester.getInventory(), 70.0);  // Было 100, продали 30
    //EXPECT_DOUBLE_EQ(backtester.getCash(), 1000000.0 + 30.0 * 100.0);
    //EXPECT_EQ(backtester.getTotalFills(), 1);
    
    std::filesystem::remove("test_sell_cancel_l2.csv");
    std::filesystem::remove("test_sell_cancel_trade.csv");
    
    std::cout << "=== Test SellWithCancelAfterPartialFill END ===" << std::endl;
}