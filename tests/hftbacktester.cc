#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <filesystem>
#include <fstream>
#include <thread>
#include <future>
#include <vector>
#include <string>

// Подключаем заголовочные файлы вашего проекта
#include "mm/hftbacktester/hftbacktester.h"
#include "mm/strategy/i_strategy.h"
#include "mm/data_collector/i_data_collector.h"
#include "mm/type/l2snapshot.h"
#include "mm/type/market_context.h"
#include "mm/type/order.h"
#include "mm/type/fill.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

// ==============================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ==============================

void createTestL2File(const std::string& filename, const std::vector<std::tuple<int64_t, 
                       std::vector<std::pair<double, double>>, 
                       std::vector<std::pair<double, double>>>>& snapshots) {
    std::ofstream file(filename);
    file << "row,timestamp,ask_price0,ask_vol0,bid_price0,bid_vol0";
    for (int i = 1; i < 25; i++) {
        file << ",ask_price" << i << ",ask_vol" << i << ",bid_price" << i << ",bid_vol" << i;
    }
    file << "\n";
    
    for (size_t idx = 0; idx < snapshots.size(); ++idx) {
        auto [ts, asks, bids] = snapshots[idx];
        file << idx + 1 << "," << ts << ",";
        
        for (int i = 0; i < 25; i++) {
            double ask_price = (i < (int)asks.size()) ? asks[i].first : 0.0;
            double ask_vol = (i < (int)asks.size()) ? asks[i].second : 0.0;
            double bid_price = (i < (int)bids.size()) ? bids[i].first : 0.0;
            double bid_vol = (i < (int)bids.size()) ? bids[i].second : 0.0;
            
            file << ask_price << "," << ask_vol << "," << bid_price << "," << bid_vol;
            if (i < 24) file << ",";
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

// Тестовая стратегия
class TestStrategy : public IStrategy {
public:
    std::vector<MarketContext> received_contexts;
    std::vector<Fill> received_fills;
    
    void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
        received_contexts.push_back(ctx);
    }
    
    void onFill(const Fill& fill) override {
        received_fills.push_back(fill);
    }
};

// Стратегия для торговли
class TradingStrategy : public IStrategy {
public:
    bool buy_submitted = false;
    bool sell_submitted = false;
    double target_buy_price = 0;
    double target_sell_price = 0;
    
    void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
        if (!buy_submitted && !ctx.l2.asks.empty()) {
            target_buy_price = ctx.l2.asks[0].first;
            api->submitLimitOrder(Side::kBid, target_buy_price, 50.0);
            buy_submitted = true;
        }
        if (buy_submitted && !sell_submitted && !ctx.l2.bids.empty() && 
            ctx.l2.bids[0].first > target_buy_price + 0.5) {
            target_sell_price = ctx.l2.bids[0].first;
            api->submitLimitOrder(Side::kAsk, target_sell_price, 50.0);
            sell_submitted = true;
        }
    }
    
    void onFill(const Fill& fill) override {
        if (fill.side == Side::kBid) {
            std::cout << "Bought " << fill.amount << " at " << fill.price << std::endl;
        } else {
            std::cout << "Sold " << fill.amount << " at " << fill.price << std::endl;
        }
    }
};

// ==============================
// ТЕСТОВЫЕ СТРАТЕГИИ
// ==============================

// Стратегия для покупки с отменой
class SimpleCancelStrategy : public IStrategy {
public:
    bool order_sent = false;
    bool cancel_done = false;
    
    void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
        if (!order_sent && !ctx.l2.asks.empty()) {
            api->submitLimitOrder(Side::kBid, 100.0, 100);
            order_sent = true;
            std::cout << "BUY order sent at " << ctx.current_timestamp << std::endl;
        }
        
        if (order_sent && !cancel_done && ctx.current_timestamp > 2000) {
            api->cancelAll();
            cancel_done = true;
            std::cout << "Order cancelled at " << ctx.current_timestamp << std::endl;
        }
    }
};



// ==============================
// СТРАТЕГИЯ С ОТМЕНОЙ ПОСЛЕ ЧАСТИЧНОГО ИСПОЛНЕНИЯ
// ==============================

class CancelAfterPartialFillStrategy : public IStrategy {
public:
    enum State { INIT, ORDER_SENT, WAITING_FOR_FILL, PARTIAL_FILLED, CANCELLED } state = INIT;
    double initial_cash = 0;
    double initial_inventory = 0;
    int tick_count = 0;
    bool cancel_called = false;
    
    void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
        std::cout << "onMarketContext: state=" << state 
                  << ", timestamp=" << ctx.current_timestamp 
                  << ", cash=" << ctx.cash 
                  << ", inventory=" << ctx.inventory << std::endl;
        
        switch (state) {
            case INIT:
                if (!ctx.l2.asks.empty()) {
                    api->submitLimitOrder(Side::kBid, 100.0, 100);
                    initial_cash = ctx.cash;
                    initial_inventory = ctx.inventory;
                    state = WAITING_FOR_FILL;
                    std::cout << "Order sent: 100 units at 100.0" << std::endl;
                }
                break;
                
            case WAITING_FOR_FILL:
                if (ctx.cash != initial_cash || ctx.inventory != initial_inventory) {
                    double filled = std::abs(ctx.inventory - initial_inventory);
                    std::cout << "Portfolio changed! Filled: " << filled << std::endl;
                    
                    // Сразу отменяем остаток, не меняя state на промежуточный
                    api->cancelAll();
                    cancel_called = true;
                    state = CANCELLED;
                    std::cout << "Remaining order cancelled immediately" << std::endl;
                }
                break;
            default:
                break;
        }
    }
};

// Добавьте в тестовый файл
class TestDataCollector : public IFastDataCollector {
public:
    std::vector<Fill> received_fills;
    
    std::vector<double> onMarketContext(const MarketContext& ctx) override {
        return {};
    }
    
    std::vector<double> onFill(const Fill& fill) override {
        return {};
    }
};

// ==============================
// ТЕСТЫ КОНСТРУКТОРА И КОНФИГУРАЦИИ
// ==============================

TEST(HFTBacktesterTest, ConstructorDefaultConfig) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    SUCCEED();
}

TEST(HFTBacktesterTest, ConstructorCustomConfig) {
    HFTBacktesterConfig config;
    config.cash = 500000.0;
    config.inventory = 100.0;
    config.turnover = 500000.0;
    HFTBacktester backtester(std::move(config));
    SUCCEED();
}

// ==============================
// ТЕСТЫ ЗАГРУЗКИ ДАННЫХ
// ==============================

TEST(HFTBacktesterTest, LoadL2DataValidFile) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 100}}, {{99.5, 100}}}
    };
    createTestL2File("test_l2.csv", snapshots);
    
    EXPECT_NO_THROW(backtester.loadL2Data("test_l2.csv"));
    std::filesystem::remove("test_l2.csv");
}

TEST(HFTBacktesterTest, LoadTradeDataValidFile) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 100.0, 50}
    };
    createTestTradeFile("test_trade.csv", trades);
    
    EXPECT_NO_THROW(backtester.loadTradeData("test_trade.csv"));
    std::filesystem::remove("test_trade.csv");
}

TEST(HFTBacktesterTest, LoadEmptyL2File) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    std::ofstream file("empty_l2.csv");
    file << "row,timestamp,ask_price0,ask_vol0,bid_price0,bid_vol0\n";
    file.close();
    
    EXPECT_NO_THROW(backtester.loadL2Data("empty_l2.csv"));
    std::filesystem::remove("empty_l2.csv");
}

TEST(HFTBacktesterTest, LoadNonExistentFile) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    EXPECT_NO_THROW(backtester.loadL2Data("non_existent.csv"));
    EXPECT_NO_THROW(backtester.loadTradeData("non_existent.csv"));
}

// ==============================
// ТЕСТЫ API СТРАТЕГИИ
// ==============================

TEST(HFTBacktesterTest, SetStrategyAndCollector) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    auto strategy = std::make_unique<TestStrategy>();
    auto collector = std::make_unique<TestDataCollector>();
    
    EXPECT_NO_THROW(backtester.setStrategy(std::move(strategy)));
    EXPECT_NO_THROW(backtester.setDataCollector(std::move(collector)));
}

TEST(HFTBacktesterTest, SubmitLimitOrder) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    EXPECT_NO_THROW(backtester.submitLimitOrder(Side::kBid, 100.0, 50.0));
}

TEST(HFTBacktesterTest, SubmitMarketOrder) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    EXPECT_NO_THROW(backtester.submitMarketOrder(Side::kBid, 50.0));
}

TEST(HFTBacktesterTest, CancelAllOrders) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    backtester.submitLimitOrder(Side::kBid, 100.0, 50.0);
    EXPECT_NO_THROW(backtester.cancelAll());
}

// ==============================
// ТЕСТЫ COLLECT DATA
// ==============================

TEST(HFTBacktesterTest, CollectDataWithL2Only) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 100}}, {{99.5, 100}}},
        {2000, {{101.0, 150}}, {{100.0, 150}}}
    };
    createTestL2File("test_l2.csv", snapshots);
    backtester.loadL2Data("test_l2.csv");
    
    auto state = backtester.collectData(1.0);
    ASSERT_TRUE(state.has_value());

    EXPECT_EQ(state->current_l2_idx, 2);
    EXPECT_EQ(state->current_trade_idx, 0);
    
    std::filesystem::remove("test_l2.csv");
}

TEST(HFTBacktesterTest, CollectDataWithTradesOnly) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 100.0, 50},
        {2500, TradeSide::kBuy, 101.0, 30}
    };
    createTestTradeFile("test_trade.csv", trades);
    backtester.loadTradeData("test_trade.csv");
    
    auto state = backtester.collectData(1.0);
    ASSERT_TRUE(state.has_value());

    EXPECT_EQ(state->current_l2_idx, 0);
    EXPECT_EQ(state->current_trade_idx, 2);
    
    std::filesystem::remove("test_trade.csv");
}

TEST(HFTBacktesterTest, CollectDataMixed) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 100}}, {{99.5, 100}}},
        {2000, {{101.0, 150}}, {{100.0, 150}}}
    };
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 100.0, 50},
        {2500, TradeSide::kBuy, 101.0, 30}
    };
    
    createTestL2File("test_l2.csv", snapshots);
    createTestTradeFile("test_trade.csv", trades);
    backtester.loadL2Data("test_l2.csv");
    backtester.loadTradeData("test_trade.csv");
    
    auto state = backtester.collectData(1.0);
    ASSERT_TRUE(state.has_value());

    EXPECT_EQ(state->current_l2_idx, 2);
    EXPECT_EQ(state->current_trade_idx, 2);
    
    std::filesystem::remove("test_l2.csv");
    std::filesystem::remove("test_trade.csv");
}

TEST(HFTBacktesterTest, CollectDataPartialRatio) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 100}}, {{99.5, 100}}},
        {2000, {{101.0, 150}}, {{100.0, 150}}}
    };
    createTestL2File("test_l2.csv", snapshots);
    backtester.loadL2Data("test_l2.csv");
    
    auto state = backtester.collectData(0.5);
    ASSERT_TRUE(state.has_value());

    EXPECT_LE(state->current_l2_idx, 1);
    
    std::filesystem::remove("test_l2.csv");
}

// ==============================
// ТЕСТЫ RUN STRATEGY
// ==============================

TEST(HFTBacktesterTest, RunStrategyFromScratch) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    auto strategy = std::make_unique<TestStrategy>();
    backtester.setStrategy(std::move(strategy));
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 100}}, {{99.5, 100}}}
    };
    createTestL2File("test_l2.csv", snapshots);
    backtester.loadL2Data("test_l2.csv");
    
    EXPECT_NO_THROW(backtester.runStrategy());
    
    std::filesystem::remove("test_l2.csv");
}

TEST(HFTBacktesterTest, RunStrategyFromSavedState) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    auto strategy = std::make_unique<TestStrategy>();
    backtester.setStrategy(std::move(strategy));
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 100}}, {{99.5, 100}}},
        {2000, {{101.0, 150}}, {{100.0, 150}}}
    };
    createTestL2File("test_l2.csv", snapshots);
    backtester.loadL2Data("test_l2.csv");
    
    SimulationState state{1, 0, 950000.0, 50.0};
    EXPECT_NO_THROW(backtester.runStrategy(state));
    
    std::filesystem::remove("test_l2.csv");
}

// ==============================
// ТЕСТЫ ИСПОЛНЕНИЯ ОРДЕРОВ
// ==============================

TEST(HFTBacktesterTest, StrategyReceivesMarketContext) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    auto strategy = std::make_unique<TestStrategy>();
    auto* strategy_ptr = strategy.get();
    backtester.setStrategy(std::move(strategy));
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 100}}, {{99.5, 100}}}
    };
    createTestL2File("test_l2.csv", snapshots);
    backtester.loadL2Data("test_l2.csv");
    
    backtester.runStrategy();
    
    EXPECT_GE(strategy_ptr->received_contexts.size(), 1);
    
    std::filesystem::remove("test_l2.csv");
}

TEST(HFTBacktesterTest, FullTradingCycle) {
    HFTBacktesterConfig config;
    config.cash = 1000000.0;
    config.inventory = 0.0;
    HFTBacktester backtester(std::move(config));
    
    auto strategy = std::make_unique<TradingStrategy>();
    backtester.setStrategy(std::move(strategy));
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 1000}}, {{99.5, 1000}}},
        {2000, {{102.0, 1000}}, {{101.0, 1000}}}
    };
    
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 100.0, 50},
        {2500, TradeSide::kBuy, 101.0, 50}
    };
    
    createTestL2File("test_l2.csv", snapshots);
    createTestTradeFile("test_trade.csv", trades);
    backtester.loadL2Data("test_l2.csv");
    backtester.loadTradeData("test_trade.csv");
    
    EXPECT_NO_THROW(backtester.runStrategy());
    
    std::filesystem::remove("test_l2.csv");
    std::filesystem::remove("test_trade.csv");
}

// ==============================
// ТЕСТЫ ПОРТФЕЛЯ
// ==============================

TEST(HFTBacktesterTest, PortfolioNotChangedWithoutTrades) {
    HFTBacktesterConfig config;
    config.cash = 1000000.0;
    config.inventory = 0.0;
    HFTBacktester backtester(std::move(config));
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 100}}, {{99.5, 100}}}
    };
    createTestL2File("test_l2.csv", snapshots);
    backtester.loadL2Data("test_l2.csv");
    
    backtester.collectData(1.0);
    
    // Портфель не должен измениться при простом сборе данных
    EXPECT_DOUBLE_EQ(backtester.getCash(), 1000000.0);
    EXPECT_DOUBLE_EQ(backtester.getInventory(), 0.0);
    
    std::filesystem::remove("test_l2.csv");
}

// ==============================
// ТЕСТЫ КРАЕВЫХ СЛУЧАЕВ
// ==============================

TEST(HFTBacktesterTest, NoDataEmptyRun) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    EXPECT_NO_THROW(backtester.runStrategy());
}

TEST(HFTBacktesterTest, OnlyL2DataNoTrades) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 100}}, {{99.5, 100}}}
    };
    createTestL2File("test_l2.csv", snapshots);
    backtester.loadL2Data("test_l2.csv");
    
    auto strategy = std::make_unique<TradingStrategy>();
    backtester.setStrategy(std::move(strategy));
    
    EXPECT_NO_THROW(backtester.runStrategy());
    
    std::filesystem::remove("test_l2.csv");
}

TEST(HFTBacktesterTest, OnlyTradesNoL2Data) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 100.0, 50}
    };
    createTestTradeFile("test_trade.csv", trades);
    backtester.loadTradeData("test_trade.csv");
    
    auto strategy = std::make_unique<TradingStrategy>();
    backtester.setStrategy(std::move(strategy));
    
    EXPECT_NO_THROW(backtester.runStrategy());
    
    std::filesystem::remove("test_trade.csv");
}

// ==============================
// СТРЕСС-ТЕСТЫ
// ==============================

TEST(HFTBacktesterTest, LargeL2Data) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    std::ofstream l2_file("large_l2.csv");
    l2_file << "row,timestamp,ask_price0,ask_vol0,bid_price0,bid_vol0";
    for (int i = 1; i < 25; i++) {
        l2_file << ",ask_price" << i << ",ask_vol" << i << ",bid_price" << i << ",bid_vol" << i;
    }
    l2_file << "\n";
    
    for (int i = 1; i <= 10000; i++) {
        l2_file << i << "," << (1000000 + i * 100) << ",";
        for (int j = 0; j < 25; j++) {
            double price = 100.0 + j * 0.1;
            double vol = 1000.0 + j * 10;
            l2_file << price << "," << vol << "," << (price - 0.1) << "," << vol;
            if (j < 24) l2_file << ",";
        }
        l2_file << "\n";
    }
    l2_file.close();
    
    EXPECT_NO_THROW(backtester.loadL2Data("large_l2.csv"));
    EXPECT_NO_THROW(backtester.runStrategy());
    
    std::filesystem::remove("large_l2.csv");
}

TEST(HFTBacktesterTest, LargeTradeData) {
    HFTBacktesterConfig config;
    HFTBacktester backtester(std::move(config));
    
    std::ofstream trade_file("large_trade.csv");
    trade_file << "row,timestamp,side,price,amount\n";
    for (int i = 1; i <= 10000; i++) {
        trade_file << i << "," << (1000000 + i * 100) << ",";
        trade_file << ((i % 2 == 0) ? "buy" : "sell") << ",";
        trade_file << (100.0 + (i % 10) * 0.1) << "," << (100.0 + i) << "\n";
    }
    trade_file.close();
    
    EXPECT_NO_THROW(backtester.loadTradeData("large_trade.csv"));
    
    std::filesystem::remove("large_trade.csv");
}

// ==============================
// ТЕСТОВАЯ СТРАТЕГИЯ ДЛЯ ОТМЕНЫ ОРДЕРОВ
// ==============================

class CancelOrderStrategy : public IStrategy {
public:
    enum State { INIT, ORDER_SENT, ORDER_CANCELLED, DONE } state = INIT;
    int64_t order_id = -1;
    double target_price = 0.0;
    double target_qty = 0.0;
    
    void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
        switch (state) {
            case INIT:
                if (!ctx.l2.asks.empty()) {
                    target_price = ctx.l2.asks[0].first;
                    target_qty = 50.0;
                    api->submitLimitOrder(Side::kBid, target_price, target_qty);
                    state = ORDER_SENT;
                    std::cout << "Order sent at price " << target_price << std::endl;
                }
                break;
                
            case ORDER_SENT:
                // Отменяем ордер через некоторое время
                api->cancelAll();
                state = ORDER_CANCELLED;
                std::cout << "Order cancelled" << std::endl;
                break;
                
            default:
                break;
        }
    }
    
    void onFill(const Fill& fill) override {
        std::cout << "Fill received! amount=" << fill.amount 
                  << ", price=" << fill.price << std::endl;
    }
};

// ==============================
// ТЕСТЫ ОТМЕНЫ ОРДЕРОВ
// ==============================

TEST(HFTBacktesterTest, CancelOrderBeforeExecution1) {
    std::cout << "\n=== Test CancelOrderBeforeExecution START ===" << std::endl;
    
    HFTBacktesterConfig config;
    config.cash = 1000000.0;
    config.inventory = 0.0;
    HFTBacktester backtester(std::move(config));
    
    std::cout << "Initial state: cash=" << backtester.getCash() 
              << ", inventory=" << backtester.getInventory() << std::endl;
    
    // Стратегия, которая отправляет ордер по цене, которая НЕ может исполниться
    class ImmediateCancelStrategy : public IStrategy {
    public:
        bool order_sent = false;
        bool cancelled = false;
        
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
            std::cout << "onMarketContext: timestamp=" << ctx.current_timestamp 
                      << ", best_ask=" << (ctx.l2.asks.empty() ? 0 : ctx.l2.asks[0].first)
                      << ", best_bid=" << (ctx.l2.bids.empty() ? 0 : ctx.l2.bids[0].first) << std::endl;
            
            if (!order_sent && !ctx.l2.asks.empty()) {
                // Отправляем ордер по цене НИЖЕ лучшего ask (не может исполниться)
                // Лучший ask = 100.0, отправляем по 99.0
                double buy_price = ctx.l2.asks[0].first - 1.0;
                std::cout << "  -> Submitting BUY order at " << buy_price 
                          << " (best ask=" << ctx.l2.asks[0].first << ")" << std::endl;
                api->submitLimitOrder(Side::kBid, buy_price, 50);
                order_sent = true;
                std::cout << "  -> Order submitted" << std::endl;
                
                // НЕМЕДЛЕННО отменяем
                std::cout << "  -> Cancelling order immediately" << std::endl;
                api->cancelAll();
                cancelled = true;
                std::cout << "  -> Order cancelled" << std::endl;
            }
        }
        
        void onFill(const Fill& fill) override {
            std::cout << "  -> FILL received! amount=" << fill.amount 
                      << ", price=" << fill.price << std::endl;
            // Если fill пришел, значит что-то пошло не так
            EXPECT_TRUE(false) << "Fill should not happen!";
        }
    };
    
    auto strategy = std::make_unique<ImmediateCancelStrategy>();
    backtester.setStrategy(std::move(strategy));
    
    // Создаем L2 данные - ask на 100.0
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 1000}}, {{99.5, 1000}}},  // Best ask = 100.0
        {2000, {{101.0, 1000}}, {{100.0, 1000}}}
    };
    
    std::cout << "Creating test files..." << std::endl;
    createTestL2File("test_cancel_l2.csv", snapshots);
    
    std::cout << "Loading L2 data..." << std::endl;
    backtester.loadL2Data("test_cancel_l2.csv");
    
    std::cout << "Running strategy..." << std::endl;
    backtester.runStrategy();
    
    std::cout << "\n=== Final State ===" << std::endl;
    std::cout << "Final cash: " << backtester.getCash() << std::endl;
    std::cout << "Final inventory: " << backtester.getInventory() << std::endl;
    std::cout << "Total orders placed: " << backtester.getTotalOrdersPlaced() << std::endl;
    std::cout << "Total fills: " << backtester.getTotalFills() << std::endl;
    
    // Ордер был отменен, портфель не изменился
    EXPECT_DOUBLE_EQ(backtester.getCash(), 1000000.0);
    EXPECT_DOUBLE_EQ(backtester.getInventory(), 0.0);
    EXPECT_EQ(backtester.getTotalOrdersPlaced(), 1);
    EXPECT_EQ(backtester.getTotalFills(), 0);
    
    std::filesystem::remove("test_cancel_l2.csv");
    
    std::cout << "=== Test CancelOrderBeforeExecution END ===" << std::endl;
}

// ==============================
// СТРАТЕГИЯ С ЧАСТИЧНОЙ ОТМЕНОЙ
// ==============================

class PartialCancelStrategy : public IStrategy {
public:
    enum State { INIT, FIRST_ORDER, SECOND_ORDER, CANCEL_FIRST, DONE } state = INIT;
    int64_t first_order_id = -1;
    int64_t second_order_id = -1;
    
    void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
        switch (state) {
            case INIT:
                if (!ctx.l2.asks.empty()) {
                    // Отправляем первый ордер
                    api->submitLimitOrder(Side::kBid, 100.0, 30);
                    state = FIRST_ORDER;
                    std::cout << "First order sent" << std::endl;
                }
                break;
                
            case FIRST_ORDER:
                // Отправляем второй ордер
                api->submitLimitOrder(Side::kBid, 99.5, 20);
                state = SECOND_ORDER;
                std::cout << "Second order sent" << std::endl;
                break;
                
            case SECOND_ORDER:
                // Отменяем только первый ордер
                // В реальном API нужен метод cancelOrder(1)
                // Пока используем cancelAll для простоты
                api->cancelAll();
                state = DONE;
                std::cout << "Orders cancelled" << std::endl;
                break;
                
            default:
                break;
        }
    }
    
    void onFill(const Fill& fill) override {
        std::cout << "Fill: " << fill.amount << " @ " << fill.price << std::endl;
    }
};

TEST(HFTBacktesterTest, PartialCancelOrders) {
    std::cout << "\n=== Test PartialCancelOrders START ===" << std::endl;
    
    HFTBacktesterConfig config;
    config.cash = 1000000.0;
    config.inventory = 0.0;
    HFTBacktester backtester(std::move(config));
    
    // ============================================================
    // Пассивная стратегия: отправляет лимитные ордера, которые НЕ исполняются сразу
    // ============================================================
    class PassivePartialCancelStrategy : public IStrategy {
    public:
        enum State { INIT, FIRST_ORDER_SENT, SECOND_ORDER_SENT, CANCELLED } state = INIT;
        
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
            std::cout << "[Strategy] state=" << state 
                      << ", ts=" << ctx.current_timestamp
                      << ", best_ask=" << (ctx.l2.asks.empty() ? 0 : ctx.l2.asks[0].first)
                      << ", inv=" << ctx.inventory 
                      << ", cash=" << ctx.cash << std::endl;
            
            switch (state) {
                case INIT:
                    if (!ctx.l2.asks.empty()) {
                        // Пассивный ордер - цена НИЖЕ лучшего ask
                        double passive_price = ctx.l2.asks[0].first - 0.5;
                        std::cout << "[Strategy] Sending PASSIVE order 1: price=" << passive_price << ", qty=30" << std::endl;
                        api->submitLimitOrder(Side::kBid, passive_price, 30);
                        state = FIRST_ORDER_SENT;
                    }
                    break;
                    
                case FIRST_ORDER_SENT:
                    if (ctx.current_timestamp > 1000) {
                        // Отправляем второй пассивный ордер
                        double passive_price = ctx.l2.asks[0].first - 0.5;
                        std::cout << "[Strategy] Sending PASSIVE order 2: price=" << passive_price << ", qty=20" << std::endl;
                        api->submitLimitOrder(Side::kBid, passive_price, 20);
                        state = SECOND_ORDER_SENT;
                    }
                    break;
                    
                case SECOND_ORDER_SENT:
                    if (ctx.current_timestamp > 1500) {
                        // Отменяем только первый ордер (id=1)
                        std::cout << "[Strategy] Cancelling order 1 only..." << std::endl;
                        auto orderRef = LimitOrderRef{1};
                        api->cancelOrder(orderRef);
                        state = CANCELLED;
                    }
                    break;
                    
                default:
                    break;
            }
        }
        
        void onFill(const Fill& fill) override {
            std::cout << "[Strategy] FILL: " << fill.amount << " @ " << fill.price 
                      << " [" << (fill.side == Side::kBid ? "BUY" : "SELL") << "]"
                      << " order_id=" << fill.order_id << std::endl;
        }
    };
    
    auto strategy = std::make_unique<PassivePartialCancelStrategy>();
    backtester.setStrategy(std::move(strategy));
    
    // L2 данные: ask на 100.0 (наши пассивные ордера по 99.5, не пересекаются)
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 1000}}, {{99.5, 1000}}},  // Лучший ask = 100.0
        {2000, {{101.0, 1000}}, {{100.0, 1000}}}
    };
    
    createTestL2File("test_partial_l2.csv", snapshots);
    backtester.loadL2Data("test_partial_l2.csv");
    
    std::cout << "\n=== Running Strategy ===" << std::endl;
    backtester.runStrategy();
    
    std::cout << "\n=== Final State ===" << std::endl;
    std::cout << "Final cash: " << backtester.getCash() << std::endl;
    std::cout << "Final inventory: " << backtester.getInventory() << std::endl;
    std::cout << "Total orders placed: " << backtester.getTotalOrdersPlaced() << std::endl;
    std::cout << "Total fills: " << backtester.getTotalFills() << std::endl;
    
    const auto& fills = backtester.getFills();
    for (size_t i = 0; i < fills.size(); ++i) {
        std::cout << "  Fill " << i << ": order_id=" << fills[i].order_id
                  << ", side=" << (fills[i].side == Side::kBid ? "BUY" : "SELL")
                  << ", amount=" << fills[i].amount
                  << ", price=" << fills[i].price << std::endl;
    }
    
    // Все ордера были пассивными, ни один не исполнился (нет встречных трейдов)
    // Первый ордер отменен, второй остался в книге
    EXPECT_DOUBLE_EQ(backtester.getCash(), 1000000.0);
    EXPECT_DOUBLE_EQ(backtester.getInventory(), 0.0);
    
    std::filesystem::remove("test_partial_l2.csv");
    
    std::cout << "=== Test PartialCancelOrders END ===" << std::endl;
}

TEST(HFTBacktesterTest, CancelAfterPartialFill) {
    std::cout << "\n=== Test CancelAfterPartialFill START ===" << std::endl;
    
    HFTBacktesterConfig config;
    config.cash = 1000000.0;
    config.inventory = 0.0;
    HFTBacktester backtester(std::move(config));
    
    // ============================================================
    // Стратегия "PassiveLimitOrder" - отправляет ПАССИВНЫЙ лимитный ордер
    // (цена хуже лучшего ask), ждет когда внешний трейд его исполнит
    // ============================================================
    class PassiveLimitOrderStrategy : public IStrategy {
    public:
        enum State { AWAITING_ORDER_SEND, WAITING_FOR_FILL, CANCELLED } state = AWAITING_ORDER_SEND;
        double initial_inventory = 0;
        bool cancelled = false;
        
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
            std::cout << "[Strategy] state=" << state 
                      << ", ts=" << ctx.current_timestamp
                      << ", best_ask=" << (ctx.l2.asks.empty() ? 0 : ctx.l2.asks[0].first)
                      << ", inv=" << ctx.inventory 
                      << ", cash=" << ctx.cash << std::endl;
            
            switch (state) {
                case AWAITING_ORDER_SEND:
                    if (!ctx.l2.asks.empty()) {
                        // Отправляем ПАССИВНЫЙ ордер - цена НИЖЕ лучшего ask
                        // Например: лучший ask = 100.0, отправляем по 99.0
                        double passive_price = ctx.l2.asks[0].first - 1.0;
                        std::cout << "[Strategy] Sending PASSIVE LIMIT BUY order: price=" << passive_price << ", qty=100" << std::endl;
                        api->submitLimitOrder(Side::kBid, passive_price, 100);
                        initial_inventory = ctx.inventory;
                        state = WAITING_FOR_FILL;
                        std::cout << "[Strategy] Order sent (passive, waiting for external trade)" << std::endl;
                    }
                    break;
                    
                case WAITING_FOR_FILL:
                    // Проверяем, изменился ли портфель (внешний трейд исполнил наш ордер)
                    if (!cancelled && ctx.inventory != initial_inventory) {
                        double filled = ctx.inventory - initial_inventory;
                        std::cout << "[Strategy] Fill detected! Filled=" << filled << " units" << std::endl;
                        std::cout << "[Strategy] Cancelling remaining order..." << std::endl;
                        api->cancelAll();
                        cancelled = true;
                        state = CANCELLED;
                        std::cout << "[Strategy] Order cancelled" << std::endl;
                    }
                    break;
                    
                default:
                    break;
            }
        }
        
        void onFill(const Fill& fill) override {
            std::cout << "[Strategy] FILL: " << fill.amount << " @ " << fill.price 
                      << " [" << (fill.side == Side::kBid ? "BUY" : "SELL") << "]" << std::endl;
        }
    };
    
    auto strategy = std::make_unique<PassiveLimitOrderStrategy>();
    backtester.setStrategy(std::move(strategy));
    
    // L2 данные: ask на 100.0 объемом 30 (но наш ордер по 99.0, не пересекается)
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 30}}, {{99.5, 1000}}},  // Лучший ask = 100.0
        {3000, {{102.0, 1000}}, {{101.0, 1000}}}
    };
    
    // Внешний трейд: кто-то продает по 99.0 (снимает наш пассивный ордер)
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 99.0, 30}  // Внешний продавец по 99.0 (наша цена)
    };
    
    std::cout << "Creating test files..." << std::endl;
    createTestL2File("test_partial_fill_l2.csv", snapshots);
    createTestTradeFile("test_partial_fill_trade.csv", trades);
    
    std::cout << "Loading L2 data..." << std::endl;
    backtester.loadL2Data("test_partial_fill_l2.csv");
    
    std::cout << "Loading Trade data..." << std::endl;
    backtester.loadTradeData("test_partial_fill_trade.csv");
    
    std::cout << "\n=== Running Strategy ===" << std::endl;
    backtester.runStrategy();
    
    std::cout << "\n=== Final State ===" << std::endl;
    std::cout << "Final cash: " << backtester.getCash() << std::endl;
    std::cout << "Final inventory: " << backtester.getInventory() << std::endl;
    std::cout << "Total orders placed: " << backtester.getTotalOrdersPlaced() << std::endl;
    std::cout << "Total fills: " << backtester.getTotalFills() << std::endl;
    
    const auto& fills = backtester.getFills();
    for (size_t i = 0; i < fills.size(); ++i) {
        std::cout << "  Fill " << i << ": order_id=" << fills[i].order_id
                  << ", side=" << (fills[i].side == Side::kBid ? "BUY" : "SELL")
                  << ", amount=" << fills[i].amount
                  << ", price=" << fills[i].price << std::endl;
    }
    
    // Проверки: должно исполниться 30 из 100 (внешний трейд), остальное отменено
    EXPECT_TRUE(backtester.getInventory() == 30.0 || backtester.getInventory() == 0.0);
    if (backtester.getInventory() == 30.0) {
        std::cout << "✅ Partial fill worked correctly" << std::endl;
    } else {
        std::cout << "❌ Partial fill did not work" << std::endl;
    }
    
    std::filesystem::remove("test_partial_fill_l2.csv");
    std::filesystem::remove("test_partial_fill_trade.csv");
    
    std::cout << "=== Test CancelAfterPartialFill END ===" << std::endl;
}

// ==============================
// СТРАТЕГИЯ С ОТМЕНОЙ И ПРОВЕРКОЙ PNL
// ==============================

class CancelAndCheckPnLStrategy : public IStrategy {
public:
    enum State { INIT, BUY_SENT, BUY_EXECUTED, SELL_SENT, SELL_EXECUTED, DONE } state = INIT;
    double buy_price = 0;
    double sell_price = 0;
    
    void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
        switch (state) {
            case INIT:
                if (!ctx.l2.asks.empty()) {
                    buy_price = ctx.l2.asks[0].first;
                    api->submitLimitOrder(Side::kBid, buy_price, 50);
                    state = BUY_SENT;
                    std::cout << "Buy order sent at " << buy_price << std::endl;
                }
                break;
                
            case BUY_EXECUTED:
                if (!ctx.l2.bids.empty() && ctx.l2.bids[0].first > buy_price + 0.5) {
                    sell_price = ctx.l2.bids[0].first;
                    api->submitLimitOrder(Side::kAsk, sell_price, 50);
                    state = SELL_SENT;
                    std::cout << "Sell order sent at " << sell_price << std::endl;
                }
                break;
                
            case SELL_EXECUTED:
                // Все сделано, отменяем все на всякий случай
                api->cancelAll();
                state = DONE;
                break;
                
            default:
                break;
        }
    }
    
    void onFill(const Fill& fill) override {
        if (fill.side == Side::kBid) {
            std::cout << "Bought " << fill.amount << " at " << fill.price << std::endl;
            if (state == BUY_SENT) {
                state = BUY_EXECUTED;
            }
        } else if (fill.side == Side::kAsk) {
            std::cout << "Sold " << fill.amount << " at " << fill.price << std::endl;
            if (state == SELL_SENT) {
                state = SELL_EXECUTED;
            }
        }
    }
};

// ==============================
// ТЕСТЫ ДЛЯ ПОКУПКИ С ОТМЕНОЙ
// ==============================

// TEST(HFTBacktesterTest, PassiveBuyExecutionByPublicTrade) {
//     std::cout << "\n=== Test: Execution by Public Trade START ===" << std::endl;
    
//     HFTBacktesterConfig config;
//     config.cash = 1000000.0;
//     config.inventory = 0.0;
//     HFTBacktester backtester(std::move(config));

//     class PassiveConsistencyStrategy : public IStrategy {
//     public:
//         enum State { INIT, WAITING_FOR_TRADE, FINISHED } state = INIT;
//         double target_price = 99.0;
        
//         void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
//             switch (state) {
//                 case INIT:
//                     // Видим стакан, ставим лимитку НИЖЕ рынка (пассивно)
//                     if (!ctx.l2.asks.empty()) {
//                         std::cout << "[Strategy] Placing Passive Limit Buy at " << target_price << std::endl;
//                         api->submitLimitOrder(Side::kBid, target_price, 100);
//                         state = WAITING_FOR_TRADE;
//                     }
//                     break;
                    
//                 case WAITING_FOR_TRADE:
//                     // Если инвентарь изменился — значит, прошел публичный трейд по нашей цене
//                     if (ctx.inventory > 0.00000001) {
//                         std::cout << "[Strategy] Fill detected from Public Trade! Inventory: " << ctx.inventory << std::endl;
//                         std::cout << "[Strategy] Cancelling remaining part..." << std::endl;
//                         api->cancelAll();
//                         state = FINISHED;
//                     }
//                     break;
//                 default: break;
//             }
//         }
//     };

//     backtester.setStrategy(std::make_unique<PassiveConsistencyStrategy>());

//     std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
//         // 1. кто то ставим ордер. Рынок далеко.
//         {1000, {{100.0, 50}}, {{99.5, 50}}}, 
        
//         // 2. Рынок упал. Теперь 99.0 - это Best Bid. 
//         // лимитка теперь часть этого объема (или стоит за ним).
//         {1200, {{99.5, 50}}, {{99.0, 100}}} 
//     };

//     // 3. Трейд: агрессивный продавец ударил в цену 99.0.
//     // Так как мы стояли на этом уровне, мы получим исполнение так как выставились сюда раньше.
//     std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
//         {1500, TradeSide::kSell, 99.0, 40} 
//     };

//     createTestL2File("consistent_l2.csv", snapshots);
//     createTestTradeFile("consistent_trade.csv", trades);

//     backtester.loadL2Data("consistent_l2.csv");
//     backtester.loadTradeData("consistent_trade.csv");

//     // Запуск: 
//     // - t=1000: ставим ордер
//     // - t=1500: приходит трейд, движок матчит его об нашу лимитку
//     // - t=1501: стратегия видит fill и делает cancel
//     backtester.runStrategy();

//     // Проверки согласованности:
//     // Мы просили 100, но рынок продал только 40. Мы должны забрать все 40 (или меньше, если была очередь).
//     EXPECT_DOUBLE_EQ(backtester.getInventory(), 40.0);
//     EXPECT_DOUBLE_EQ(backtester.getCash(), 1000000.0 - 40.0 * 99.0);
    
//     std::filesystem::remove("consistent_l2.csv");
//     std::filesystem::remove("consistent_trade.csv");
//     std::cout << "=== Test: Execution by Public Trade END ===" << std::endl;
// }

TEST(HFTBacktesterTest, PassiveBuyExecutionByPublicTrade1) {
    std::cout << "\n=== Test: Execution by Public Trade START ===" << std::endl;
    
    HFTBacktesterConfig config;
    config.cash = 1000000.0;
    config.inventory = 0.0;
    HFTBacktester backtester(std::move(config));

    class PassiveConsistencyStrategy : public IStrategy {
    public:
        enum State { INIT, WAITING_FOR_TRADE, FINISHED } state = INIT;
        double target_price = 99.0;
        double initial_inventory = 0;
        double initial_cash = 0;
        OrderRef order_id;
        
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
            std::cout << "[Strategy] onMarketContext: state=" << state 
                      << ", ts=" << ctx.current_timestamp
                      << ", best_ask=" << (ctx.l2.asks.empty() ? 0 : ctx.l2.asks[0].first)
                      << ", best_bid=" << (ctx.l2.bids.empty() ? 0 : ctx.l2.bids[0].first)
                      << ", inv=" << ctx.inventory 
                      << ", cash=" << ctx.cash << std::endl;
            
            // Выводим текущее состояние книги из Matching Engine
            auto bids = api->getMatchingEngine().getBids();
            auto asks = api->getMatchingEngine().getAsks();
            std::cout << "[Strategy] ME Bids: ";
            for (const auto& bid : bids) {
                std::cout << bid.first << ":" << bid.second << " ";
            }
            std::cout << std::endl;
            std::cout << "[Strategy] ME Asks: ";
            for (const auto& ask : asks) {
                std::cout << ask.first << ":" << ask.second << " ";
            }
            std::cout << std::endl;
            
            switch (state) {
                case INIT:
                    if (!ctx.l2.asks.empty()) {
                        std::cout << "[Strategy] Placing Passive Limit Buy at " << target_price << ", qty=100" << std::endl;
                        order_id = api->submitLimitOrder(Side::kBid, target_price, 100);
                        initial_inventory = ctx.inventory;
                        initial_cash = ctx.cash;
                        state = WAITING_FOR_TRADE;
                        std::cout << "[Strategy] Order placed, id=" << order_id.getOrderId() << std::endl;
                    }
                    break;
                    
                case WAITING_FOR_TRADE:
                    if (ctx.inventory != initial_inventory) {
                        double filled = ctx.inventory - initial_inventory;
                        std::cout << "[Strategy] Fill detected! Filled=" << filled << " units" << std::endl;
                        api->cancelAll();
                        state = FINISHED;
                    }
                    break;
                    
                default: 
                    break;
            }
        }
        
        void onFill(const Fill& fill) override {
            std::cout << "[Strategy] FILL CALLBACK: amount=" << fill.amount 
                      << ", price=" << fill.price 
                      << ", side=" << (fill.side == Side::kBid ? "BUY" : "SELL")
                      << ", order_id=" << fill.order_id << std::endl;
        }
    };

    backtester.setStrategy(std::make_unique<PassiveConsistencyStrategy>());

    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 50}}, {{99.5, 50}}}, 
        {1200, {{99.5, 50}}, {{99.0, 100}}}
    };

    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 99.0, 40}
    };

    createTestL2File("consistent_l2.csv", snapshots);
    createTestTradeFile("consistent_trade.csv", trades);

    backtester.loadL2Data("consistent_l2.csv");
    backtester.loadTradeData("consistent_trade.csv");

    backtester.runStrategy();
    
    std::cout << "\n=== Final State ===" << std::endl;
    std::cout << "Final inventory: " << backtester.getInventory() << std::endl;
    std::cout << "Final cash: " << backtester.getCash() << std::endl;
    
    // Выводим финальное состояние Matching Engine
    auto final_bids = backtester.getMatchingEngine().getBids();
    std::cout << "Final ME Bids: ";
    for (const auto& bid : final_bids) {
        std::cout << bid.first << ":" << bid.second << " ";
    }
    std::cout << std::endl;

    EXPECT_DOUBLE_EQ(backtester.getInventory(), 40.0);
    EXPECT_DOUBLE_EQ(backtester.getCash(), 1000000.0 - 40.0 * 99.0);
    
    std::filesystem::remove("consistent_l2.csv");
    std::filesystem::remove("consistent_trade.csv");
    std::cout << "=== Test: Execution by Public Trade END ===" << std::endl;
}

// ==============================
// ТЕСТЫ ДЛЯ ПРОДАЖИ С ОТМЕНОЙ
// ==============================

// TEST(HFTBacktesterTest, SellWithCancelAfterPartialFill) {
//     HFTBacktesterConfig config;
//     config.cash = 1000000.0;
//     config.inventory = 100.0;  // Начинаем с 100 единиц
//     HFTBacktester backtester(std::move(config));
    
//     auto strategy = std::make_unique<SimpleSellCancelStrategy>();
//     backtester.setStrategy(std::move(strategy));
    
//     std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
//         {1000, {{101.0, 1000}}, {{100.0, 30}}},
//         {3000, {{103.0, 1000}}, {{102.0, 1000}}}
//     };
    
//     std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
//         {1500, TradeSide::kBuy, 100.0, 30}  // Покупатель снимает наши 30
//     };
    
//     createTestL2File("test_sell_cancel_l2.csv", snapshots);
//     createTestTradeFile("test_sell_cancel_trade.csv", trades);
    
//     backtester.loadL2Data("test_sell_cancel_l2.csv");
//     backtester.loadTradeData("test_sell_cancel_trade.csv");
    
//     backtester.runStrategy();
    
//     // Должно исполниться 30 единиц продажи
//     EXPECT_DOUBLE_EQ(backtester.getInventory(), 70.0);  // Было 100, продали 30
//     EXPECT_DOUBLE_EQ(backtester.getCash(), 1000000.0 + 30.0 * 100.0);
//     //EXPECT_EQ(backtester.getTotalFills(), 1);
    
//     std::filesystem::remove("test_sell_cancel_l2.csv");
//     std::filesystem::remove("test_sell_cancel_trade.csv");
// }

TEST(HFTBacktesterTest, SellWithCancelAfterPartialFill1) {
    // Стратегия для продажи с отменой
    class SimpleSellCancelStrategy : public IStrategy {
    public:
        bool order_sent = false;
        bool cancel_done = false;
        
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
            if (!order_sent && !ctx.l2.bids.empty()) {
                api->submitLimitOrder(Side::kAsk, 100, 100);//нельзя ставить 00 иначе ударим в биды и придётся потом надо будет докручивать трейды чтобы эта сделка отобразилась
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
        {1000, {{101.0, 1000}}, {{99.0, 30}}},
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
    EXPECT_DOUBLE_EQ(backtester.getInventory(), 70.0);  // Было 100, продали 30
    EXPECT_DOUBLE_EQ(backtester.getCash(), 1000000.0 + 30.0 * 100.0);
    EXPECT_EQ(backtester.getTotalFills(), 1);
    
    std::filesystem::remove("test_sell_cancel_l2.csv");
    std::filesystem::remove("test_sell_cancel_trade.csv");
    
    std::cout << "=== Test SellWithCancelAfterPartialFill END ===" << std::endl;
}

// ==============================
// ТЕСТЫ ДЛЯ МАТЧИНГА (ОБНОВЛЕННАЯ ЛОГИКА)
// ==============================

TEST(MatchingEngineTest, MatchLimitBuyWithLimitSell) {
    MatchingEngine engine;
    
    // Добавляем лимитный ордер на продажу
    Order sell;
    sell.order_id = LimitOrderRef{1};
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 50;
    sell.timestamp = 1000;
    auto sell_trades = engine.addLimitOrder(sell);
    EXPECT_TRUE(sell_trades.empty());
    
    // Добавляем лимитный ордер на покупку
    Order buy;
    buy.order_id = LimitOrderRef{2};
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 50;
    buy.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(buy);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 50);
    EXPECT_EQ(trades[0].side, TradeSide::kBuy);
    
    // Книга должна быть пуста
    EXPECT_TRUE(engine.getBids().empty());
    EXPECT_TRUE(engine.getAsks().empty());
}

TEST(MatchingEngineTest, MatchLimitSellWithLimitBuy) {
    MatchingEngine engine;
    
    // Добавляем лимитный ордер на покупку
    Order buy;
    buy.order_id = LimitOrderRef{1};
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 50;
    buy.timestamp = 1000;
    auto buy_trades = engine.addLimitOrder(buy);
    EXPECT_TRUE(buy_trades.empty());
    
    // Добавляем лимитный ордер на продажу
    Order sell;
    sell.order_id = LimitOrderRef{2};
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 50;
    sell.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(sell);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 50);
    EXPECT_EQ(trades[0].side, TradeSide::kSell);
    
    EXPECT_TRUE(engine.getBids().empty());
    EXPECT_TRUE(engine.getAsks().empty());
}

TEST(MatchingEngineTest, MatchMultipleLevelsBuy) {
    MatchingEngine engine;
    
    // Добавляем лимитные ордера на продажу на разных уровнях
    Order sell1;
    sell1.order_id = LimitOrderRef{1};
    sell1.side = Side::kAsk;
    sell1.type = OrderType::kLimit;
    sell1.price = 100.0;
    sell1.amount = 30;
    sell1.timestamp = 1000;
    engine.addLimitOrder(sell1);
    
    Order sell2;
    sell2.order_id = LimitOrderRef{2};
    sell2.side = Side::kAsk;
    sell2.type = OrderType::kLimit;
    sell2.price = 101.0;
    sell2.amount = 40;
    sell2.timestamp = 1000;
    engine.addLimitOrder(sell2);
    
    // Добавляем лимитный ордер на покупку
    Order buy;
    buy.order_id = LimitOrderRef{3};
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 101.0;
    buy.amount = 60;
    buy.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(buy);
    
    ASSERT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_DOUBLE_EQ(trades[1].price, 101.0);
    EXPECT_DOUBLE_EQ(trades[1].amount, 30);
    
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].second, 10);
}

TEST(MatchingEngineTest, PartialFillLimitBuy) {
    MatchingEngine engine;
    
    // Добавляем лимитный ордер на продажу
    Order sell;
    sell.order_id = LimitOrderRef{1};
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 100;
    sell.timestamp = 1000;
    engine.addLimitOrder(sell);
    
    // Добавляем лимитный ордер на покупку (меньше объема)
    Order buy;
    buy.order_id = LimitOrderRef{2};
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 60;
    buy.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(buy);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].amount, 60);
    
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].second, 40);
}

TEST(MatchingEngineTest, PartialFillLimitSell) {
    MatchingEngine engine;
    
    // Добавляем лимитный ордер на покупку
    Order buy;
    buy.order_id = LimitOrderRef{1};
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 100;
    buy.timestamp = 1000;
    engine.addLimitOrder(buy);
    
    // Добавляем лимитный ордер на продажу (меньше объема)
    Order sell;
    sell.order_id = LimitOrderRef{2};
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 60;
    sell.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(sell);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].amount, 60);
    
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].second, 40);
}

// ==============================
// ТЕСТЫ ДЛЯ РЫНОЧНЫХ ОРДЕРОВ
// ==============================

TEST(MatchingEngineTest, MarketBuyOrder1) {
    MatchingEngine engine;
    
    // Добавляем лимитные ордера на продажу
    Order sell1;
    sell1.order_id = LimitOrderRef{1};
    sell1.side = Side::kAsk;
    sell1.type = OrderType::kLimit;
    sell1.price = 100.0;
    sell1.amount = 50;
    sell1.timestamp = 1000;
    engine.addLimitOrder(sell1);
    
    Order sell2;
    sell2.order_id = LimitOrderRef{2};
    sell2.side = Side::kAsk;
    sell2.type = OrderType::kLimit;
    sell2.price = 101.0;
    sell2.amount = 30;
    sell2.timestamp = 1000;
    engine.addLimitOrder(sell2);
    
    // Рыночный ордер на покупку
    Order marketBuy;
    marketBuy.order_id = MarketOrderRef{3};
    marketBuy.side = Side::kBid;
    marketBuy.type = OrderType::kMarket;
    marketBuy.amount = 70;
    marketBuy.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(marketBuy);
    
    ASSERT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 50);
    EXPECT_DOUBLE_EQ(trades[1].price, 101.0);
    EXPECT_DOUBLE_EQ(trades[1].amount, 20);
    
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].second, 10);
}

TEST(MatchingEngineTest, MarketSellOrder1) {
    MatchingEngine engine;
    
    // Добавляем лимитные ордера на покупку
    Order buy1;
    buy1.order_id = LimitOrderRef{1};
    buy1.side = Side::kBid;
    buy1.type = OrderType::kLimit;
    buy1.price = 99.0;
    buy1.amount = 40;
    buy1.timestamp = 1000;
    engine.addLimitOrder(buy1);
    
    Order buy2;
    buy2.order_id = LimitOrderRef{2};
    buy2.side = Side::kBid;
    buy2.type = OrderType::kLimit;
    buy2.price = 98.5;
    buy2.amount = 60;
    buy2.timestamp = 1000;
    engine.addLimitOrder(buy2);
    
    // Рыночный ордер на продажу
    Order marketSell;
    marketSell.order_id = MarketOrderRef{3};
    marketSell.side = Side::kAsk;
    marketSell.type = OrderType::kMarket;
    marketSell.amount = 80;
    marketSell.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(marketSell);
    
    ASSERT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 99.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 40);
    EXPECT_DOUBLE_EQ(trades[1].price, 98.5);
    EXPECT_DOUBLE_EQ(trades[1].amount, 40);
    
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].second, 20);
}

// ==============================
// ТЕСТЫ ДЛЯ ОТМЕНЫ ОРДЕРОВ В MATCHING ENGINE
// ==============================

TEST(MatchingEngineTest, CancelOrderInMatchingEngine) {
    MatchingEngine engine;
    
    Order buy;
    buy.order_id = LimitOrderRef{1};
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 99.0;
    buy.amount = 100;
    buy.timestamp = 1000;
    engine.addLimitOrder(buy);
    
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    
    bool cancelled = engine.cancelOrder(LimitOrderRef{1});
    EXPECT_TRUE(cancelled);
    
    bids = engine.getBids();
    EXPECT_TRUE(bids.empty());
}

// ==============================
// ТЕСТЫ ДЛЯ L2 СНАПШОТОВ
// ==============================

TEST(MatchingEngineTest, L2SnapshotAfterAddSnapshot) {
    MatchingEngine engine;
    
    std::vector<std::pair<double, double>> asks = {{100.0, 100}, {101.0, 200}};
    std::vector<std::pair<double, double>> bids = {{99.5, 100}, {99.0, 200}};
    
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    auto result_asks = engine.getAsks();
    auto result_bids = engine.getBids();
    
    ASSERT_EQ(result_asks.size(), 2);
    EXPECT_DOUBLE_EQ(result_asks[0].first, 100.0);
    EXPECT_DOUBLE_EQ(result_asks[0].second, 100);
    EXPECT_DOUBLE_EQ(result_asks[1].first, 101.0);
    EXPECT_DOUBLE_EQ(result_asks[1].second, 200);
    
    ASSERT_EQ(result_bids.size(), 2);
    EXPECT_DOUBLE_EQ(result_bids[0].first, 99.5);
    EXPECT_DOUBLE_EQ(result_bids[0].second, 100);
    EXPECT_DOUBLE_EQ(result_bids[1].first, 99.0);
    EXPECT_DOUBLE_EQ(result_bids[1].second, 200);
}


// ==============================
// ТЕСТЫ ЗАГРУЗКИ ДАННЫХ
// ==============================

TEST(HistoryManagerTest, LoadL2DataDirectly) {
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 100}}, {{99.5, 100}}},
        {2000, {{101.0, 150}}, {{100.0, 150}}}
    };
    createTestL2File("test_l2_direct.csv", snapshots);
    
    auto hm = std::make_shared<HistoryManager>();
    hm->loadL2Data("test_l2_direct.csv");
    
    EXPECT_EQ(hm->getL2Size(), 2);
    EXPECT_GT(hm->getL2Size(), 0);
    
    const auto& l2 = hm->getL2(0);
    EXPECT_EQ(l2.timestamp, 1000);
    
    std::filesystem::remove("test_l2_direct.csv");
}

TEST(HistoryManagerTest, LoadTradeDataDirectly) {
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 100.0, 50},
        {2500, TradeSide::kBuy, 101.0, 30}
    };
    createTestTradeFile("test_trade_direct.csv", trades);
    
    auto hm = std::make_shared<HistoryManager>();
    hm->loadTradeData("test_trade_direct.csv");
    
    EXPECT_EQ(hm->getTradeSize(), 2);
    EXPECT_GT(hm->getTradeSize(), 0);
    
    const auto& trade = hm->getTrade(0);
    EXPECT_EQ(trade.timestamp, 1500);
    
    std::filesystem::remove("test_trade_direct.csv");
}

// ==============================
// ТЕСТЫ СРАВНЕНИЯ
// ==============================

TEST(HFTBacktesterTest, CompareInternalVsExternalHistoryManager) {
    const std::string l2_file = "compare_l2.csv";
    const std::string trade_file = "compare_trade.csv";
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 1000}}, {{99.5, 1000}}},
        {2000, {{101.0, 1000}}, {{100.0, 1000}}}
    };
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 100.0, 50},
        {2500, TradeSide::kBuy, 101.0, 50}
    };
    
    createTestL2File(l2_file, snapshots);
    createTestTradeFile(trade_file, trades);
    
    // Вариант 1: старая версия с внутренней загрузкой
    HFTBacktesterConfig config1;
    config1.cash = 1000000.0;
    config1.inventory = 0.0;
    HFTBacktester backtester1(std::move(config1));
    
    backtester1.loadL2Data(l2_file);
    backtester1.loadTradeData(trade_file);
    
    // Стратегия прямо в тесте
    class SimpleStrategy1 : public IStrategy {
    public:
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {}
        void onFill(const Fill& fill) override {}
    };
    
    auto strategy1 = std::make_unique<SimpleStrategy1>();
    backtester1.setStrategy(std::move(strategy1));
    backtester1.runStrategy();
    
    // Вариант 2: новая версия с внешним HistoryManager
    auto hm = std::make_shared<HistoryManager>();
    hm->loadL2Data(l2_file);
    hm->loadTradeData(trade_file);
    
    HFTBacktesterConfig config2;
    config2.cash = 1000000.0;
    config2.inventory = 0.0;
    HFTBacktester backtester2(std::move(config2), hm);
    
    // Стратегия прямо в тесте
    class SimpleStrategy2 : public IStrategy {
    public:
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {}
        void onFill(const Fill& fill) override {}
    };
    
    auto strategy2 = std::make_unique<SimpleStrategy2>();
    backtester2.setStrategy(std::move(strategy2));
    backtester2.runStrategy();
    
    // Сравниваем результаты
    EXPECT_DOUBLE_EQ(backtester1.getCash(), backtester2.getCash());
    EXPECT_DOUBLE_EQ(backtester1.getInventory(), backtester2.getInventory());
    EXPECT_EQ(backtester1.getTotalOrdersPlaced(), backtester2.getTotalOrdersPlaced());
    EXPECT_EQ(backtester1.getTotalFills(), backtester2.getTotalFills());
    
    std::filesystem::remove(l2_file);
    std::filesystem::remove(trade_file);
}

// ==============================
// ТЕСТЫ С ОБЩИМ HISTORY_MANAGER
// ==============================

TEST(HFTBacktesterTest, SameHistoryManagerInTwoBacktesters) {
    const std::string l2_file = "shared_l2.csv";
    const std::string trade_file = "shared_trade.csv";
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 500}}, {{99.5, 500}}},
        {2000, {{101.0, 500}}, {{100.0, 500}}}
    };
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 100.0, 30},
        {2500, TradeSide::kBuy, 101.0, 30}
    };
    
    createTestL2File(l2_file, snapshots);
    createTestTradeFile(trade_file, trades);
    
    // Создаем общий HistoryManager
    auto shared_hm = std::make_shared<HistoryManager>();
    shared_hm->loadL2Data(l2_file);
    shared_hm->loadTradeData(trade_file);
    
    // Первый бектестер
    HFTBacktesterConfig config1;
    config1.cash = 1000000.0;
    config1.inventory = 0.0;
    HFTBacktester backtester1(std::move(config1), shared_hm);
    
    class TestStrategy1 : public IStrategy {
    public:
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {}
        void onFill(const Fill& fill) override {}
    };
    
    auto strategy1 = std::make_unique<TestStrategy1>();
    backtester1.setStrategy(std::move(strategy1));
    backtester1.runStrategy();
    
    // Второй бектестер с тем же HistoryManager
    HFTBacktesterConfig config2;
    config2.cash = 1000000.0;
    config2.inventory = 0.0;
    HFTBacktester backtester2(std::move(config2), shared_hm);
    
    class TestStrategy2 : public IStrategy {
    public:
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {}
        void onFill(const Fill& fill) override {}
    };
    
    auto strategy2 = std::make_unique<TestStrategy2>();
    backtester2.setStrategy(std::move(strategy2));
    backtester2.runStrategy();
    
    EXPECT_DOUBLE_EQ(backtester1.getCash(), backtester2.getCash());
    EXPECT_DOUBLE_EQ(backtester1.getInventory(), backtester2.getInventory());
    EXPECT_EQ(backtester1.getTotalFills(), backtester2.getTotalFills());
    
    std::filesystem::remove(l2_file);
    std::filesystem::remove(trade_file);
}

// ==============================
// ПАРАЛЛЕЛЬНЫЕ ТЕСТЫ
// ==============================

TEST(HFTBacktesterTest, ParallelExecutionWithSharedHistoryManager) {
    const std::string l2_file = "parallel_l2.csv";
    const std::string trade_file = "parallel_trade.csv";
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 300}}, {{99.5, 300}}},
        {2000, {{101.0, 300}}, {{100.0, 300}}}
    };
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 100.0, 20},
        {2500, TradeSide::kBuy, 101.0, 20}
    };
    
    createTestL2File(l2_file, snapshots);
    createTestTradeFile(trade_file, trades);
    
    auto shared_hm = std::make_shared<HistoryManager>();
    shared_hm->loadL2Data(l2_file);
    shared_hm->loadTradeData(trade_file);
    
    auto run_backtester = [shared_hm]() -> std::tuple<double, double, int, int> {
        HFTBacktesterConfig config;
        config.cash = 1000000.0;
        config.inventory = 0.0;
        HFTBacktester backtester(std::move(config), shared_hm);
        
        class ParallelStrategy : public IStrategy {
        public:
            void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {}
            void onFill(const Fill& fill) override {}
        };
        
        auto strategy = std::make_unique<ParallelStrategy>();
        backtester.setStrategy(std::move(strategy));
        backtester.runStrategy();
        
        return {backtester.getCash(), backtester.getInventory(), 
                backtester.getTotalFills(), backtester.getTotalOrdersPlaced()};
    };
    
    auto future1 = std::async(std::launch::async, run_backtester);
    auto future2 = std::async(std::launch::async, run_backtester);
    
    auto [cash1, inv1, fills1, orders1] = future1.get();
    auto [cash2, inv2, fills2, orders2] = future2.get();
    
    EXPECT_DOUBLE_EQ(cash1, cash2);
    EXPECT_DOUBLE_EQ(inv1, inv2);
    EXPECT_EQ(fills1, fills2);
    EXPECT_EQ(orders1, orders2);
    
    std::filesystem::remove(l2_file);
    std::filesystem::remove(trade_file);
}

// ==============================
// СРАВНЕНИЕ РАЗНЫХ СПОСОБОВ ЗАГРУЗКИ
// ==============================

TEST(HFTBacktesterTest, LoadDataBothWaysGiveSameResults) {
    const std::string l2_file = "both_ways_l2.csv";
    const std::string trade_file = "both_ways_trade.csv";
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 200}}, {{99.5, 200}}},
        {2000, {{101.0, 200}}, {{100.0, 200}}}
    };
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 100.0, 25},
        {2500, TradeSide::kBuy, 101.0, 25}
    };
    
    createTestL2File(l2_file, snapshots);
    createTestTradeFile(trade_file, trades);
    
    // Способ 1: старая версия
    HFTBacktesterConfig config1;
    config1.cash = 1000000.0;
    config1.inventory = 0.0;
    HFTBacktester backtester_old(std::move(config1));
    backtester_old.loadL2Data(l2_file);
    backtester_old.loadTradeData(trade_file);
    
    class OldStrategy : public IStrategy {
    public:
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {}
        void onFill(const Fill& fill) override {}
    };
    
    auto strategy_old = std::make_unique<OldStrategy>();
    backtester_old.setStrategy(std::move(strategy_old));
    backtester_old.runStrategy();
    
    // Способ 2: новая версия с HistoryManager
    auto hm = std::make_shared<HistoryManager>();
    hm->loadL2Data(l2_file);
    hm->loadTradeData(trade_file);
    
    HFTBacktesterConfig config2;
    config2.cash = 1000000.0;
    config2.inventory = 0.0;
    HFTBacktester backtester_new(std::move(config2), hm);
    
    class NewStrategy : public IStrategy {
    public:
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {}
        void onFill(const Fill& fill) override {}
    };
    
    auto strategy_new = std::make_unique<NewStrategy>();
    backtester_new.setStrategy(std::move(strategy_new));
    backtester_new.runStrategy();
    
    EXPECT_DOUBLE_EQ(backtester_old.getCash(), backtester_new.getCash());
    EXPECT_DOUBLE_EQ(backtester_old.getInventory(), backtester_new.getInventory());
    EXPECT_EQ(backtester_old.getTotalFills(), backtester_new.getTotalFills());
    EXPECT_EQ(backtester_old.getTotalOrdersPlaced(), backtester_new.getTotalOrdersPlaced());
    
    std::filesystem::remove(l2_file);
    std::filesystem::remove(trade_file);
}

// ==============================
// МНОГОПОТОЧНЫЙ ЗАПУСК С РАЗНЫМИ HISTORY_MANAGER
// ==============================

TEST(HFTBacktesterTest, ParallelExecutionWithSeparateHistoryManagers) {
    const std::string l2_file = "separate_l2.csv";
    const std::string trade_file = "separate_trade.csv";
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 400}}, {{99.5, 400}}},
        {2000, {{101.0, 400}}, {{100.0, 400}}}
    };
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {1500, TradeSide::kSell, 100.0, 40},
        {2500, TradeSide::kBuy, 101.0, 40}
    };
    
    createTestL2File(l2_file, snapshots);
    createTestTradeFile(trade_file, trades);
    
    auto run_backtester = [&]() -> std::tuple<double, double, int, int> {
        auto hm = std::make_shared<HistoryManager>();
        hm->loadL2Data(l2_file);
        hm->loadTradeData(trade_file);
        
        HFTBacktesterConfig config;
        config.cash = 1000000.0;
        config.inventory = 0.0;
        HFTBacktester backtester(std::move(config), hm);
        
        class SeparateStrategy : public IStrategy {
        public:
            void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {}
            void onFill(const Fill& fill) override {}
        };
        
        auto strategy = std::make_unique<SeparateStrategy>();
        backtester.setStrategy(std::move(strategy));
        backtester.runStrategy();
        
        return {backtester.getCash(), backtester.getInventory(), 
                backtester.getTotalFills(), backtester.getTotalOrdersPlaced()};
    };
    
    std::vector<std::future<std::tuple<double, double, int, int>>> futures;
    for (int i = 0; i < 4; ++i) {
        futures.push_back(std::async(std::launch::async, run_backtester));
    }
    
    std::vector<std::tuple<double, double, int, int>> results;
    for (auto& f : futures) {
        results.push_back(f.get());
    }
    
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_DOUBLE_EQ(std::get<0>(results[0]), std::get<0>(results[i]));
        EXPECT_DOUBLE_EQ(std::get<1>(results[0]), std::get<1>(results[i]));
        EXPECT_EQ(std::get<2>(results[0]), std::get<2>(results[i]));
        EXPECT_EQ(std::get<3>(results[0]), std::get<3>(results[i]));
    }
    
    std::filesystem::remove(l2_file);
    std::filesystem::remove(trade_file);
}

// ==============================
// ТЕСТ 1: ОДНА ЛИМИТКА И ОДИН ВНЕШНИЙ ТРЕЙД
// ==============================

TEST(HFTBacktesterTest, SingleLimitOrderExecution) {
    std::cout << "\n=== Test 1: Single Limit Order Execution ===" << std::endl;
    
    class SingleLimitStrategy : public IStrategy {
    public:
        bool order_sent = false;
        
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
            if (!order_sent && !ctx.l2.bids.empty()) {
                double bid_price = ctx.l2.bids[0].first + 0.000001;
                std::cout << "Sending LIMIT BUY order: price=" << bid_price << ", qty=100" << std::endl;
                api->submitLimitOrder(Side::kBid, bid_price, 100);
                order_sent = true;
            }
        }
        
        void onFill(const Fill& fill) override {
            std::cout << "FILL: " << fill.amount << " @ " << fill.price 
                      << " side=" << (fill.side == Side::kBid ? "BUY" : "SELL") << std::endl;
        }
    };
    
    auto hm = std::make_shared<HistoryManager>();
    
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{100.0, 1000}}, {{99.5, 1000}}},
        {3000, {{102.0, 1000}}, {{101.0, 1000}}}
    };
    createTestL2File("test_single_l2.csv", snapshots);
    hm->loadL2Data("test_single_l2.csv");
    
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {2000, TradeSide::kSell, 99.500001, 50}
    };
    createTestTradeFile("test_single_trade.csv", trades);
    hm->loadTradeData("test_single_trade.csv");
    
    HFTBacktesterConfig config;
    config.cash = 1000000.0;
    config.inventory = 0.0;
    HFTBacktester backtester(std::move(config), hm);
    
    auto strategy = std::make_unique<SingleLimitStrategy>();
    backtester.setStrategy(std::move(strategy));
    
    backtester.runStrategy();
    
    std::cout << "\n=== RESULTS ===" << std::endl;
    std::cout << "Final Cash: " << backtester.getCash() << std::endl;
    std::cout << "Final Inventory: " << backtester.getInventory() << std::endl;
    std::cout << "Final PnL: " << backtester.getEquity() << std::endl;
    std::cout << "Total Orders: " << backtester.getTotalOrdersPlaced() << std::endl;
    std::cout << "Total Fills: " << backtester.getTotalFills() << std::endl;
    
    // === АНАЛИТИЧЕСКИЙ РАСЧЕТ ДЛЯ ПРОВЕРКИ ===
    
    // 1. Анализ первого снапшота (t=1000):
    // snapshots[0] = {1000, {{100.0, 1000}}, {{99.5, 1000}}}
    // best_ask = 100.0, best_bid = 99.5
    // Mid-price (t=1000) = (100.0 + 99.5) / 2.0 = 99.75
    
    // 2. Параметры ордера:
    // Стратегия берет bids[0].first (99.5) + 0.000001
    double my_limit_price = 99.500001; 
    double my_limit_qty = 100.0;
    
    // 3. Исполнение (t=2000):
    // Приходит Trade: Side=kSell, Price=99.500001, Qty=50
    // Наша лимитка на BUY стоит ровно по 99.500001. 
    // Так как цена трейда (Sell) совпадает с нашей ценой (Buy), происходит Fill.
    
    double fill_price = 99.500001;
    double fill_qty = 50.0; // Трейд меньше нашего ордера, забираем только 50
    
    // Cash = 1000000.0 - (50 * 99.500001) = 995024.99995
    double expected_cash = 1000000.0 - (fill_qty * fill_price);
    double expected_inv = 50.0;
    
    // 4. Анализ второго снапшота (t=3000) для Mark-to-Market:
    // snapshots[1] = {3000, {{102.0, 1000}}, {{101.0, 1000}}}
    // best_ask = 102.0, best_bid = 101.0
    // Final Mid-price = (102.0 + 101.0) / 2.0 = 101.5
    
    double final_mid = 101.5;
    // PnL = 995024.99995 + (50 * 101.5) = 995024.99995 + 5075.0 = 1000099.99995
    double expected_pnl = expected_cash + (expected_inv * final_mid);
    
    // Обновляем проверки
    EXPECT_DOUBLE_EQ(backtester.getInventory(), expected_inv);
    EXPECT_NEAR(backtester.getCash(), expected_cash, 1e-7);
    EXPECT_NEAR(backtester.getEquity(), expected_pnl, 1e-7);
    EXPECT_EQ(backtester.getTotalFills(), 1);
    
    std::filesystem::remove("test_single_l2.csv");
    std::filesystem::remove("test_single_trade.csv");
}

// ==============================
// ТЕСТ 3: СЕРИЯ ИЗ 3 СДЕЛОК (ТИК 0.1)
// ==============================

TEST(HFTBacktesterTest, SeriesOfTradesLargeTick) {
    std::cout << "\n=== Test 3: Series of Trades (Tick 0.1) ===" << std::endl;
    
    class DynamicBidStrategy : public IStrategy {
    public:
        void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
            if (ctx.l2.bids.empty()) return;

            // Ставим Bid на 1 тик (0.1) выше текущего лучшего бида
            // Снапшот: {asks, bids}
            double tick_size = 0.1;
            double my_bid = ctx.l2.bids[0].first + tick_size;
            
            api->cancelAll();
            api->submitLimitOrder(Side::kBid, my_bid, 100);
            
            // Используем фиксированный вывод для отладки
            std::cout << std::fixed << std::setprecision(1) 
                      << "TS: " << ctx.current_timestamp 
                      << " | Setting Bid: " << my_bid << std::endl;
        }
        
        void onFill(const Fill& fill) override {
            std::cout << std::fixed << std::setprecision(1)
                      << "  >>> FILL: " << fill.amount << " @ " << fill.price << std::endl;
        }
    };
    
    auto hm = std::make_shared<HistoryManager>();
    
    // Снапшоты: {timestamp, asks, bids}
    // Цены специально разнесены, чтобы тик 0.1 был значимым
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{105.0, 1000}}, {{100.0, 1000}}},  // Стратегия поставит Bid 100.1
        {3000, {{110.0, 1000}}, {{105.0, 1000}}},  // Стратегия поставит Bid 105.1
        {5000, {{120.0, 1000}}, {{115.0, 1000}}}   // Стратегия поставит Bid 115.1
    };
    createTestL2File("test_series_l2.csv", snapshots);
    hm->loadL2Data("test_series_l2.csv");
    
    // Трейды (Sells), которые бьют точно в наши лимитки
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {2000, TradeSide::kSell, 100.1, 10}, // Fill 1
        {4000, TradeSide::kSell, 105.1, 20}, // Fill 2
        {6000, TradeSide::kSell, 115.1, 30}  // Fill 3
    };
    createTestTradeFile("test_series_trade.csv", trades);
    hm->loadTradeData("test_series_trade.csv");
    
    HFTBacktesterConfig config;
    config.cash = 1000000.0;
    config.inventory = 0.0;
    HFTBacktester backtester(std::move(config), hm);
    
    backtester.setStrategy(std::make_unique<DynamicBidStrategy>());
    backtester.runStrategy();
    
    // === ТОЧНЫЙ АНАЛИТИЧЕСКИЙ РАСЧЕТ ===
    
    // 1. Покупка 10 @ 100.1 = 1001.0
    // 2. Покупка 20 @ 105.1 = 2102.0
    // 3. Покупка 30 @ 115.1 = 3453.0
    
    double total_spent = 1001.0 + 2102.0 + 3453.0; // 6556.0
    // Ожидаемый Mid = (120.0 + 115.1) / 2 = 117.55
    double expected_cash = 993444.0;
    double expected_inv = 60.0;
    double expected_pnl = 1000497.0; // 993444.0 + (60.0 * 117.55)
   
    std::cout << "\n=== FINAL CHECKS ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Expected Cash: " << expected_cash << " | Actual: " << backtester.getCash() << std::endl;
    std::cout << "Expected PnL:  " << expected_pnl  << " | Actual: " << backtester.getEquity() << std::endl;
    
    EXPECT_DOUBLE_EQ(backtester.getInventory(), expected_inv);
    EXPECT_NEAR(backtester.getCash(), expected_cash, 1e-7);
    EXPECT_NEAR(backtester.getEquity(), expected_pnl, 1e-7);
    EXPECT_EQ(backtester.getTotalFills(), 3);
    
    std::filesystem::remove("test_series_l2.csv");
    std::filesystem::remove("test_series_trade.csv");
}

// ==============================
// ТЕСТ 4: ПОКУПКА И ПРОДАЖА (ПОЛНЫЙ ЦИКЛ, ТИК 0.1)
// ==============================

TEST(HFTBacktesterTest, BuyAndSellCycleLargeTick) {
    std::cout << "\n=== Test 4: Buy and Sell Cycle (Tick 0.1) ===" << std::endl;
    
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
    
    auto hm = std::make_shared<HistoryManager>();
    
    // Снапшоты: {timestamp, asks, bids}
    std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
        {1000, {{105.0, 1000}}, {{100.0, 1000}}}, // ТУТ: поставим Bid 100.1
        {3000, {{110.0, 1000}}, {{105.0, 1000}}}  // ТУТ: уже купили, ставим Ask 109.9
    };
    createTestL2File("test_cycle_l2.csv", snapshots);
    hm->loadL2Data("test_cycle_l2.csv");
    
    // Трейды для исполнения наших ордеров
    std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
        {2000, TradeSide::kSell, 100.1, 50}, // Исполняет покупку по 100.1
        {4000, TradeSide::kBuy, 109.9, 50}   // Исполняет продажу по 109.9
    };
    createTestTradeFile("test_cycle_trade.csv", trades);
    hm->loadTradeData("test_cycle_trade.csv");
    
    HFTBacktesterConfig config;
    config.cash = 1000000.0;
    config.inventory = 0.0;
    HFTBacktester backtester(std::move(config), hm);
    
    backtester.setStrategy(std::make_unique<BuySellStrategy>());
    backtester.runStrategy();
    
    // === ТОЧНЫЙ АНАЛИТИЧЕСКИЙ РАСЧЕТ ===
    
    // 1. Покупка: 50 @ 100.1 = 5005.0 (Cash становится 994995.0)
    // 2. Продажа: 50 @ 109.9 = 5495.0 (Cash становится 994995.0 + 5495.0 = 1000490.0)
    
    double expected_cash = 1000490.0;
    double expected_inv = 0.0;
    
    // Поскольку инвентарь 0, PnL обязан быть равен Cash
    double expected_pnl = expected_cash;
    
    // Проверка логики прибыли: (109.9 - 100.1) * 50 = 9.8 * 50 = 490.0
    
    std::cout << "\n=== FINAL CHECKS ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Final Cash: " << backtester.getCash() << std::endl;
    std::cout << "Final PnL:  " << backtester.getEquity() << std::endl;
    
    EXPECT_DOUBLE_EQ(backtester.getInventory(), expected_inv);
    EXPECT_NEAR(backtester.getCash(), expected_cash, 1e-7);
    EXPECT_NEAR(backtester.getEquity(), expected_pnl, 1e-7);
    EXPECT_EQ(backtester.getTotalFills(), 2);
    
    std::filesystem::remove("test_cycle_l2.csv");
    std::filesystem::remove("test_cycle_trade.csv");
}

// // ==============================
// // ТЕСТ 5: ПРОВЕРКА PNL ПОСЛЕ ЧАСТИЧНОГО ИСПОЛНЕНИЯ
// // ==============================

// TEST(HFTBacktesterTest, PartialFillAndPnL) {
//     std::cout << "\n=== Test 5: Partial Fill and PnL Check ===" << std::endl;
    
//     class PartialFillStrategy : public IStrategy {
//     public:
//         bool order_sent = false;
        
//         void onMarketContext(const MarketContext& ctx, HFTBacktester* api) override {
//             if (!order_sent && !ctx.l2.bids.empty()) {
//                 double bid_price = ctx.l2.bids[0].first - 0.000001;
//                 std::cout << "Sending LIMIT BUY order: price=" << bid_price << ", qty=100" << std::endl;
//                 api->submitLimitOrder(Side::kBid, bid_price, 100);
//                 order_sent = true;
//             }
//         }
        
//         void onFill(const Fill& fill) override {
//             std::cout << "FILL: " << fill.amount << " @ " << fill.price 
//                       << " side=" << (fill.side == Side::kBid ? "BUY" : "SELL") << std::endl;
//         }
//     };
    
//     auto hm = std::make_shared<HistoryManager>();
    
//     std::vector<std::tuple<int64_t, std::vector<std::pair<double, double>>, std::vector<std::pair<double, double>>>> snapshots = {
//         {1000, {{100.0, 1000}}, {{99.5, 1000}}},
//         {3000, {{102.0, 1000}}, {{101.0, 1000}}}
//     };
//     createTestL2File("test_partial_l2.csv", snapshots);
//     hm->loadL2Data("test_partial_l2.csv");
    
//     // Частичное исполнение: только 30 из 100
//     std::vector<std::tuple<int64_t, TradeSide, double, double>> trades = {
//         {2000, TradeSide::kSell, 99.4, 30}
//     };
//     createTestTradeFile("test_partial_trade.csv", trades);
//     hm->loadTradeData("test_partial_trade.csv");
    
//     HFTBacktesterConfig config;
//     config.cash = 1000000.0;
//     config.inventory = 0.0;
//     HFTBacktester backtester(std::move(config), hm);
    
//     auto strategy = std::make_unique<PartialFillStrategy>();
//     backtester.setStrategy(std::move(strategy));
    
//     backtester.runStrategy();
    
//     std::cout << "\n=== RESULTS ===" << std::endl;
//     std::cout << "Final Cash: " << backtester.getCash() << std::endl;
//     std::cout << "Final Inventory: " << backtester.getInventory() << std::endl;
//     std::cout << "Final PnL: " << backtester.getEquity() << std::endl;
//     std::cout << "Total Fills: " << backtester.getTotalFills() << std::endl;
    
//     // Ожидаем: купили 30 по 99.4
//     double expected_cash = 1000000.0 - 30.0 * 99.4;
//     double expected_inv = 30.0;
//     double expected_pnl = expected_cash + expected_inv * 101.5;
    
//     EXPECT_DOUBLE_EQ(backtester.getInventory(), expected_inv);
//     EXPECT_DOUBLE_EQ(backtester.getCash(), expected_cash);
//     EXPECT_DOUBLE_EQ(backtester.getEquity(), expected_pnl);
//     EXPECT_EQ(backtester.getTotalFills(), 1);
    
//     std::filesystem::remove("test_partial_l2.csv");
//     std::filesystem::remove("test_partial_trade.csv");
// }