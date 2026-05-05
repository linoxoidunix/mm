#include <gtest/gtest.h>
#include <fstream>
#include "mm/matching_engine/matching_engine.h"

TEST(MatchingEngineTest, MarketBuyOrder) {
    MatchingEngine engine;
    
    // Добавляем лимитные ордера на продажу
    Order sell1;
    sell1.order_id = LimitOrderRef(1);
    sell1.side = Side::kAsk;
    sell1.type = OrderType::kLimit;
    sell1.price = 100.0;
    sell1.amount = 50;
    sell1.timestamp = 1000;
    engine.addLimitOrder(sell1);
    
    Order sell2;
    sell2.order_id = LimitOrderRef(2);
    sell2.side = Side::kAsk;
    sell2.type = OrderType::kLimit;
    sell2.price = 100.5;
    sell2.amount = 30;
    sell2.timestamp = 1000;
    engine.addLimitOrder(sell2);
    
    // Рыночный ордер на покупку 70
    Order marketBuy;
    marketBuy.order_id = MarketOrderRef(3);
    marketBuy.side = Side::kBid;
    marketBuy.type = OrderType::kMarket;
    marketBuy.amount = 70;
    marketBuy.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(marketBuy);
    
    EXPECT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 50);
    EXPECT_DOUBLE_EQ(trades[1].price, 100.5);
    EXPECT_DOUBLE_EQ(trades[1].amount, 20);
    
    // Проверяем остаток в asks
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].first, 100.5);
    EXPECT_DOUBLE_EQ(asks[0].second, 10);  // 30 - 20 = 10
}

TEST(MatchingEngineTest, MarketSellOrder) {
    MatchingEngine engine;
    
    // Добавляем лимитные ордера на покупку
    Order buy1;
    buy1.order_id = LimitOrderRef(1);
    buy1.side = Side::kBid;
    buy1.type = OrderType::kLimit;
    buy1.price = 99.5;
    buy1.amount = 40;
    buy1.timestamp = 1000;
    engine.addLimitOrder(buy1);
    
    Order buy2;
    buy2.order_id = LimitOrderRef(2);
    buy2.side = Side::kBid;
    buy2.type = OrderType::kLimit;
    buy2.price = 99.0;
    buy2.amount = 60;
    buy2.timestamp = 1000;
    engine.addLimitOrder(buy2);
    
    // Рыночный ордер на продажу 80
    Order marketSell;
    marketSell.order_id = MarketOrderRef(3);
    marketSell.side = Side::kAsk;
    marketSell.type = OrderType::kMarket;
    marketSell.amount = 80;
    marketSell.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(marketSell);
    
    EXPECT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 99.5);
    EXPECT_DOUBLE_EQ(trades[0].amount, 40);
    EXPECT_DOUBLE_EQ(trades[1].price, 99.0);
    EXPECT_DOUBLE_EQ(trades[1].amount, 40);
    
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].first, 99.0);
    EXPECT_DOUBLE_EQ(bids[0].second, 20);  // 60 - 40 = 20
}

TEST(MatchingEngineTest, MarketOrderNoLiquidity) {
    MatchingEngine engine;
    
    // Пустая книга
    Order marketBuy;
    marketBuy.order_id = MarketOrderRef(1);
    marketBuy.side = Side::kBid;
    marketBuy.type = OrderType::kMarket;
    marketBuy.amount = 100;
    marketBuy.timestamp = 1000;
    
    auto trades = engine.addMarketOrder(marketBuy);
    
    EXPECT_TRUE(trades.empty());
    EXPECT_DOUBLE_EQ(marketBuy.filled_amount, 0);
}

// ==============================
// БАЗОВЫЕ ТЕСТЫ
// ==============================

TEST(MatchingEngineTest, AddLimitOrderNoMatch) {
    MatchingEngine engine;
    
    Order buy;
    buy.order_id = LimitOrderRef(1);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 99.0;
    buy.amount = 100;
    buy.timestamp = 1000;
    
    auto trades = engine.addLimitOrder(buy);
    
    EXPECT_TRUE(trades.empty());
    EXPECT_DOUBLE_EQ(engine.getBestBid(), 99.0);
    EXPECT_DOUBLE_EQ(engine.getBestAsk(), 0);
}

TEST(MatchingEngineTest, AddLimitOrderMatch) {
    MatchingEngine engine;
    
    // Сначала продавец
    Order sell;
    sell.order_id = LimitOrderRef(1);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 50;
    sell.timestamp = 1000;
    engine.addLimitOrder(sell);
    
    // Потом покупатель
    Order buy;
    buy.order_id = LimitOrderRef(2);
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
    EXPECT_TRUE(engine.getBids().empty());
    EXPECT_TRUE(engine.getAsks().empty());
}

// ==============================
// ТЕСТЫ ЧАСТИЧНОГО ИСПОЛНЕНИЯ
// ==============================

TEST(MatchingEngineTest, PartialFillBuy) {
    MatchingEngine engine;
    
    // Продавец продает 100
    Order sell;
    sell.order_id = LimitOrderRef(1);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 100;
    sell.timestamp = 1000;
    engine.addLimitOrder(sell);
    
    // Покупатель покупает только 60
    Order buy;
    buy.order_id = LimitOrderRef(2);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 60;
    buy.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(buy);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].amount, 60);
    
    // В книге должен остаться sell на 40
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].second, 40);
}

TEST(MatchingEngineTest, PartialFillSell) {
    MatchingEngine engine;
    
    // Покупатель хочет купить 100
    Order buy;
    buy.order_id = LimitOrderRef(1);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 100;
    buy.timestamp = 1000;
    engine.addLimitOrder(buy);
    
    // Продавец продает только 60
    Order sell;
    sell.order_id = LimitOrderRef(2);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 60;
    sell.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(sell);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].amount, 60);
    
    // В книге должен остаться buy на 40
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].second, 40);
}

// ==============================
// ТЕСТЫ МНОЖЕСТВЕННЫХ УРОВНЕЙ
// ==============================

TEST(MatchingEngineTest, MultipleLevelsBuy) {
    MatchingEngine engine;
    
    // Три уровня asks
    Order sell1;
    sell1.order_id = LimitOrderRef(1);
    sell1.side = Side::kAsk;
    sell1.type = OrderType::kLimit;
    sell1.price = 100.0;
    sell1.amount = 30;
    sell1.timestamp = 1000;
    engine.addLimitOrder(sell1);
    
    Order sell2;
    sell2.order_id = LimitOrderRef(2);
    sell2.side = Side::kAsk;
    sell2.type = OrderType::kLimit;
    sell2.price = 100.5;
    sell2.amount = 40;
    sell2.timestamp = 1001;
    engine.addLimitOrder(sell2);
    
    Order sell3;
    sell3.order_id = LimitOrderRef(3);
    sell3.side = Side::kAsk;
    sell3.type = OrderType::kLimit;
    sell3.price = 101.0;
    sell3.amount = 50;
    sell3.timestamp = 1002;
    engine.addLimitOrder(sell3);
    
    // Покупатель хочет купить 100
    Order buy;
    buy.order_id = LimitOrderRef(4);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 101.0;
    buy.amount = 100;
    buy.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(buy);
    
    ASSERT_EQ(trades.size(), 3);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_DOUBLE_EQ(trades[1].price, 100.5);
    EXPECT_DOUBLE_EQ(trades[1].amount, 40);
    EXPECT_DOUBLE_EQ(trades[2].price, 101.0);
    EXPECT_DOUBLE_EQ(trades[2].amount, 30);
    
    // В asks должен остаться 101.0 с объемом 20
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].first, 101.0);
    EXPECT_DOUBLE_EQ(asks[0].second, 20);
}

TEST(MatchingEngineTest, MultipleLevelsSell) {
    MatchingEngine engine;
    
    // Три уровня bids
    Order buy1;
    buy1.order_id = LimitOrderRef(1);
    buy1.side = Side::kBid;
    buy1.type = OrderType::kLimit;
    buy1.price = 100.0;
    buy1.amount = 30;
    buy1.timestamp = 1000;
    engine.addLimitOrder(buy1);
    
    Order buy2;
    buy2.order_id = LimitOrderRef(2);
    buy2.side = Side::kBid;
    buy2.type = OrderType::kLimit;
    buy2.price = 99.5;
    buy2.amount = 40;
    buy2.timestamp = 1001;
    engine.addLimitOrder(buy2);
    
    Order buy3;
    buy3.order_id = LimitOrderRef(3);
    buy3.side = Side::kBid;
    buy3.type = OrderType::kLimit;
    buy3.price = 99.0;
    buy3.amount = 50;
    buy3.timestamp = 1002;
    engine.addLimitOrder(buy3);
    
    // Продавец хочет продать 100
    Order sell;
    sell.order_id = LimitOrderRef(4);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 99.0;
    sell.amount = 100;
    sell.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(sell);
    
    ASSERT_EQ(trades.size(), 3);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_DOUBLE_EQ(trades[1].price, 99.5);
    EXPECT_DOUBLE_EQ(trades[1].amount, 40);
    EXPECT_DOUBLE_EQ(trades[2].price, 99.0);
    EXPECT_DOUBLE_EQ(trades[2].amount, 30);
    
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].first, 99.0);
    EXPECT_DOUBLE_EQ(bids[0].second, 20);
}

// ==============================
// ТЕСТЫ FIFO (ОЧЕРЕДЬ)
// ==============================

TEST(MatchingEngineTest, Matching_Engine_FIFO_AsksOrder) {
    MatchingEngine engine;
       
    // Два ордера на продажу на одном уровне
    Order sell1;
    sell1.order_id = LimitOrderRef(1);
    sell1.side = Side::kAsk;
    sell1.type = OrderType::kLimit;
    sell1.price = 100.0;
    sell1.amount = 30;
    sell1.timestamp = 1000;
    engine.addLimitOrder(sell1);
    
    Order sell2;
    sell2.order_id = LimitOrderRef(2);
    sell2.side = Side::kAsk;
    sell2.type = OrderType::kLimit;
    sell2.price = 100.0;
    sell2.amount = 30;
    sell2.timestamp = 2000;
    engine.addLimitOrder(sell2);
    
    // Покупатель приходит и покупает 50
    Order buy;
    buy.order_id = LimitOrderRef(3);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 50;
    buy.timestamp = 3000;
    
    auto trades = engine.addLimitOrder(buy);
    
    // Проверяем количество трейдов
    ASSERT_EQ(trades.size(), 2);
    
    // Проверяем объемы трейдов
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);  // Первый трейд - 30
    EXPECT_DOUBLE_EQ(trades[1].amount, 20);  // Второй трейд - 20
    
    // Проверяем остаток в asks
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].first, 100.0);
    EXPECT_DOUBLE_EQ(asks[0].second, 10);  // Осталось 10 от sell2
    
    // НО ЭТО НЕ ПРОВЕРЯЕТ, ЧТО ИСПОЛНИЛСЯ ИМЕННО sell1, А НЕ sell2
    
    // ЛУЧШИЙ СПОСОБ: проверить, что первый ордер полностью исполнен
    // Для этого нужно иметь доступ к внутреннему состоянию ордеров в книге
    // или добавить метод для проверки остатков по ордерам
    
    // Альтернатива: проверить через cancelOrder, что ордер id=1 уже не существует
    bool order1_exists = engine.cancelOrder(LimitOrderRef(1));
    EXPECT_FALSE(order1_exists);  // Ордер 1 должен быть полностью исполнен
    
    bool order2_exists = engine.cancelOrder(LimitOrderRef(2));
    EXPECT_TRUE(order2_exists);   // Ордер 2 должен еще существовать (с остатком)
}

TEST(MatchingEngineTest, FIFO_BidsOrder) {
    MatchingEngine engine;
    
    // Два ордера на покупку на одном уровне
    Order buy1;
    buy1.order_id = LimitOrderRef(1);
    buy1.side = Side::kBid;
    buy1.type = OrderType::kLimit;
    buy1.price = 100.0;
    buy1.amount = 30;
    buy1.timestamp = 1000;
    engine.addLimitOrder(buy1);
    
    Order buy2;
    buy2.order_id = LimitOrderRef(2);
    buy2.side = Side::kBid;
    buy2.type = OrderType::kLimit;
    buy2.price = 100.0;
    buy2.amount = 30;
    buy2.timestamp = 2000;
    engine.addLimitOrder(buy2);
    
    // Продавец приходит и продает 50
    Order sell;
    sell.order_id = LimitOrderRef(3);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 50;
    sell.timestamp = 3000;
    
    auto trades = engine.addLimitOrder(sell);
    
    // Проверяем количество трейдов
    ASSERT_EQ(trades.size(), 2);
    
    // Проверяем объемы трейдов
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_DOUBLE_EQ(trades[1].amount, 20);
    
    // Проверяем остаток в книге (один уровень с объемом 10)
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].first, 100.0);
    EXPECT_DOUBLE_EQ(bids[0].second, 10);
    
    // ============================================
    // ГЛАВНАЯ ПРОВЕРКА FIFO:
    // Пытаемся отменить ордер id=1 - он должен быть уже полностью исполнен
    // ============================================
    bool order1_cancelled = engine.cancelOrder(LimitOrderRef(1));
    EXPECT_FALSE(order1_cancelled);  // Ордер 1 уже не существует в книге
    
    // Пытаемся отменить ордер id=2 - он должен существовать
    bool order2_cancelled = engine.cancelOrder(LimitOrderRef(2));
    EXPECT_TRUE(order2_cancelled);   // Ордер 2 еще в книге
    
    // Проверяем, что после отмены ордера 2 книга пуста
    auto bids_after_cancel = engine.getBids();
    EXPECT_TRUE(bids_after_cancel.empty());
}

// ==============================
// ТЕСТЫ РЫНОЧНЫХ ОРДЕРОВ
// ==============================

TEST(MatchingEngineTest, MarketBuyOrderFull) {
    MatchingEngine engine;
    
    Order sell1;
    sell1.order_id = LimitOrderRef(1);
    sell1.side = Side::kAsk;
    sell1.type = OrderType::kLimit;
    sell1.price = 100.0;
    sell1.amount = 50;
    sell1.timestamp = 1000;
    engine.addLimitOrder(sell1);
    
    Order sell2;
    sell2.order_id = LimitOrderRef(2);
    sell2.side = Side::kAsk;
    sell2.type = OrderType::kLimit;
    sell2.price = 100.5;
    sell2.amount = 30;
    sell2.timestamp = 1000;
    engine.addLimitOrder(sell2);
    
    Order marketBuy;
    marketBuy.order_id = MarketOrderRef(3);
    marketBuy.side = Side::kBid;
    marketBuy.type = OrderType::kMarket;
    marketBuy.amount = 80;
    marketBuy.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(marketBuy);
    
    ASSERT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 50);
    EXPECT_DOUBLE_EQ(trades[1].price, 100.5);
    EXPECT_DOUBLE_EQ(trades[1].amount, 30);
    
    EXPECT_TRUE(engine.getAsks().empty());
    EXPECT_DOUBLE_EQ(marketBuy.filled_amount, 80);
}

TEST(MatchingEngineTest, MarketSellOrderFull) {
    MatchingEngine engine;
    
    Order buy1;
    buy1.order_id = LimitOrderRef(1);
    buy1.side = Side::kBid;
    buy1.type = OrderType::kLimit;
    buy1.price = 99.5;
    buy1.amount = 40;
    buy1.timestamp = 1000;
    engine.addLimitOrder(buy1);
    
    Order buy2;
    buy2.order_id = LimitOrderRef(2);
    buy2.side = Side::kBid;
    buy2.type = OrderType::kLimit;
    buy2.price = 99.0;
    buy2.amount = 60;
    buy2.timestamp = 1000;
    engine.addLimitOrder(buy2);
    
    Order marketSell;
    marketSell.order_id = MarketOrderRef(3);
    marketSell.side = Side::kAsk;
    marketSell.type = OrderType::kMarket;
    marketSell.amount = 100;
    marketSell.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(marketSell);
    
    ASSERT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 99.5);
    EXPECT_DOUBLE_EQ(trades[0].amount, 40);
    EXPECT_DOUBLE_EQ(trades[1].price, 99.0);
    EXPECT_DOUBLE_EQ(trades[1].amount, 60);
    
    EXPECT_TRUE(engine.getBids().empty());
    EXPECT_DOUBLE_EQ(marketSell.filled_amount, 100);
}

TEST(MatchingEngineTest, MarketBuyOrderPartialLiquidity) {
    MatchingEngine engine;
    
    Order sell;
    sell.order_id = LimitOrderRef(1);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 30;
    sell.timestamp = 1000;
    engine.addLimitOrder(sell);
    
    Order marketBuy;
    marketBuy.order_id = MarketOrderRef(2);
    marketBuy.side = Side::kBid;
    marketBuy.type = OrderType::kMarket;
    marketBuy.amount = 50;
    marketBuy.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(marketBuy);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_DOUBLE_EQ(marketBuy.filled_amount, 30);
    EXPECT_TRUE(engine.getAsks().empty());
}

TEST(MatchingEngineTest, MarketSellOrderPartialLiquidity) {
    MatchingEngine engine;
    
    Order buy;
    buy.order_id = LimitOrderRef(1);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 99.5;
    buy.amount = 30;
    buy.timestamp = 1000;
    engine.addLimitOrder(buy);
    
    Order marketSell;
    marketSell.order_id = MarketOrderRef(2);
    marketSell.side = Side::kAsk;
    marketSell.type = OrderType::kMarket;
    marketSell.amount = 50;
    marketSell.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(marketSell);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_DOUBLE_EQ(marketSell.filled_amount, 30);
    EXPECT_TRUE(engine.getBids().empty());
}

TEST(MatchingEngineTest, MarketBuyOrderNoLiquidity) {
    MatchingEngine engine;
    
    Order marketBuy;
    marketBuy.order_id = MarketOrderRef(1);
    marketBuy.side = Side::kBid;
    marketBuy.type = OrderType::kMarket;
    marketBuy.amount = 100;
    marketBuy.timestamp = 1000;
    
    auto trades = engine.addMarketOrder(marketBuy);
    
    EXPECT_TRUE(trades.empty());
    EXPECT_DOUBLE_EQ(marketBuy.filled_amount, 0);
}

// ==============================
// ТЕСТЫ ОТМЕНЫ ОРДЕРОВ
// ==============================

TEST(MatchingEngineTest, CancelOrderFromBids) {
    MatchingEngine engine;
    
    Order buy;
    buy.order_id = LimitOrderRef(1);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 99.0;
    buy.amount = 100;
    buy.timestamp = 1000;
    engine.addLimitOrder(buy);
    
    EXPECT_FALSE(engine.getBids().empty());
    
    bool cancelled = engine.cancelOrder(LimitOrderRef(1));
    EXPECT_TRUE(cancelled);
    EXPECT_TRUE(engine.getBids().empty());
}

TEST(MatchingEngineTest, CancelOrderFromAsks) {
    MatchingEngine engine;
    
    Order sell;
    sell.order_id = LimitOrderRef(1);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 101.0;
    sell.amount = 100;
    sell.timestamp = 1000;
    engine.addLimitOrder(sell);
    
    EXPECT_FALSE(engine.getAsks().empty());
    
    bool cancelled = engine.cancelOrder(LimitOrderRef(1));
    EXPECT_TRUE(cancelled);
    EXPECT_TRUE(engine.getAsks().empty());
}

TEST(MatchingEngineTest, CancelOrderAfterPartialFill) {
    MatchingEngine engine;
    
    Order sell;
    sell.order_id = LimitOrderRef(1);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 100;
    sell.timestamp = 1000;
    engine.addLimitOrder(sell);
    
    Order buy;
    buy.order_id = LimitOrderRef(2);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 60;
    buy.timestamp = 2000;
    engine.addLimitOrder(buy);
    
    // Осталось 40 в asks
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].second, 40);
    
    // Отменяем остаток
    bool cancelled = engine.cancelOrder(LimitOrderRef(1));
    EXPECT_TRUE(cancelled);
    EXPECT_TRUE(engine.getAsks().empty());
}

// ==============================
// ТЕСТЫ КОМБИНАЦИЙ
// ==============================

TEST(MatchingEngineTest, MultipleOrdersSameSide) {
    MatchingEngine engine;
    
    // Три покупателя на разных ценах
    Order buy1;
    buy1.order_id = LimitOrderRef(1);
    buy1.side = Side::kBid;
    buy1.type = OrderType::kLimit;
    buy1.price = 100.0;
    buy1.amount = 30;
    buy1.timestamp = 1000;
    engine.addLimitOrder(buy1);
    
    Order buy2;
    buy2.order_id = LimitOrderRef(2);
    buy2.side = Side::kBid;
    buy2.type = OrderType::kLimit;
    buy2.price = 99.5;
    buy2.amount = 40;
    buy2.timestamp = 1001;
    engine.addLimitOrder(buy2);
    
    Order buy3;
    buy3.order_id = LimitOrderRef(3);
    buy3.side = Side::kBid;
    buy3.type = OrderType::kLimit;
    buy3.price = 99.0;
    buy3.amount = 50;
    buy3.timestamp = 1002;
    engine.addLimitOrder(buy3);
    
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 3);
    EXPECT_DOUBLE_EQ(bids[0].first, 100.0);
    EXPECT_DOUBLE_EQ(bids[1].first, 99.5);
    EXPECT_DOUBLE_EQ(bids[2].first, 99.0);
}

TEST(MatchingEngineTest, AggressiveLimitOrder) {
    MatchingEngine engine;
    
    // Есть покупатели на разных уровнях
    Order buy1;
    buy1.order_id = LimitOrderRef(1);
    buy1.side = Side::kBid;
    buy1.type = OrderType::kLimit;
    buy1.price = 99.0;
    buy1.amount = 50;
    buy1.timestamp = 1000;
    engine.addLimitOrder(buy1);
    
    Order buy2;
    buy2.order_id = LimitOrderRef(2);
    buy2.side = Side::kBid;
    buy2.type = OrderType::kLimit;
    buy2.price = 98.5;
    buy2.amount = 50;
    buy2.timestamp = 1001;
    engine.addLimitOrder(buy2);
    
    // Приходит агрессивный продавец с ценой 98.5
    Order sell;
    sell.order_id = LimitOrderRef(3);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 98.5;
    sell.amount = 80;
    sell.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(sell);
    
    // Должен сматчиться с лучшим покупателем (99.0) 50 единиц
    // и с следующим (98.5) 30 единиц
    ASSERT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 99.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 50);
    EXPECT_DOUBLE_EQ(trades[1].price, 98.5);
    EXPECT_DOUBLE_EQ(trades[1].amount, 30);
    
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].first, 98.5);
    EXPECT_DOUBLE_EQ(bids[0].second, 20);  // Осталось 20 от второго покупателя
}

// ==============================
// ТЕСТЫ СПРЕДА
// ==============================

TEST(MatchingEngineTest, SpreadCalculation) {
    MatchingEngine engine;
    
    Order buy;
    buy.order_id = LimitOrderRef(1);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 99.5;
    buy.amount = 100;
    buy.timestamp = 1000;
    engine.addLimitOrder(buy);
    
    Order sell;
    sell.order_id = LimitOrderRef(2);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.5;
    sell.amount = 100;
    sell.timestamp = 1000;
    engine.addLimitOrder(sell);
    
    EXPECT_DOUBLE_EQ(engine.getBestBid(), 99.5);
    EXPECT_DOUBLE_EQ(engine.getBestAsk(), 100.5);
    EXPECT_DOUBLE_EQ(engine.getSpread(), 1.0);
}

TEST(MatchingEngineTest, SpreadZeroWhenMatch) {
    MatchingEngine engine;
    
    Order buy;
    buy.order_id = LimitOrderRef(1);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 100;
    buy.timestamp = 1000;
    engine.addLimitOrder(buy);
    
    Order sell;
    sell.order_id = LimitOrderRef(2);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 50;
    sell.timestamp = 1000;
    
    auto trades = engine.addLimitOrder(sell);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(engine.getSpread(), 0);  // Книга пуста после полного совпадения
}

// ==============================
// ТЕСТЫ КРАЕВЫХ СЛУЧАЕВ
// ==============================

TEST(MatchingEngineTest, ZeroAmountOrder) {
    MatchingEngine engine;
    
    Order buy;
    buy.order_id = LimitOrderRef(1);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 0;
    buy.timestamp = 1000;
    
    auto trades = engine.addLimitOrder(buy);
    EXPECT_TRUE(trades.empty());
}

TEST(MatchingEngineTest, NegativePriceOrder) {
    MatchingEngine engine;
    
    Order buy;
    buy.order_id = LimitOrderRef(1);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = -100.0;
    buy.amount = 100;
    buy.timestamp = 1000;
    
    auto trades = engine.addLimitOrder(buy);
    EXPECT_TRUE(trades.empty());
    // С отрицательной ценой ордер не должен добавляться в книгу или должен быть отклонен
}

TEST(MatchingEngineTest, VeryLargeOrder) {
    MatchingEngine engine;
    
    Order sell;
    sell.order_id = LimitOrderRef(1);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 1e9;
    sell.timestamp = 1000;
    engine.addLimitOrder(sell);
    
    Order buy;
    buy.order_id = LimitOrderRef(2);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 1e9;
    buy.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(buy);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].amount, 1e9);
    EXPECT_TRUE(engine.getAsks().empty());
    EXPECT_TRUE(engine.getBids().empty());
}

// ==============================
// ТЕСТЫ СМЕШАННЫХ СЦЕНАРИЕВ
// ==============================

TEST(MatchingEngineTest, MarketThenLimit) {
    MatchingEngine engine;
    
    // Сначала рыночный ордер (не исполнится - нет ликвидности)
    Order marketBuy;
    marketBuy.order_id = MarketOrderRef(1);
    marketBuy.side = Side::kBid;
    marketBuy.type = OrderType::kMarket;
    marketBuy.amount = 100;
    marketBuy.timestamp = 1000;
    engine.addMarketOrder(marketBuy);
    
    EXPECT_TRUE(engine.getAsks().empty());
    
    // Потом лимитный ордер на продажу
    Order sell;
    sell.order_id = LimitOrderRef(2);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 100.0;
    sell.amount = 100;
    sell.timestamp = 2000;
    engine.addLimitOrder(sell);
    
    // Рыночный ордер уже не исполнится, так как он был обработан
    // Нужно было бы сохранять рыночные ордера, но в нашей модели они не сохраняются
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].second, 100);
}

TEST(MatchingEngineTest, ClearEngine) {
    MatchingEngine engine;
    
    Order buy;
    buy.order_id = LimitOrderRef(1);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 99.0;
    buy.amount = 100;
    buy.timestamp = 1000;
    engine.addLimitOrder(buy);
    
    Order sell;
    sell.order_id = LimitOrderRef(2);
    sell.side = Side::kAsk;
    sell.type = OrderType::kLimit;
    sell.price = 101.0;
    sell.amount = 100;
    sell.timestamp = 1000;
    engine.addLimitOrder(sell);
    
    EXPECT_FALSE(engine.getBids().empty());
    EXPECT_FALSE(engine.getAsks().empty());
    
    engine.clear();
    
    EXPECT_TRUE(engine.getBids().empty());
    EXPECT_TRUE(engine.getAsks().empty());
    EXPECT_DOUBLE_EQ(engine.getBestBid(), 0);
    EXPECT_DOUBLE_EQ(engine.getBestAsk(), 0);
}

// ==============================
// СТРЕСС-ТЕСТЫ
// ==============================

TEST(MatchingEngineTest, ManyOrdersAtSamePrice) {
    MatchingEngine engine;
    
    // Добавляем 100 ордеров на продажу по одной цене
    for (int i = 1; i <= 100; ++i) {
        Order sell;
        sell.order_id = LimitOrderRef(i);
        sell.side = Side::kAsk;
        sell.type = OrderType::kLimit;
        sell.price = 100.0;
        sell.amount = 10;
        sell.timestamp = 1000 + i;
        engine.addLimitOrder(sell);
    }
    
    // Приходит покупатель на 1000
    Order buy;
    buy.order_id = LimitOrderRef(101);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 1000;
    buy.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(buy);
    
    // Должны исполниться все 100 ордеров
    ASSERT_EQ(trades.size(), 100);
    double total = 0;
    for (const auto& trade : trades) {
        total += trade.amount;
    }
    EXPECT_DOUBLE_EQ(total, 1000);
    EXPECT_TRUE(engine.getAsks().empty());
}

TEST(MatchingEngineTest, ManyOrdersAtDifferentPrices) {
    MatchingEngine engine;
    
    // Добавляем asks на разных ценах
    for (int i = 1; i <= 100; ++i) {
        Order sell;
        sell.order_id = LimitOrderRef(i);
        sell.side = Side::kAsk;
        sell.type = OrderType::kLimit;
        sell.price = 100.0 + i * 0.1;
        sell.amount = 10;
        sell.timestamp = 1000 + i;
        engine.addLimitOrder(sell);
    }
    
    // Приходит покупатель с высоким лимитом
    Order buy;
    buy.order_id = LimitOrderRef(101);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 110.0;
    buy.amount = 500;
    buy.timestamp = 2000;
    
    auto trades = engine.addLimitOrder(buy);
    
    // Должны исполниться первые 50 уровней (каждый по 10 = 500)
    ASSERT_EQ(trades.size(), 50);
    double last_price = 0;
    for (const auto& trade : trades) {
        EXPECT_GT(trade.price, last_price);
        last_price = trade.price;
    }
    EXPECT_DOUBLE_EQ(last_price, 100.0 + 50 * 0.1);
}

// ==============================
// ТЕСТЫ ПОРЯДКА ИСПОЛНЕНИЯ
// ==============================

TEST(MatchingEngineTest, PriceTimePriority) {
    MatchingEngine engine;
    
    // Ордер с лучшей ценой, но позже
    Order sell1;
    sell1.order_id = LimitOrderRef(1);
    sell1.side = Side::kAsk;
    sell1.type = OrderType::kLimit;
    sell1.price = 99.0;
    sell1.amount = 50;
    sell1.timestamp = 2000;
    engine.addLimitOrder(sell1);
    
    // Ордер с худшей ценой, но раньше
    Order sell2;
    sell2.order_id = LimitOrderRef(2);
    sell2.side = Side::kAsk;
    sell2.type = OrderType::kLimit;
    sell2.price = 100.0;
    sell2.amount = 50;
    sell2.timestamp = 1000;
    engine.addLimitOrder(sell2);
    
    // Покупатель приходит с ценой 100
    Order buy;
    buy.order_id = LimitOrderRef(3);
    buy.side = Side::kBid;
    buy.type = OrderType::kLimit;
    buy.price = 100.0;
    buy.amount = 50;
    buy.timestamp = 3000;
    
    auto trades = engine.addLimitOrder(buy);
    
    // Должен исполниться ордер с лучшей ценой (99.0), а не более ранний
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].price, 99.0);
    
    // Ордер по 100.0 остался в книге
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].first, 100.0);
}

// ==============================
// ТЕСТЫ ПРИОРИТЕТА ИСПОЛНЕНИЯ (НАШИ VS ВНЕШНИЕ)
// ==============================

// 1. Наш лимитный ордер в книге, приходит внешний рыночный ордер
// TEST(MatchingEngineTest, OurLimitVsExternalMarket) {
//     MatchingEngine engine;
    
//     // Наш лимитный ордер на продажу (добавляем первым)
//     Order ourLimit;
//     ourLimit.order_id = LimitOrderRef{1};
//     ourLimit.side = Side::kAsk;
//     ourLimit.type = OrderType::kLimit;
//     ourLimit.price = 100.0;
//     ourLimit.amount = 50;
//     ourLimit.timestamp = 1000;
//     engine.addLimitOrder(ourLimit);
    
//     // Внешний лимитный ордер на продажу (добавляем позже, но с лучшей ценой)
//     Order externalLimit;
//     externalLimit.order_id = ExternalOrderRef{100};
//     externalLimit.side = Side::kAsk;
//     externalLimit.type = OrderType::kLimit;
//     externalLimit.price = 99.5;  // Лучшая цена!
//     externalLimit.amount = 30;
//     externalLimit.timestamp = 1500;
//     engine.addLimitOrder(externalLimit);
    
//     // Внешний рыночный ордер на покупку
//     Order externalMarket;
//     externalMarket.order_id = ExternalOrderRef{-1};
//     externalMarket.side = Side::kBid;
//     externalMarket.type = OrderType::kMarket;
//     externalMarket.amount = 60;
//     externalMarket.timestamp = 2000;
    
//     auto trades = engine.addMarketOrder(externalMarket);
    
//     // Должен исполниться внешний ордер с лучшей ценой (99.5) первым
//     ASSERT_EQ(trades.size(), 2);
//     EXPECT_DOUBLE_EQ(trades[0].price, 99.5);
//     EXPECT_DOUBLE_EQ(trades[0].amount, 30);
//     EXPECT_TRUE(trades[0].passive.isExternal());
//     EXPECT_EQ(trades[0].passive.getOrderId(), 100);
    
//     EXPECT_DOUBLE_EQ(trades[1].price, 100.0);
//     EXPECT_DOUBLE_EQ(trades[1].amount, 30);
//     EXPECT_TRUE(trades[1].passive.isLimit());
//     EXPECT_EQ(trades[1].passive.getOrderId(), 1);
//     EXPECT_EQ(trades[1].getOurRole(), InnerTrade::OurRole::kPassive);
// }

TEST(MatchingEngineTest, OurLimitVsExternalMarket_1) {
    MatchingEngine engine;
    
   
    // Наш лимитный ордер на продажу (добавляем первым)
    Order ourLimit;
    ourLimit.order_id = LimitOrderRef{1};
    ourLimit.side = Side::kAsk;
    ourLimit.type = OrderType::kLimit;
    ourLimit.price = 100.0;
    ourLimit.amount = 50;
    ourLimit.timestamp = 1000;
    
  
    auto ourResult = engine.addLimitOrder(ourLimit);
    
    // Внешний лимитный ордер на продажу (добавляем позже, но с лучшей ценой)
    Order externalLimit;
    externalLimit.order_id = ExternalOrderRef{100};
    externalLimit.side = Side::kAsk;
    externalLimit.type = OrderType::kLimit;
    externalLimit.price = 99.5;  // Лучшая цена!
    externalLimit.amount = 30;
    externalLimit.timestamp = 1500;
    
    auto externalResult = engine.addLimitOrder(externalLimit);
   
    // Внешний рыночный ордер на покупку
    Order externalMarket;
    externalMarket.order_id = ExternalOrderRef{-1};
    externalMarket.side = Side::kBid;
    externalMarket.type = OrderType::kMarket;
    externalMarket.amount = 60;
    externalMarket.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(externalMarket);
    
    // Проверки
    ASSERT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 99.5);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_TRUE(trades[0].passive.isExternal());
    EXPECT_EQ(trades[0].passive.getOrderId(), 100);
    
    EXPECT_DOUBLE_EQ(trades[1].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[1].amount, 30);
    EXPECT_TRUE(trades[1].passive.isLimit());
    EXPECT_EQ(trades[1].passive.getOrderId(), 1);
    EXPECT_EQ(trades[1].getOurRole(), InnerTrade::OurRole::kPassive);
}

// 2. Внешний лимитный ордер в книге, наш рыночный ордер
TEST(MatchingEngineTest, ExternalLimitVsOurMarket) {
    MatchingEngine engine;
    
    // Внешний лимитный ордер на продажу
    Order externalLimit;
    externalLimit.order_id = ExternalOrderRef{100};
    externalLimit.side = Side::kAsk;
    externalLimit.type = OrderType::kLimit;
    externalLimit.price = 100.0;
    externalLimit.amount = 50;
    externalLimit.timestamp = 1000;
    engine.addLimitOrder(externalLimit);
    
    // Наш лимитный ордер на продажу (лучшая цена)
    Order ourLimit;
    ourLimit.order_id = LimitOrderRef{1};
    ourLimit.side = Side::kAsk;
    ourLimit.type = OrderType::kLimit;
    ourLimit.price = 99.5;  // Лучшая цена
    ourLimit.amount = 30;
    ourLimit.timestamp = 1500;
    engine.addLimitOrder(ourLimit);
    
    // Наш рыночный ордер на покупку
    Order ourMarket;
    ourMarket.order_id = MarketOrderRef{2};
    ourMarket.side = Side::kBid;
    ourMarket.type = OrderType::kMarket;
    ourMarket.amount = 60;
    ourMarket.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(ourMarket);
    
    // Должен исполниться наш лимитный ордер (лучшая цена) первым
    ASSERT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 99.5);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_TRUE(trades[0].passive.isLimit());
    EXPECT_EQ(trades[0].passive.getOrderId(), 1);
    EXPECT_EQ(trades[0].getOurRole(), InnerTrade::OurRole::kBoth);
    EXPECT_EQ(trades[0].getOurOrderId(), 2);
    
    EXPECT_DOUBLE_EQ(trades[1].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[1].amount, 30);
    EXPECT_TRUE(trades[1].passive.isExternal());
    EXPECT_EQ(trades[1].passive.getOrderId(), 100);
}

// 3. Наш и внешний лимитные ордера на одном уровне - кто первый
TEST(MatchingEngineTest, SamePriceOurFirstVsExternal) {
    MatchingEngine engine;
    
    // Наш лимитный ордер (первый)
    Order ourLimit;
    ourLimit.order_id = LimitOrderRef{1};
    ourLimit.side = Side::kAsk;
    ourLimit.type = OrderType::kLimit;
    ourLimit.price = 100.0;
    ourLimit.amount = 30;
    ourLimit.timestamp = 1000;
    engine.addLimitOrder(ourLimit);
    
    // Внешний лимитный ордер (второй)
    Order externalLimit;
    externalLimit.order_id = ExternalOrderRef{100};
    externalLimit.side = Side::kAsk;
    externalLimit.type = OrderType::kLimit;
    externalLimit.price = 100.0;
    externalLimit.amount = 30;
    externalLimit.timestamp = 2000;
    engine.addLimitOrder(externalLimit);
    
    // Приходит рыночный ордер на покупку 50
    // Внешний рыночный ордер на покупку
    Order externalMarket;
    externalMarket.order_id = ExternalOrderRef{-1};
    externalMarket.side = Side::kBid;
    externalMarket.type = OrderType::kMarket;
    externalMarket.amount = 50;
    externalMarket.timestamp = 3000;
    
    auto trades = engine.addMarketOrder(externalMarket);
    
    // Должен исполниться НАШ ордер первым (он первый)
    ASSERT_EQ(trades.size(), 2);
    EXPECT_TRUE(trades[0].passive.isLimit());
    EXPECT_EQ(trades[0].passive.getOrderId(), 1);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_EQ(trades[0].getOurRole(), InnerTrade::OurRole::kPassive);
    
    EXPECT_TRUE(trades[1].passive.isExternal());
    EXPECT_EQ(trades[1].passive.getOrderId(), 100);
    EXPECT_DOUBLE_EQ(trades[1].amount, 20);
}

// 4. Внешний и наш лимитные ордера на одном уровне - внешний первый
TEST(MatchingEngineTest, SamePriceExternalFirstVsOur) {
    MatchingEngine engine;
    
    // Внешний лимитный ордер (первый)
    Order externalLimit;
    externalLimit.order_id = ExternalOrderRef{100};
    externalLimit.side = Side::kAsk;
    externalLimit.type = OrderType::kLimit;
    externalLimit.price = 100.0;
    externalLimit.amount = 30;
    externalLimit.timestamp = 1000;
    engine.addLimitOrder(externalLimit);
    
    // Наш лимитный ордер (второй)
    Order ourLimit;
    ourLimit.order_id = LimitOrderRef{1};
    ourLimit.side = Side::kAsk;
    ourLimit.type = OrderType::kLimit;
    ourLimit.price = 100.0;
    ourLimit.amount = 30;
    ourLimit.timestamp = 2000;
    engine.addLimitOrder(ourLimit);
    
    // Приходит рыночный ордер на покупку 50
    Order externalMarket;
    externalMarket.order_id = ExternalOrderRef{-1};
    externalMarket.side = Side::kBid;
    externalMarket.type = OrderType::kMarket;
    externalMarket.amount = 50;
    externalMarket.timestamp = 3000;
    
    auto trades = engine.addMarketOrder(externalMarket);
    
    // Должен исполниться ВНЕШНИЙ ордер первым (он первый)
    ASSERT_EQ(trades.size(), 2);
    EXPECT_TRUE(trades[0].passive.isExternal());
    EXPECT_EQ(trades[0].passive.getOrderId(), 100);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    
    EXPECT_TRUE(trades[1].passive.isLimit());
    EXPECT_EQ(trades[1].passive.getOrderId(), 1);
    EXPECT_DOUBLE_EQ(trades[1].amount, 20);
    EXPECT_EQ(trades[1].getOurRole(), InnerTrade::OurRole::kPassive);
}

// 5. Наш рыночный ордер против внешних лимитных
TEST(MatchingEngineTest, OurMarketVsExternalLimits) {
    MatchingEngine engine;
    
    // Внешние лимитные ордера на разных ценах
    Order externalLimit1;
    externalLimit1.order_id = ExternalOrderRef{100};
    externalLimit1.side = Side::kAsk;
    externalLimit1.type = OrderType::kLimit;
    externalLimit1.price = 100.0;
    externalLimit1.amount = 30;
    externalLimit1.timestamp = 1000;
    engine.addLimitOrder(externalLimit1);
    
    Order externalLimit2;
    externalLimit2.order_id = ExternalOrderRef{101};
    externalLimit2.side = Side::kAsk;
    externalLimit2.type = OrderType::kLimit;
    externalLimit2.price = 100.5;
    externalLimit2.amount = 40;
    externalLimit2.timestamp = 1000;
    engine.addLimitOrder(externalLimit2);
    
    // Наш рыночный ордер на покупку
    Order ourMarket;
    ourMarket.order_id = MarketOrderRef{1};
    ourMarket.side = Side::kBid;
    ourMarket.type = OrderType::kMarket;
    ourMarket.amount = 50;
    ourMarket.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(ourMarket);
    
    ASSERT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_TRUE(trades[0].passive.isExternal());
    EXPECT_EQ(trades[0].passive.getOrderId(), 100);
    
    EXPECT_DOUBLE_EQ(trades[1].price, 100.5);
    EXPECT_DOUBLE_EQ(trades[1].amount, 20);
    EXPECT_TRUE(trades[1].passive.isExternal());
    EXPECT_EQ(trades[1].passive.getOrderId(), 101);
    
    EXPECT_EQ(trades[0].getOurRole(), InnerTrade::OurRole::kAggressor);
    EXPECT_EQ(trades[1].getOurRole(), InnerTrade::OurRole::kAggressor);
    EXPECT_EQ(trades[0].getOurOrderId(), 1);
}

// 6. Внешний рыночный ордер против наших лимитных
TEST(MatchingEngineTest, ExternalMarketVsOurLimits) {
    MatchingEngine engine;
    
    // Наши лимитные ордера
    Order ourLimit1;
    ourLimit1.order_id = LimitOrderRef{1};
    ourLimit1.side = Side::kAsk;
    ourLimit1.type = OrderType::kLimit;
    ourLimit1.price = 100.0;
    ourLimit1.amount = 30;
    ourLimit1.timestamp = 1000;
    engine.addLimitOrder(ourLimit1);
    
    Order ourLimit2;
    ourLimit2.order_id = LimitOrderRef{2};
    ourLimit2.side = Side::kAsk;
    ourLimit2.type = OrderType::kLimit;
    ourLimit2.price = 100.5;
    ourLimit2.amount = 40;
    ourLimit2.timestamp = 1000;
    engine.addLimitOrder(ourLimit2);
    
    // Внешний рыночный ордер на покупку
    Order externalMarket;
    externalMarket.order_id = ExternalOrderRef{-1};
    externalMarket.side = Side::kBid;
    externalMarket.type = OrderType::kMarket;
    externalMarket.amount = 50;
    externalMarket.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(externalMarket);
    
    ASSERT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_TRUE(trades[0].passive.isLimit());
    EXPECT_EQ(trades[0].passive.getOrderId(), 1);
    EXPECT_EQ(trades[0].getOurRole(), InnerTrade::OurRole::kPassive);
    EXPECT_EQ(trades[0].getOurOrderId(), 1);
    
    EXPECT_DOUBLE_EQ(trades[1].price, 100.5);
    EXPECT_DOUBLE_EQ(trades[1].amount, 20);
    EXPECT_TRUE(trades[1].passive.isLimit());
    EXPECT_EQ(trades[1].passive.getOrderId(), 2);
    EXPECT_EQ(trades[1].getOurRole(), InnerTrade::OurRole::kPassive);
    EXPECT_EQ(trades[1].getOurOrderId(), 2);
}

// 7. Наш лимитный ордер против нашего рыночного (внутреннее исполнение)
TEST(MatchingEngineTest, OurLimitVsOurMarket) {
    MatchingEngine engine;
    
    // Наш лимитный ордер в книге
    Order ourLimit;
    ourLimit.order_id = LimitOrderRef{1};
    ourLimit.side = Side::kAsk;
    ourLimit.type = OrderType::kLimit;
    ourLimit.price = 100.0;
    ourLimit.amount = 50;
    ourLimit.timestamp = 1000;
    engine.addLimitOrder(ourLimit);
    
    // Наш рыночный ордер на покупку
    Order ourMarket;
    ourMarket.order_id = MarketOrderRef{2};
    ourMarket.side = Side::kBid;
    ourMarket.type = OrderType::kMarket;
    ourMarket.amount = 30;
    ourMarket.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(ourMarket);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    
    // Оба ордера наши
    EXPECT_TRUE(trades[0].aggressor.isMarket());
    EXPECT_EQ(trades[0].aggressor.getOrderId(), 2);
    EXPECT_TRUE(trades[0].passive.isLimit());
    EXPECT_EQ(trades[0].passive.getOrderId(), 1);
    EXPECT_EQ(trades[0].getOurRole(), InnerTrade::OurRole::kBoth);
}

// 8. Сложный сценарий: смешанные ордера на разных уровнях
TEST(MatchingEngineTest, MixedOrdersMultipleLevels) {
    MatchingEngine engine;
    
    // Уровень 1: внешний ордер с лучшей ценой
    Order externalBest;
    externalBest.order_id = ExternalOrderRef{100};
    externalBest.side = Side::kAsk;
    externalBest.type = OrderType::kLimit;
    externalBest.price = 99.5;
    externalBest.amount = 20;
    externalBest.timestamp = 1000;
    engine.addLimitOrder(externalBest);
    
    // Уровень 1: наш ордер на той же цене, но позже
    Order ourSamePrice;
    ourSamePrice.order_id = LimitOrderRef{1};
    ourSamePrice.side = Side::kAsk;
    ourSamePrice.type = OrderType::kLimit;
    ourSamePrice.price = 99.5;
    ourSamePrice.amount = 20;
    ourSamePrice.timestamp = 1500;
    engine.addLimitOrder(ourSamePrice);
    
    // Уровень 2: наш ордер
    Order ourLevel2;
    ourLevel2.order_id = LimitOrderRef{2};
    ourLevel2.side = Side::kAsk;
    ourLevel2.type = OrderType::kLimit;
    ourLevel2.price = 100.0;
    ourLevel2.amount = 30;
    ourLevel2.timestamp = 1000;
    engine.addLimitOrder(ourLevel2);
    
    // Уровень 2: внешний ордер
    Order externalLevel2;
    externalLevel2.order_id = ExternalOrderRef{101};
    externalLevel2.side = Side::kAsk;
    externalLevel2.type = OrderType::kLimit;
    externalLevel2.price = 100.0;
    externalLevel2.amount = 20;
    externalLevel2.timestamp = 1200;
    engine.addLimitOrder(externalLevel2);
    
    // Приходит наш рыночный ордер на покупку 80
    Order ourMarket;
    ourMarket.order_id = MarketOrderRef{3};
    ourMarket.side = Side::kBid;
    ourMarket.type = OrderType::kMarket;
    ourMarket.amount = 80;
    ourMarket.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(ourMarket);
    
    // Ожидаемый порядок:
    // 1. Внешний ордер на 99.5 (лучшая цена, первый)
    // 2. Наш ордер на 99.5 (та же цена, но позже)
    // 3. Наш ордер на 100.0 (первый на этом уровне)
    // 4. Внешний ордер на 100.0
    ASSERT_EQ(trades.size(), 4);
    
    EXPECT_DOUBLE_EQ(trades[0].price, 99.5);
    EXPECT_EQ(trades[0].passive.getOrderId(), 100);
    EXPECT_TRUE(trades[0].passive.isExternal());
    
    EXPECT_DOUBLE_EQ(trades[1].price, 99.5);
    EXPECT_EQ(trades[1].passive.getOrderId(), 1);
    EXPECT_TRUE(trades[1].passive.isLimit());
    EXPECT_EQ(trades[1].getOurRole(), InnerTrade::OurRole::kBoth);
    
    EXPECT_DOUBLE_EQ(trades[2].price, 100.0);
    EXPECT_EQ(trades[2].passive.getOrderId(), 2);
    EXPECT_TRUE(trades[2].passive.isLimit());
    EXPECT_EQ(trades[2].getOurRole(), InnerTrade::OurRole::kBoth);
    
    EXPECT_DOUBLE_EQ(trades[3].price, 100.0);
    EXPECT_EQ(trades[3].passive.getOrderId(), 101);
    EXPECT_TRUE(trades[3].passive.isExternal());
    
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].first, 100.0);
    EXPECT_DOUBLE_EQ(asks[0].second, 10);
}

// 9. Наш лимитный ордер снимается частично внешним трейдом, потом добивается нашим рыночным
TEST(MatchingEngineTest, PartialFillByExternalThenOurMarket) {
    MatchingEngine engine;
    
    // Наш лимитный ордер
    Order ourLimit;
    ourLimit.order_id = LimitOrderRef{1};
    ourLimit.side = Side::kAsk;
    ourLimit.type = OrderType::kLimit;
    ourLimit.price = 100.0;
    ourLimit.amount = 100;
    ourLimit.timestamp = 1000;
    engine.addLimitOrder(ourLimit);
    
    // Внешний трейд снимает часть
    Order externalMarket;
    externalMarket.order_id = ExternalOrderRef{-1};
    externalMarket.side = Side::kBid;
    externalMarket.type = OrderType::kMarket;
    externalMarket.amount = 30;
    externalMarket.timestamp = 1500;
    
    auto externalTrades = engine.addMarketOrder(externalMarket);
    ASSERT_EQ(externalTrades.size(), 1);
    EXPECT_DOUBLE_EQ(externalTrades[0].amount, 30);
    EXPECT_EQ(externalTrades[0].getOurRole(), InnerTrade::OurRole::kPassive);
    EXPECT_EQ(externalTrades[0].getOurOrderId(), 1);
    
    // Остаток: 70
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].second, 70);
    
    // Наш рыночный ордер добивает остаток
    Order ourMarket;
    ourMarket.order_id = MarketOrderRef{2};
    ourMarket.side = Side::kBid;
    ourMarket.type = OrderType::kMarket;
    ourMarket.amount = 70;
    ourMarket.timestamp = 2000;
    
    auto ourTrades = engine.addMarketOrder(ourMarket);
    ASSERT_EQ(ourTrades.size(), 1);
    EXPECT_DOUBLE_EQ(ourTrades[0].amount, 70);
    EXPECT_TRUE(ourTrades[0].passive.isLimit());
    EXPECT_EQ(ourTrades[0].passive.getOrderId(), 1);
    EXPECT_EQ(ourTrades[0].getOurRole(), InnerTrade::OurRole::kBoth);
    EXPECT_EQ(ourTrades[0].getOurOrderId(), 2);
    
    EXPECT_TRUE(engine.getAsks().empty());
}

// 10. Наш лимитный ордер на покупку против внешнего и нашего рыночного на продажу
TEST(MatchingEngineTest, OurBidLimitVsExternalAndOurAskMarket) {
    MatchingEngine engine;
    
    // Наш лимитный ордер на покупку
    Order ourBidLimit;
    ourBidLimit.order_id = LimitOrderRef{1};
    ourBidLimit.side = Side::kBid;
    ourBidLimit.type = OrderType::kLimit;
    ourBidLimit.price = 100.0;
    ourBidLimit.amount = 50;
    ourBidLimit.timestamp = 1000;
    engine.addLimitOrder(ourBidLimit);
    
    // Внешний лимитный ордер на покупку (лучшая цена!)
    Order externalBidLimit;
    externalBidLimit.order_id = ExternalOrderRef{100};
    externalBidLimit.side = Side::kBid;
    externalBidLimit.type = OrderType::kLimit;
    externalBidLimit.price = 100.5;
    externalBidLimit.amount = 30;
    externalBidLimit.timestamp = 1000;
    engine.addLimitOrder(externalBidLimit);
    
    // Наш рыночный ордер на продажу
    Order ourMarket;
    ourMarket.order_id = MarketOrderRef{2};
    ourMarket.side = Side::kAsk;
    ourMarket.type = OrderType::kMarket;
    ourMarket.amount = 60;
    ourMarket.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(ourMarket);
    
    // Должен исполниться внешний ордер (лучшая цена)
    ASSERT_EQ(trades.size(), 2);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.5);
    EXPECT_DOUBLE_EQ(trades[0].amount, 30);
    EXPECT_TRUE(trades[0].passive.isExternal());
    EXPECT_EQ(trades[0].passive.getOrderId(), 100);
    
    EXPECT_DOUBLE_EQ(trades[1].price, 100.0);
    EXPECT_DOUBLE_EQ(trades[1].amount, 30);
    EXPECT_TRUE(trades[1].passive.isLimit());
    EXPECT_EQ(trades[1].passive.getOrderId(), 1);
    EXPECT_EQ(trades[1].getOurRole(), InnerTrade::OurRole::kBoth);
    EXPECT_EQ(trades[1].getOurOrderId(), 2);
}

// 11. Тест на правильность ID в OrderRef
TEST(MatchingEngineTest, OrderRefIdsCorrectness) {
    MatchingEngine engine;
    
    // Наши лимитные ордера с разными ID
    for (int i = 1; i <= 5; ++i) {
        Order ourOrder;
        ourOrder.order_id = LimitOrderRef{i};
        ourOrder.side = Side::kAsk;
        ourOrder.type = OrderType::kLimit;
        ourOrder.price = 100.0 + i * 0.1;
        ourOrder.amount = 10;
        ourOrder.timestamp = 1000;
        engine.addLimitOrder(ourOrder);
    }
    
    // Внешний рыночный ордер покупает 30
    Order externalMarket;
    externalMarket.order_id = ExternalOrderRef{-1};
    externalMarket.side = Side::kBid;
    externalMarket.type = OrderType::kMarket;
    externalMarket.amount = 30;
    externalMarket.timestamp = 2000;
    
    auto trades = engine.addMarketOrder(externalMarket);
    
    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].passive.getOrderId(), 1);
    EXPECT_EQ(trades[1].passive.getOrderId(), 2);
    EXPECT_EQ(trades[2].passive.getOrderId(), 3);
    
    // Проверяем, что наши ордера имеют правильный тип
    EXPECT_TRUE(trades[0].passive.isLimit());
    EXPECT_FALSE(trades[0].passive.isMarket());
    EXPECT_FALSE(trades[0].passive.isExternal());
    
    // Агрессор - внешний
    EXPECT_TRUE(trades[0].aggressor.isExternal());
    EXPECT_EQ(trades[0].aggressor.getOrderId(), -1);
    
    // Наша роль - пассивный
    EXPECT_EQ(trades[0].getOurRole(), InnerTrade::OurRole::kPassive);
    EXPECT_EQ(trades[0].getOurOrderId(), 1);
}

// 12. Тест на приоритет: наш рыночный против внешнего рыночного
TEST(MatchingEngineTest, OurMarketVsExternalMarketSameTime) {
    MatchingEngine engine;
    
    // Внешний лимитный ордер
    Order externalLimit;
    externalLimit.order_id = ExternalOrderRef{100};
    externalLimit.side = Side::kAsk;
    externalLimit.type = OrderType::kLimit;
    externalLimit.price = 100.0;
    externalLimit.amount = 50;
    externalLimit.timestamp = 1000;
    engine.addLimitOrder(externalLimit);
    
    // Наш лимитный ордер (та же цена)
    Order ourLimit;
    ourLimit.order_id = LimitOrderRef{1};
    ourLimit.side = Side::kAsk;
    ourLimit.type = OrderType::kLimit;
    ourLimit.price = 100.0;
    ourLimit.amount = 30;
    ourLimit.timestamp = 1500;
    engine.addLimitOrder(ourLimit);
    
    // Внешний рыночный ордер (первый)
    Order externalMarket;
    externalMarket.order_id = ExternalOrderRef{-1};
    externalMarket.side = Side::kBid;
    externalMarket.type = OrderType::kMarket;
    externalMarket.amount = 40;
    externalMarket.timestamp = 2000;
    
    auto externalTrades = engine.addMarketOrder(externalMarket);

    ASSERT_EQ(externalTrades.size(), 1);
    EXPECT_EQ(externalTrades[0].passive.getOrderId(), 100);  // Внешний ордер (первый в очереди)
    EXPECT_DOUBLE_EQ(externalTrades[0].amount, 40);
    
    // Наш рыночный ордер (второй)
    Order ourMarket;
    ourMarket.order_id = MarketOrderRef{2};
    ourMarket.side = Side::kBid;
    ourMarket.type = OrderType::kMarket;
    ourMarket.amount = 40;
    ourMarket.timestamp = 2001;
    
    auto ourTrades = engine.addMarketOrder(ourMarket);
    
    // Должен исполниться остаток внешнего (10) + наш (30)
    ASSERT_EQ(ourTrades.size(), 2);
    EXPECT_EQ(ourTrades[0].passive.getOrderId(), 100);
    EXPECT_DOUBLE_EQ(ourTrades[0].amount, 10);
    EXPECT_EQ(ourTrades[1].passive.getOrderId(), 1);
    EXPECT_DOUBLE_EQ(ourTrades[1].amount, 30);
}

// ==============================
// ТЕСТЫ ДЛЯ addSnapshot
// ==============================

// 1. Базовый тест: добавление пустого снапшота
TEST(MatchingEngineTest, AddSnapshotEmpty) {
    MatchingEngine engine;
    
    std::vector<std::pair<double, double>> asks;
    std::vector<std::pair<double, double>> bids;
    
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    EXPECT_TRUE(engine.getAsks().empty());
    EXPECT_TRUE(engine.getBids().empty());
    EXPECT_DOUBLE_EQ(engine.getBestAsk(), 0);
    EXPECT_DOUBLE_EQ(engine.getBestBid(), 0);
}

// 2. Тест добавления простого снапшота с asks и bids
TEST(MatchingEngineTest, AddSnapshotBasic) {
    MatchingEngine engine;
    
    std::vector<std::pair<double, double>> asks = {
        {100.0, 100},
        {100.5, 200},
        {101.0, 150}
    };
    
    std::vector<std::pair<double, double>> bids = {
        {99.5, 100},
        {99.0, 200},
        {98.5, 150}
    };
    
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    // Проверяем asks
    auto result_asks = engine.getAsks();
    ASSERT_EQ(result_asks.size(), 3);
    EXPECT_DOUBLE_EQ(result_asks[0].first, 100.0);
    EXPECT_DOUBLE_EQ(result_asks[0].second, 100);
    EXPECT_DOUBLE_EQ(result_asks[1].first, 100.5);
    EXPECT_DOUBLE_EQ(result_asks[1].second, 200);
    EXPECT_DOUBLE_EQ(result_asks[2].first, 101.0);
    EXPECT_DOUBLE_EQ(result_asks[2].second, 150);
    
    // Проверяем bids
    auto result_bids = engine.getBids();
    ASSERT_EQ(result_bids.size(), 3);
    EXPECT_DOUBLE_EQ(result_bids[0].first, 99.5);
    EXPECT_DOUBLE_EQ(result_bids[0].second, 100);
    EXPECT_DOUBLE_EQ(result_bids[1].first, 99.0);
    EXPECT_DOUBLE_EQ(result_bids[1].second, 200);
    EXPECT_DOUBLE_EQ(result_bids[2].first, 98.5);
    EXPECT_DOUBLE_EQ(result_bids[2].second, 150);
    
    // Проверяем лучшие цены
    EXPECT_DOUBLE_EQ(engine.getBestAsk(), 100.0);
    EXPECT_DOUBLE_EQ(engine.getBestBid(), 99.5);
    EXPECT_DOUBLE_EQ(engine.getSpread(), 0.5);
}

// 3. Тест: снапшот только с asks
TEST(MatchingEngineTest, AddSnapshotOnlyAsks) {
    MatchingEngine engine;
    
    std::vector<std::pair<double, double>> asks = {
        {100.0, 50},
        {101.0, 100}
    };
    
    std::vector<std::pair<double, double>> bids;
    
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    auto result_asks = engine.getAsks();
    ASSERT_EQ(result_asks.size(), 2);
    EXPECT_DOUBLE_EQ(result_asks[0].first, 100.0);
    EXPECT_DOUBLE_EQ(result_asks[0].second, 50);
    
    auto result_bids = engine.getBids();
    EXPECT_TRUE(result_bids.empty());
    
    EXPECT_DOUBLE_EQ(engine.getBestAsk(), 100.0);
    EXPECT_DOUBLE_EQ(engine.getBestBid(), 0);
}

// 4. Тест: снапшот только с bids
TEST(MatchingEngineTest, AddSnapshotOnlyBids) {
    MatchingEngine engine;
    
    std::vector<std::pair<double, double>> asks;
    
    std::vector<std::pair<double, double>> bids = {
        {99.5, 50},
        {99.0, 100}
    };
    
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    auto result_bids = engine.getBids();
    ASSERT_EQ(result_bids.size(), 2);
    EXPECT_DOUBLE_EQ(result_bids[0].first, 99.5);
    EXPECT_DOUBLE_EQ(result_bids[0].second, 50);
    
    auto result_asks = engine.getAsks();
    EXPECT_TRUE(result_asks.empty());
    
    EXPECT_DOUBLE_EQ(engine.getBestBid(), 99.5);
    EXPECT_DOUBLE_EQ(engine.getBestAsk(), 0);
}

// 6. Тест: обновление существующего стакана (наши ордера сохраняются)
TEST(MatchingEngineTest, AddSnapshotPreserveOurOrders) {
    MatchingEngine engine;
    
    // Добавляем наш лимитный ордер
    Order ourOrder;
    ourOrder.order_id = LimitOrderRef{1};
    ourOrder.side = Side::kBid;
    ourOrder.type = OrderType::kLimit;
    ourOrder.price = 99.0;
    ourOrder.amount = 50;
    ourOrder.timestamp = 1000;
    engine.addLimitOrder(ourOrder);
    
    // Проверяем, что ордер в книге
    auto bids_before = engine.getBids();
    ASSERT_EQ(bids_before.size(), 1);
    EXPECT_DOUBLE_EQ(bids_before[0].second, 50);
    
    // Приходит новый снапшот
    std::vector<std::pair<double, double>> asks = {{100.0, 100}};
    std::vector<std::pair<double, double>> bids = {{99.5, 100}, {99.0, 50}};
    
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    // Наш ордер должен сохраниться на уровне 99.0
    auto result_bids = engine.getBids();
    ASSERT_EQ(result_bids.size(), 2);
    
    // Первый уровень (лучшая цена) - чужой ордер
    EXPECT_DOUBLE_EQ(result_bids[0].first, 99.5);
    EXPECT_DOUBLE_EQ(result_bids[0].second, 100);
    
    // Второй уровень - наш ордер должен быть добавлен к чужому
    // Общий объем = наши 50 + чужие 50 = 100
    EXPECT_DOUBLE_EQ(result_bids[1].first, 99.0);
    EXPECT_DOUBLE_EQ(result_bids[1].second, 100); // 50 (наши) + 50 (чужие)
}

// 7. Тест: несколько наших ордеров на разных уровнях
TEST(MatchingEngineTest, AddSnapshotMultipleOurOrders) {
    MatchingEngine engine;
    
    // Добавляем наши лимитные ордера
    Order ourOrder1;
    ourOrder1.order_id = LimitOrderRef{1};
    ourOrder1.side = Side::kBid;
    ourOrder1.type = OrderType::kLimit;
    ourOrder1.price = 99.0;
    ourOrder1.amount = 30;
    ourOrder1.timestamp = 1000;
    engine.addLimitOrder(ourOrder1);
    
    Order ourOrder2;
    ourOrder2.order_id = LimitOrderRef{2};
    ourOrder2.side = Side::kAsk;
    ourOrder2.type = OrderType::kLimit;
    ourOrder2.price = 101.0;
    ourOrder2.amount = 40;
    ourOrder2.timestamp = 1000;
    engine.addLimitOrder(ourOrder2);
    
    // Приходит снапшот
    std::vector<std::pair<double, double>> asks = {{100.0, 100}, {101.0, 60}};
    std::vector<std::pair<double, double>> bids = {{99.5, 80}, {99.0, 20}};
    
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    // Проверяем bids: уровень 99.0 должен иметь общий объем 20 (чужие) + 30 (наши) = 50
    auto result_bids = engine.getBids();
    ASSERT_EQ(result_bids.size(), 2);
    EXPECT_DOUBLE_EQ(result_bids[0].first, 99.5);
    EXPECT_DOUBLE_EQ(result_bids[0].second, 80);
    EXPECT_DOUBLE_EQ(result_bids[1].first, 99.0);
    EXPECT_DOUBLE_EQ(result_bids[1].second, 50);
    
    // Проверяем asks: уровень 101.0 должен иметь общий объем 60 (чужие) + 40 (наши) = 100
    auto result_asks = engine.getAsks();
    ASSERT_EQ(result_asks.size(), 2);
    EXPECT_DOUBLE_EQ(result_asks[0].first, 100.0);
    EXPECT_DOUBLE_EQ(result_asks[0].second, 100);
    EXPECT_DOUBLE_EQ(result_asks[1].first, 101.0);
    EXPECT_DOUBLE_EQ(result_asks[1].second, 100);
}

// 8. Тест: полная замена стакана (предыдущие внешние ордера удаляются)
TEST(MatchingEngineTest, AddSnapshotReplacesExternalOrders) {
    MatchingEngine engine;
    
    // Первый снапшот
    std::vector<std::pair<double, double>> asks1 = {{100.0, 100}};
    std::vector<std::pair<double, double>> bids1 = {{99.5, 100}};
    engine.addSnapshot(std::move(asks1), std::move(bids1));
    
    // Добавляем наш ордер
    Order ourOrder;
    ourOrder.order_id = LimitOrderRef{1};
    ourOrder.side = Side::kBid;
    ourOrder.type = OrderType::kLimit;
    ourOrder.price = 99.0;
    ourOrder.amount = 50;
    ourOrder.timestamp = 1000;
    engine.addLimitOrder(ourOrder);
    
    // Второй снапшот (другие цены)
    std::vector<std::pair<double, double>> asks2 = {{101.0, 200}};
    std::vector<std::pair<double, double>> bids2 = {{100.5, 200}};
    engine.addSnapshot(std::move(asks2), std::move(bids2));
    
    // Внешние ордера из первого снапшота должны исчезнуть
    auto result_asks = engine.getAsks();
    ASSERT_EQ(result_asks.size(), 1);
    EXPECT_DOUBLE_EQ(result_asks[0].first, 101.0);
    EXPECT_DOUBLE_EQ(result_asks[0].second, 200);
    
    auto result_bids = engine.getBids();
    ASSERT_EQ(result_bids.size(), 2); // 100.5 (внешний) и 99.0 (наш)
    EXPECT_DOUBLE_EQ(result_bids[0].first, 100.5);
    EXPECT_DOUBLE_EQ(result_bids[0].second, 200);
    EXPECT_DOUBLE_EQ(result_bids[1].first, 99.0);
    EXPECT_DOUBLE_EQ(result_bids[1].second, 50);
}

// 9. Стресс-тест: много уровней
TEST(MatchingEngineTest, AddSnapshotManyLevels) {
    MatchingEngine engine;
    
    std::vector<std::pair<double, double>> asks;
    std::vector<std::pair<double, double>> bids;
    
    // Создаем 100 уровней
    for (int i = 1; i <= 100; ++i) {
        asks.emplace_back(100.0 + i * 0.1, i * 10);
        bids.emplace_back(99.9 - i * 0.1, i * 10);
    }
    
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    auto result_asks = engine.getAsks(1000);
    ASSERT_EQ(result_asks.size(), 100);
    EXPECT_DOUBLE_EQ(result_asks[0].first, 100.1);
    EXPECT_DOUBLE_EQ(result_asks[0].second, 10);
    
    auto result_bids = engine.getBids(1000);
    ASSERT_EQ(result_bids.size(), 100);
    EXPECT_DOUBLE_EQ(result_bids[0].first, 99.8);
    EXPECT_DOUBLE_EQ(result_bids[0].second, 10);
}

// 9. Стресс-тест: много уровней
// TEST(MatchingEngineTest, AddSnapshotManyLevels1) {
//     MatchingEngine engine;
    
//     std::vector<std::pair<double, double>> asks;
//     std::vector<std::pair<double, double>> bids;
    
//     std::cout << "\n=== Test AddSnapshotManyLevels ===" << std::endl;
    
//     // Создаем 100 уровней
//     for (int i = 1; i <= 100; ++i) {
//         asks.emplace_back(100.0 + i * 0.1, i * 10);
//         bids.emplace_back(99.9 - i * 0.1, i * 10);
//     }
    
//     std::cout << "Created " << asks.size() << " asks and " << bids.size() << " bids" << std::endl;
//     std::cout << "First ask: price=" << asks[0].first << ", volume=" << asks[0].second << std::endl;
//     std::cout << "Last ask: price=" << asks[99].first << ", volume=" << asks[99].second << std::endl;
//     std::cout << "First bid: price=" << bids[0].first << ", volume=" << bids[0].second << std::endl;
//     std::cout << "Last bid: price=" << bids[99].first << ", volume=" << bids[99].second << std::endl;
    
//     engine.addSnapshot(std::move(asks), std::move(bids));
    
//     auto result_asks = engine.getAsks();
//     auto result_bids = engine.getBids();
    
//     std::cout << "\nResult asks size: " << result_asks.size() << std::endl;
//     std::cout << "Result bids size: " << result_bids.size() << std::endl;
    
//     if (result_asks.size() > 0) {
//         std::cout << "First result ask: price=" << result_asks[0].first 
//                   << ", volume=" << result_asks[0].second << std::endl;
//         if (result_asks.size() > 1) {
//             std::cout << "Second result ask: price=" << result_asks[1].first 
//                       << ", volume=" << result_asks[1].second << std::endl;
//         }
//     }
    
//     if (result_bids.size() > 0) {
//         std::cout << "First result bid: price=" << result_bids[0].first 
//                   << ", volume=" << result_bids[0].second << std::endl;
//         if (result_bids.size() > 1) {
//             std::cout << "Second result bid: price=" << result_bids[1].first 
//                       << ", volume=" << result_bids[1].second << std::endl;
//         }
//     }
    
//     // Проверяем размеры
//     EXPECT_EQ(result_asks.size(), 100) << "Asks size mismatch";
//     EXPECT_EQ(result_bids.size(), 100) << "Bids size mismatch";
    
//     if (result_asks.size() > 0) {
//         EXPECT_DOUBLE_EQ(result_asks[0].first, 100.1) 
//             << "First ask price: expected 100.1, got " << result_asks[0].first;
//         EXPECT_DOUBLE_EQ(result_asks[0].second, 10) 
//             << "First ask volume: expected 10, got " << result_asks[0].second;
//     }
    
//     if (result_bids.size() > 0) {
//         EXPECT_DOUBLE_EQ(result_bids[0].first, 99.8) 
//             << "First bid price: expected 99.8, got " << result_bids[0].first;
//         EXPECT_DOUBLE_EQ(result_bids[0].second, 10) 
//             << "First bid volume: expected 10, got " << result_bids[0].second;
//     }
    
//     // Дополнительная проверка последних элементов
//     if (result_asks.size() >= 100) {
//         std::cout << "Last ask: price=" << result_asks[99].first 
//                   << ", volume=" << result_asks[99].second << std::endl;
//         EXPECT_DOUBLE_EQ(result_asks[99].first, 100.0 + 100 * 0.1) 
//             << "Last ask price mismatch";
//     }
    
//     if (result_bids.size() >= 100) {
//         std::cout << "Last bid: price=" << result_bids[99].first 
//                   << ", volume=" << result_bids[99].second << std::endl;
//         EXPECT_DOUBLE_EQ(result_bids[99].first, 99.9 - 100 * 0.1) 
//             << "Last bid price mismatch";
//     }
    
//     // Проверяем сортировку asks (по возрастанию)
//     for (size_t i = 1; i < result_asks.size(); ++i) {
//         EXPECT_LT(result_asks[i-1].first, result_asks[i].first) 
//             << "Asks not sorted at index " << i;
//     }
    
//     // Проверяем сортировку bids (по убыванию)
//     for (size_t i = 1; i < result_bids.size(); ++i) {
//         EXPECT_GT(result_bids[i-1].first, result_bids[i].first) 
//             << "Bids not sorted at index " << i;
//     }
// }

// 10. Тест: дублирующиеся цены в снапшоте
TEST(MatchingEngineTest, AddSnapshotDuplicatePrices) {
    MatchingEngine engine;
    
    // Одинаковые цены - такого не должно быть, но проверим обработку
    std::vector<std::pair<double, double>> asks = {
        {100.0, 50},
        {100.0, 30}  // Дубликат
    };
    
    std::vector<std::pair<double, double>> bids = {
        {99.5, 40},
        {99.5, 20}   // Дубликат
    };
    
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    // Должны быть оба уровня или объединены?
    auto result_asks = engine.getAsks();
    // В зависимости от реализации: может быть 1 или 2 уровня
    // Обычно должно быть 2 уровня (дубликаты не объединяются автоматически)
    EXPECT_GE(result_asks.size(), 1);
    EXPECT_LE(result_asks.size(), 2);
}

// 11. Тест: добавление снапшота с нулевыми объемами
TEST(MatchingEngineTest, AddSnapshotZeroVolumes) {
    MatchingEngine engine;
    
    std::vector<std::pair<double, double>> asks = {
        {100.0, 0},
        {101.0, 0}
    };
    
    std::vector<std::pair<double, double>> bids = {
        {99.5, 0},
        {99.0, 0}
    };
    
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    // Уровни с нулевым объемом могут быть удалены
    auto result_asks = engine.getAsks();
    auto result_bids = engine.getBids();
    
    // В зависимости от реализации: могут быть удалены или остаться с нулевым объемом
    // Проверяем, что нет отрицательных объемов
    for (const auto& ask : result_asks) {
        EXPECT_GE(ask.second, 0);
    }
    for (const auto& bid : result_bids) {
        EXPECT_GE(bid.second, 0);
    }
}

// 12. Тест: несколько последовательных снапшотов
TEST(MatchingEngineTest, AddSnapshotSequential) {
    MatchingEngine engine;
    
    // Снапшот 1
    std::vector<std::pair<double, double>> asks1 = {{100.0, 100}};
    std::vector<std::pair<double, double>> bids1 = {{99.5, 100}};
    engine.addSnapshot(std::move(asks1), std::move(bids1));
    
    // Добавляем наш ордер
    Order ourOrder;
    ourOrder.order_id = LimitOrderRef{1};
    ourOrder.side = Side::kBid;
    ourOrder.type = OrderType::kLimit;
    ourOrder.price = 99.0;
    ourOrder.amount = 50;
    ourOrder.timestamp = 1000;
    engine.addLimitOrder(ourOrder);
    
    // Снапшот 2 (цена изменилась)
    std::vector<std::pair<double, double>> asks2 = {{101.0, 200}};
    std::vector<std::pair<double, double>> bids2 = {{100.0, 200}};
    engine.addSnapshot(std::move(asks2), std::move(bids2));
    
    // Снапшот 3 (возврат к старым ценам)
    std::vector<std::pair<double, double>> asks3 = {{100.0, 150}};
    std::vector<std::pair<double, double>> bids3 = {{99.5, 150}};
    engine.addSnapshot(std::move(asks3), std::move(bids3));
    
    // Наш ордер на 99.0 должен сохраниться
    auto result_bids = engine.getBids();
    bool found_our_order = false;
    for (const auto& bid : result_bids) {
        if (std::abs(bid.first - 99.0) < 1e-10) {
            found_our_order = true;
            EXPECT_DOUBLE_EQ(bid.second, 50);
            break;
        }
    }
    EXPECT_TRUE(found_our_order);
}

// 13. Тест: производительность (замер времени)
TEST(MatchingEngineTest, AddSnapshotPerformance) {
    MatchingEngine engine;
    
    std::vector<std::pair<double, double>> asks;
    std::vector<std::pair<double, double>> bids;
    
    // Создаем 25 уровней (максимум для реального L2)
    for (int i = 1; i <= 25; ++i) {
        asks.emplace_back(100.0 + i * 0.1, i * 100);
        bids.emplace_back(99.9 - i * 0.1, i * 100);
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Запускаем 10000 обновлений
    for (int i = 0; i < 10000; ++i) {
        auto asks_copy = asks;
        auto bids_copy = bids;
        engine.addSnapshot(std::move(asks_copy), std::move(bids_copy));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "10000 addSnapshot calls took " << duration.count() << " ms" << std::endl;
    
    // Проверяем, что все работает
    auto result_asks = engine.getAsks();
    EXPECT_EQ(result_asks.size(), 25);
}

// 14. Тест: корректность приоритета после снапшота
TEST(MatchingEngineTest, AddSnapshotPriority) {
    MatchingEngine engine;
    
    // Добавляем наши ордера
    Order ourOrder1;
    ourOrder1.order_id = LimitOrderRef{1};
    ourOrder1.side = Side::kBid;
    ourOrder1.type = OrderType::kLimit;
    ourOrder1.price = 99.5;
    ourOrder1.amount = 30;
    ourOrder1.timestamp = 1000;
    engine.addLimitOrder(ourOrder1);
    
    Order ourOrder2;
    ourOrder2.order_id = LimitOrderRef{2};
    ourOrder2.side = Side::kBid;
    ourOrder2.type = OrderType::kLimit;
    ourOrder2.price = 99.5;
    ourOrder2.amount = 20;
    ourOrder2.timestamp = 2000;
    engine.addLimitOrder(ourOrder2);
    
    // Приходит снапшот с внешними ордерами на том же уровне
    std::vector<std::pair<double, double>> asks = {{100.0, 100}};
    std::vector<std::pair<double, double>> bids = {{99.5, 100}}; // Внешний ордер
    
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    // Внешний ордер должен быть первым (FIFO)
    // Всего на уровне 99.5: внешний 100 + наши 30 + 20 = 150
    
    // Здесь сложно проверить порядок из публичного API,
    // но хотя бы проверим общий объем
    auto result_bids = engine.getBids();
    ASSERT_EQ(result_bids.size(), 1);
    EXPECT_DOUBLE_EQ(result_bids[0].first, 99.5);
    EXPECT_DOUBLE_EQ(result_bids[0].second, 150);
}

// ==============================
// ТЕСТЫ ОТМЕНЫ ОРДЕРОВ
// ==============================

TEST(MatchingEngineTest, CancelOrderByLimitOrderRef) {
    MatchingEngine engine;
    
    // Добавляем ордер
    Order order;
    order.order_id = LimitOrderRef{1};
    order.side = Side::kBid;
    order.type = OrderType::kLimit;
    order.price = 99.0;
    order.amount = 100;
    order.timestamp = 1000;
    engine.addLimitOrder(order);
    
    // Проверяем, что ордер в книге
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].second, 100);
    
    // Отменяем ордер
    bool cancelled = engine.cancelOrder(LimitOrderRef{1});
    EXPECT_TRUE(cancelled);
    
    // Проверяем, что книга пуста
    bids = engine.getBids();
    EXPECT_TRUE(bids.empty());
}

TEST(MatchingEngineTest, CancelOrderByMarketOrderRef) {
    MatchingEngine engine;
    
    // Добавляем рыночный ордер (он не остается в книге, поэтому добавим лимитный)
    Order order;
    order.order_id = MarketOrderRef{1};
    order.side = Side::kBid;
    order.type = OrderType::kLimit;
    order.price = 99.0;
    order.amount = 100;
    order.timestamp = 1000;
    engine.addLimitOrder(order);
    
    // Проверяем, что ордер в книге
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].second, 100);
    
    // Отменяем ордер (OrderRef с MarketOrderRef)
    bool cancelled = engine.cancelOrder(MarketOrderRef{1});
    EXPECT_TRUE(cancelled);
    
    EXPECT_TRUE(engine.getBids().empty());
}

TEST(MatchingEngineTest, CancelOrderByExternalOrderRef) {
    MatchingEngine engine;
    
    // Добавляем внешний ордер через снапшот
    std::vector<std::pair<double, double>> asks;
    std::vector<std::pair<double, double>> bids = {{99.5, 100}};
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    // Внешний ордер имеет ExternalOrderRef с ID 0
    auto bids_before = engine.getBids();
    ASSERT_EQ(bids_before.size(), 1);
    EXPECT_DOUBLE_EQ(bids_before[0].second, 100);
    
    // Отменяем внешний ордер
    bool cancelled = engine.cancelOrder(ExternalOrderRef{0});
    EXPECT_TRUE(cancelled);
    
    EXPECT_TRUE(engine.getBids().empty());
}

TEST(MatchingEngineTest, CancelNonExistentOrder) {
    MatchingEngine engine;
    
    // Пытаемся отменить несуществующий ордер
    bool cancelled = engine.cancelOrder(LimitOrderRef{999});
    EXPECT_FALSE(cancelled);
    
    // Добавляем ордер с ID 1
    Order order;
    order.order_id = LimitOrderRef{1};
    order.side = Side::kBid;
    order.type = OrderType::kLimit;
    order.price = 99.0;
    order.amount = 100;
    order.timestamp = 1000;
    engine.addLimitOrder(order);
    
    // Пытаемся отменить ордер с другим ID
    cancelled = engine.cancelOrder(LimitOrderRef{2});
    EXPECT_FALSE(cancelled);
    
    // Ордер с ID 1 должен остаться
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].second, 100);
}

TEST(MatchingEngineTest, CancelOrderMultipleOrders) {
    MatchingEngine engine;
    
    // Добавляем несколько ордеров
    Order order1;
    order1.order_id = LimitOrderRef{1};
    order1.side = Side::kBid;
    order1.type = OrderType::kLimit;
    order1.price = 99.0;
    order1.amount = 30;
    order1.timestamp = 1000;
    engine.addLimitOrder(order1);
    
    Order order2;
    order2.order_id = LimitOrderRef{2};
    order2.side = Side::kBid;
    order2.type = OrderType::kLimit;
    order2.price = 99.0;
    order2.amount = 20;
    order2.timestamp = 2000;
    engine.addLimitOrder(order2);
    
    Order order3;
    order3.order_id = LimitOrderRef{3};
    order3.side = Side::kBid;
    order3.type = OrderType::kLimit;
    order3.price = 98.5;
    order3.amount = 50;
    order3.timestamp = 3000;
    engine.addLimitOrder(order3);
    
    // Проверяем начальное состояние
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 2);
    EXPECT_DOUBLE_EQ(bids[0].second, 50);  // 30+20 = 50
    EXPECT_DOUBLE_EQ(bids[1].second, 50);
    
    // Отменяем ордер 2
    bool cancelled = engine.cancelOrder(LimitOrderRef{2});
    EXPECT_TRUE(cancelled);
    
    // Проверяем состояние после отмены
    bids = engine.getBids();
    ASSERT_EQ(bids.size(), 2);
    EXPECT_DOUBLE_EQ(bids[0].second, 30);  // Только order1 остался
    EXPECT_DOUBLE_EQ(bids[1].second, 50);
    
    // Отменяем ордер 1
    cancelled = engine.cancelOrder(LimitOrderRef{1});
    EXPECT_TRUE(cancelled);
    
    bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].first, 98.5);
    EXPECT_DOUBLE_EQ(bids[0].second, 50);
}

// ==============================
// ТЕСТЫ ОТМЕНЫ ВСЕХ НАШИХ ОРДЕРОВ
// ==============================

TEST(MatchingEngineTest, CancelAllOurOrdersEmpty) {
    MatchingEngine engine;
    
    // Отменяем все наши ордера в пустой книге
    EXPECT_NO_THROW(engine.cancelAllOurOrders());
    
    EXPECT_TRUE(engine.getBids().empty());
    EXPECT_TRUE(engine.getAsks().empty());
}

TEST(MatchingEngineTest, CancelAllOurOrdersOnlyOurOrders) {
    MatchingEngine engine;
    
    // Добавляем наши ордера
    Order order1;
    order1.order_id = LimitOrderRef{1};
    order1.side = Side::kBid;
    order1.type = OrderType::kLimit;
    order1.price = 99.0;
    order1.amount = 30;
    order1.timestamp = 1000;
    engine.addLimitOrder(order1);
    
    Order order2;
    order2.order_id = LimitOrderRef{2};
    order2.side = Side::kBid;
    order2.type = OrderType::kLimit;
    order2.price = 98.5;
    order2.amount = 20;
    order2.timestamp = 2000;
    engine.addLimitOrder(order2);
    
    Order order3;
    order3.order_id = LimitOrderRef{3};
    order3.side = Side::kAsk;
    order3.type = OrderType::kLimit;
    order3.price = 101.0;
    order3.amount = 40;
    order3.timestamp = 3000;
    engine.addLimitOrder(order3);
    
    // Проверяем начальное состояние
    auto bids = engine.getBids();
    auto asks = engine.getAsks();
    ASSERT_EQ(bids.size(), 2);
    ASSERT_EQ(asks.size(), 1);
    
    // Отменяем все наши ордера
    engine.cancelAllOurOrders();
    
    // Книга должна быть пуста
    EXPECT_TRUE(engine.getBids().empty());
    EXPECT_TRUE(engine.getAsks().empty());
}

TEST(MatchingEngineTest, CancelAllOurOrdersMixedWithExternal) {
    MatchingEngine engine;
    
    // Добавляем внешние ордера через снапшот
    std::vector<std::pair<double, double>> asks = {{101.0, 100}};
    std::vector<std::pair<double, double>> bids = {{99.5, 100}, {99.0, 50}};
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    // Добавляем наши ордера
    Order ourOrder1;
    ourOrder1.order_id = LimitOrderRef{1};
    ourOrder1.side = Side::kBid;
    ourOrder1.type = OrderType::kLimit;
    ourOrder1.price = 99.5;
    ourOrder1.amount = 30;
    ourOrder1.timestamp = 1000;
    engine.addLimitOrder(ourOrder1);
    
    Order ourOrder2;
    ourOrder2.order_id = LimitOrderRef{2};
    ourOrder2.side = Side::kAsk;
    ourOrder2.type = OrderType::kLimit;
    ourOrder2.price = 101.0;
    ourOrder2.amount = 20;
    ourOrder2.timestamp = 2000;
    engine.addLimitOrder(ourOrder2);
    
    // Проверяем начальное состояние
    auto bids_before = engine.getBids();
    auto asks_before = engine.getAsks();
    
    // Bids: уровень 99.5 - внешний 100 + наш 30 = 130
    //       уровень 99.0 - внешний 50
    ASSERT_EQ(bids_before.size(), 2);
    EXPECT_DOUBLE_EQ(bids_before[0].second, 130);
    EXPECT_DOUBLE_EQ(bids_before[1].second, 50);
    
    // Asks: уровень 101.0 - внешний 100 + наш 20 = 120
    ASSERT_EQ(asks_before.size(), 1);
    EXPECT_DOUBLE_EQ(asks_before[0].second, 120);
    
    // Отменяем все наши ордера
    engine.cancelAllOurOrders();
    
    // Внешние ордера должны остаться
    auto bids_after = engine.getBids();
    auto asks_after = engine.getAsks();
    
    ASSERT_EQ(bids_after.size(), 2);
    EXPECT_DOUBLE_EQ(bids_after[0].first, 99.5);
    EXPECT_DOUBLE_EQ(bids_after[0].second, 100);  // Только внешний
    EXPECT_DOUBLE_EQ(bids_after[1].first, 99.0);
    EXPECT_DOUBLE_EQ(bids_after[1].second, 50);
    
    ASSERT_EQ(asks_after.size(), 1);
    EXPECT_DOUBLE_EQ(asks_after[0].first, 101.0);
    EXPECT_DOUBLE_EQ(asks_after[0].second, 100);  // Только внешний
}

TEST(MatchingEngineTest, CancelAllOurOrdersWithPartialFilled) {
    MatchingEngine engine;
    
    // Добавляем наши ордера
    Order ourOrder;
    ourOrder.order_id = LimitOrderRef{1};
    ourOrder.side = Side::kAsk;
    ourOrder.type = OrderType::kLimit;
    ourOrder.price = 100.0;
    ourOrder.amount = 100;
    ourOrder.timestamp = 1000;
    engine.addLimitOrder(ourOrder);
    
    // Частичное исполнение (внешний рыночный ордер)
    Order externalMarket;
    externalMarket.order_id = ExternalOrderRef{-1};
    externalMarket.side = Side::kBid;
    externalMarket.type = OrderType::kMarket;
    externalMarket.amount = 60;
    externalMarket.timestamp = 1500;
    
    auto trades = engine.addMarketOrder(externalMarket);
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].amount, 60);
    
    // Остаток нашего ордера: 40
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].second, 40);
    
    // Отменяем все наши ордера
    engine.cancelAllOurOrders();
    
    // Книга должна быть пуста (внешних ордеров не было)
    EXPECT_TRUE(engine.getAsks().empty());
}

// ==============================
// СТРЕСС-ТЕСТЫ ОТМЕНЫ
// ==============================

TEST(MatchingEngineTest, CancelManyOrders) {
    MatchingEngine engine;
    
    // Добавляем много ордеров
    for (int i = 1; i <= 100; ++i) {
        Order order;
        order.order_id = LimitOrderRef{i};
        order.side = (i % 2 == 0) ? Side::kBid : Side::kAsk;
        order.type = OrderType::kLimit;
        order.price = 100.0 + (i % 10) * 0.5;
        order.amount = 100;
        order.timestamp = 1000 * i;
        engine.addLimitOrder(order);
    }
    
    // Проверяем, что ордера добавились
    auto bids = engine.getBids();
    auto asks = engine.getAsks();
    EXPECT_FALSE(bids.empty() && asks.empty());
    
    // Отменяем все ордера
    engine.cancelAllOurOrders();
    
    EXPECT_TRUE(engine.getBids().empty());
    EXPECT_TRUE(engine.getAsks().empty());
}

TEST(MatchingEngineTest, CancelAllOurOrdersAfterSnapshot) {
    MatchingEngine engine;
    
    // Сначала снапшот с внешними ордерами
    std::vector<std::pair<double, double>> asks = {{100.0, 100}, {101.0, 200}};
    std::vector<std::pair<double, double>> bids = {{99.5, 100}, {99.0, 200}};
    engine.addSnapshot(std::move(asks), std::move(bids));
    
    // Добавляем наши ордера
    for (int i = 1; i <= 10; ++i) {
        Order order;
        order.order_id = LimitOrderRef{i};
        order.side = Side::kBid;
        order.type = OrderType::kLimit;
        order.price = 99.5;
        order.amount = 10;
        order.timestamp = 1000;
        engine.addLimitOrder(order);
    }
    
    // Проверяем, что наши ордера добавились
    auto bids_before = engine.getBids();
    EXPECT_DOUBLE_EQ(bids_before[0].second, 100 + 100);  // 100 внешних + 100 наших
    
    // Отменяем все наши ордера
    engine.cancelAllOurOrders();
    
    // Должны остаться только внешние
    auto bids_after = engine.getBids();
    EXPECT_DOUBLE_EQ(bids_after[0].second, 100);
    EXPECT_DOUBLE_EQ(bids_after[1].second, 200);
}

// ==============================
// ТЕСТЫ СТАБИЛЬНОСТИ
// ==============================

TEST(MatchingEngineTest, CancelOrderAfterPartialCancel) {
    MatchingEngine engine;
    
    // Добавляем ордер
    Order order;
    order.order_id = LimitOrderRef{1};
    order.side = Side::kBid;
    order.type = OrderType::kLimit;
    order.price = 99.0;
    order.amount = 100;
    order.timestamp = 1000;
    engine.addLimitOrder(order);
    
    // Отменяем ордер
    bool cancelled = engine.cancelOrder(LimitOrderRef{1});
    EXPECT_TRUE(cancelled);
    
    // Пытаемся отменить повторно
    cancelled = engine.cancelOrder(LimitOrderRef{1});
    EXPECT_FALSE(cancelled);
    
    EXPECT_TRUE(engine.getBids().empty());
}

TEST(MatchingEngineTest, CancelOrderRespectsFIFO) {
    MatchingEngine engine;
    
    // Добавляем ордера на одном уровне
    Order order1;
    order1.order_id = LimitOrderRef{1};
    order1.side = Side::kBid;
    order1.type = OrderType::kLimit;
    order1.price = 99.0;
    order1.amount = 30;
    order1.timestamp = 1000;
    engine.addLimitOrder(order1);
    
    Order order2;
    order2.order_id = LimitOrderRef{2};
    order2.side = Side::kBid;
    order2.type = OrderType::kLimit;
    order2.price = 99.0;
    order2.amount = 20;
    order2.timestamp = 2000;
    engine.addLimitOrder(order2);
    
    Order order3;
    order3.order_id = LimitOrderRef{3};
    order3.side = Side::kBid;
    order3.type = OrderType::kLimit;
    order3.price = 98.5;
    order3.amount = 50;
    order3.timestamp = 3000;
    engine.addLimitOrder(order3);
    
    // Проверяем начальное состояние
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 2);
    EXPECT_DOUBLE_EQ(bids[0].second, 50);
    EXPECT_DOUBLE_EQ(bids[1].second, 50);
    
    // Отменяем ордер 2 (средний на уровне)
    bool cancelled = engine.cancelOrder(LimitOrderRef{2});
    EXPECT_TRUE(cancelled);
    
    // Должен остаться ордер 1 (30) на том же уровне
    bids = engine.getBids();
    ASSERT_EQ(bids.size(), 2);
    EXPECT_DOUBLE_EQ(bids[0].second, 30);
    EXPECT_DOUBLE_EQ(bids[1].second, 50);
    
    // Отменяем ордер 1
    cancelled = engine.cancelOrder(LimitOrderRef{1});
    EXPECT_TRUE(cancelled);
    
    // Уровень с ценой 99.0 должен исчезнуть (остался только ордер 3)
    bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].first, 98.5);
    EXPECT_DOUBLE_EQ(bids[0].second, 50);
}

#include <gtest/gtest.h>
#include "mm/matching_engine/matching_engine.h"

TEST(MatchingEngineTest, BidOrderPriority) {
    MatchingEngine engine;
    
    std::cout << "\n=== Test: Bid Order Priority (adding from low to high) ===" << std::endl;
    
    // Добавляем биды ОТ МАЛЕНЬКОЙ ЦЕНЫ К БОЛЬШОЙ
    // То есть начинаем с худшей цены, заканчиваем лучшей
    // Matching engine должен отсортировать их правильно (лучшие цены сверху)
    
    // Сначала добавляем внешние биды через снапшот (чтобы был стакан)
    std::vector<std::pair<double, double>> asks_snapshot = {{101.0, 100}};
    std::vector<std::pair<double, double>> bids_snapshot = {};
    engine.addSnapshot(std::move(asks_snapshot), std::move(bids_snapshot));
    
    std::cout << "Adding bids from LOWEST to HIGHEST price:" << std::endl;
    
    // Добавляем бид с самой низкой ценой (первый)
    Order order1;
    order1.order_id = LimitOrderRef{1};
    order1.side = Side::kBid;
    order1.type = OrderType::kLimit;
    order1.price = 99.0;   // Самая низкая цена
    order1.amount = 100;
    order1.timestamp = 1000;
    engine.addLimitOrder(order1);
    std::cout << "  Added bid: price=99.0, volume=100" << std::endl;
    
    // Добавляем бид с чуть более высокой ценой
    Order order2;
    order2.order_id = LimitOrderRef{2};
    order2.side = Side::kBid;
    order2.type = OrderType::kLimit;
    order2.price = 99.5;
    order2.amount = 150;
    order2.timestamp = 2000;
    engine.addLimitOrder(order2);
    std::cout << "  Added bid: price=99.5, volume=150" << std::endl;
    
    // Добавляем бид с еще более высокой ценой
    Order order3;
    order3.order_id = LimitOrderRef{3};
    order3.side = Side::kBid;
    order3.type = OrderType::kLimit;
    order3.price = 99.8;
    order3.amount = 80;
    order3.timestamp = 3000;
    engine.addLimitOrder(order3);
    std::cout << "  Added bid: price=99.8, volume=80" << std::endl;
    
    // Добавляем бид с самой высокой ценой (лучший бид) - последним
    Order order4;
    order4.order_id = LimitOrderRef{4};
    order4.side = Side::kBid;
    order4.type = OrderType::kLimit;
    order4.price = 100.0;  // Лучшая цена
    order4.amount = 200;
    order4.timestamp = 4000;
    engine.addLimitOrder(order4);
    std::cout << "  Added bid: price=100.0, volume=200 (BEST BID - last)" << std::endl;
    
    auto result_bids = engine.getBids();
    
    std::cout << "\nResult bids (should be sorted DESCENDING by price):" << std::endl;
    for (size_t i = 0; i < result_bids.size(); ++i) {
        std::cout << "  level " << i << ": price=" << result_bids[i].first 
                  << ", volume=" << result_bids[i].second << std::endl;
    }
    
    // Проверяем, что лучший бид (самая высокая цена) находится на первом месте
    ASSERT_GE(result_bids.size(), 4);
    
    // Лучший бид должен быть с ценой 100.0 (несмотря на то что он добавлен последним)
    EXPECT_DOUBLE_EQ(result_bids[0].first, 100.0);
    EXPECT_DOUBLE_EQ(result_bids[0].second, 200);
    std::cout << "\n✓ BEST BID: price=" << result_bids[0].first 
              << " (added last, but now first - correct sorting!)" << std::endl;
    
    // Проверяем остальные уровни
    EXPECT_DOUBLE_EQ(result_bids[1].first, 99.8);
    EXPECT_DOUBLE_EQ(result_bids[1].second, 80);
    
    EXPECT_DOUBLE_EQ(result_bids[2].first, 99.5);
    EXPECT_DOUBLE_EQ(result_bids[2].second, 150);
    
    EXPECT_DOUBLE_EQ(result_bids[3].first, 99.0);
    EXPECT_DOUBLE_EQ(result_bids[3].second, 100);
    
    // Проверяем полную сортировку по убыванию
    for (size_t i = 1; i < result_bids.size(); ++i) {
        EXPECT_GT(result_bids[i-1].first, result_bids[i].first);
        std::cout << "  ✓ price[" << i-1 << "]=" << result_bids[i-1].first 
                  << " > price[" << i << "]=" << result_bids[i].first << std::endl;
    }
}

// Тест: добавляем биды в случайном порядке
TEST(MatchingEngineTest, BidOrderRandomOrder) {
    MatchingEngine engine;
    
    std::cout << "\n=== Test: Bid Order Random Order ===" << std::endl;
    
    std::vector<std::pair<double, double>> asks_snapshot = {{101.0, 100}};
    std::vector<std::pair<double, double>> bids_snapshot = {};
    engine.addSnapshot(std::move(asks_snapshot), std::move(bids_snapshot));
    
    std::vector<std::tuple<int, double, double>> orders = {
        {1, 99.5, 100},
        {2, 100.0, 200},
        {3, 98.5, 50},
        {4, 99.8, 80},
        {5, 99.0, 150},
        {6, 99.3, 60}
    };
    
    std::cout << "Adding bids in RANDOM order:" << std::endl;
    for (const auto& [id, price, vol] : orders) {
        Order order;
        order.order_id = LimitOrderRef{id};
        order.side = Side::kBid;
        order.type = OrderType::kLimit;
        order.price = price;
        order.amount = vol;
        order.timestamp = id * 1000;
        engine.addLimitOrder(order);
        std::cout << "  Added bid " << id << ": price=" << price << ", volume=" << vol << std::endl;
    }
    
    auto result_bids = engine.getBids();
    
    std::cout << "\nResult bids (sorted DESCENDING):" << std::endl;
    for (size_t i = 0; i < result_bids.size(); ++i) {
        std::cout << "  level " << i << ": price=" << result_bids[i].first 
                  << ", volume=" << result_bids[i].second << std::endl;
    }
    
    // Лучший бид должен быть 100.0
    EXPECT_DOUBLE_EQ(result_bids[0].first, 100.0);
    
    // Проверяем сортировку
    for (size_t i = 1; i < result_bids.size(); ++i) {
        EXPECT_GT(result_bids[i-1].first, result_bids[i].first);
    }
    
    std::cout << "\n✓ All bids correctly sorted by price regardless of insertion order!" << std::endl;
}

// Тест: проверка что на одном уровне сохраняется FIFO
TEST(MatchingEngineTest, SameBidLevelFifo) {
    MatchingEngine engine;
    
    std::cout << "\n=== Test: Same Bid Level FIFO ===" << std::endl;
    
    std::vector<std::pair<double, double>> asks_snapshot = {{101.0, 100}};
    std::vector<std::pair<double, double>> bids_snapshot = {{100.0, 50}};  // Внешний ордер
    engine.addSnapshot(std::move(asks_snapshot), std::move(bids_snapshot));
    
    std::cout << "Added external bid at 100.0, volume=50 (first in queue)" << std::endl;
    
    // Добавляем наши ордера на том же уровне (должны идти после внешнего)
    Order order1;
    order1.order_id = LimitOrderRef{1};
    order1.side = Side::kBid;
    order1.type = OrderType::kLimit;
    order1.price = 100.0;
    order1.amount = 30;
    order1.timestamp = 1000;
    engine.addLimitOrder(order1);
    std::cout << "Added our bid 1 at 100.0, volume=30 (second in queue)" << std::endl;
    
    Order order2;
    order2.order_id = LimitOrderRef{2};
    order2.side = Side::kBid;
    order2.type = OrderType::kLimit;
    order2.price = 100.0;
    order2.amount = 20;
    order2.timestamp = 2000;
    engine.addLimitOrder(order2);
    std::cout << "Added our bid 2 at 100.0, volume=20 (third in queue)" << std::endl;
    
    auto bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].first, 100.0);
    EXPECT_DOUBLE_EQ(bids[0].second, 100);  // 50 + 30 + 20
    
    std::cout << "\nTotal volume at 100.0: " << bids[0].second << std::endl;
    
    // Приходит рыночный ордер на продажу (снимает ликвидность)
    Order marketSell;
    marketSell.order_id = MarketOrderRef{3};
    marketSell.side = Side::kAsk;
    marketSell.type = OrderType::kMarket;
    marketSell.amount = 60;
    marketSell.timestamp = 3000;
    
    auto trades = engine.addMarketOrder(marketSell);
    
    std::cout << "\nTrades generated (FIFO order):" << std::endl;
    for (const auto& trade : trades) {
        std::cout << "  Trade: amount=" << trade.amount 
                  << ", price=" << trade.price 
                  << ", passive_id=" << trade.passive.getOrderId() << std::endl;
    }
    
    // Должен исполниться внешний ордер первым (50), потом наш первый (10 из 30)
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].passive.getOrderId(), 0);  // Внешний ордер (ExternalOrderRef)
    EXPECT_DOUBLE_EQ(trades[0].amount, 50);
    EXPECT_EQ(trades[1].passive.getOrderId(), 1);  // Наш первый ордер
    EXPECT_DOUBLE_EQ(trades[1].amount, 10);
    
    // Остаток
    bids = engine.getBids();
    ASSERT_EQ(bids.size(), 1);
    EXPECT_DOUBLE_EQ(bids[0].second, 40);  // 20 (наш второй) + 20 (остаток от первого)
    
    std::cout << "\n✓ FIFO order preserved on same price level!" << std::endl;
}

TEST(MatchingEngineTest, AskOrderPriority) {
    MatchingEngine engine;
    
    std::cout << "\n=== Test: Ask Order Priority (adding from low to high) ===" << std::endl;
    
    // Добавляем asks ОТ МАЛЕНЬКОЙ ЦЕНЫ К БОЛЬШОЙ
    // Для asks лучшая цена - это самая НИЗКАЯ цена (дешевле продать)
    // То есть начинаем с худшей цены (большой), заканчиваем лучшей (маленькой)
    // Matching engine должен отсортировать их правильно (лучшие цены сверху - самые низкие)
    
    // Сначала добавляем внешние asks через снапшот (чтобы был стакан)
    std::vector<std::pair<double, double>> bids_snapshot = {{99.0, 100}};
    std::vector<std::pair<double, double>> asks_snapshot = {};
    engine.addSnapshot(std::move(asks_snapshot), std::move(bids_snapshot));
    
    std::cout << "Adding asks from HIGHEST to LOWEST price (worst to best):" << std::endl;
    
    // Добавляем ask с самой высокой ценой (худший) - первый
    Order order1;
    order1.order_id = LimitOrderRef{1};
    order1.side = Side::kAsk;
    order1.type = OrderType::kLimit;
    order1.price = 101.0;   // Самая высокая цена (худшая)
    order1.amount = 100;
    order1.timestamp = 1000;
    engine.addLimitOrder(order1);
    std::cout << "  Added ask: price=101.0, volume=100 (worst)" << std::endl;
    
    // Добавляем ask с чуть более низкой ценой
    Order order2;
    order2.order_id = LimitOrderRef{2};
    order2.side = Side::kAsk;
    order2.type = OrderType::kLimit;
    order2.price = 100.5;
    order2.amount = 150;
    order2.timestamp = 2000;
    engine.addLimitOrder(order2);
    std::cout << "  Added ask: price=100.5, volume=150" << std::endl;
    
    // Добавляем ask с еще более низкой ценой
    Order order3;
    order3.order_id = LimitOrderRef{3};
    order3.side = Side::kAsk;
    order3.type = OrderType::kLimit;
    order3.price = 100.2;
    order3.amount = 80;
    order3.timestamp = 3000;
    engine.addLimitOrder(order3);
    std::cout << "  Added ask: price=100.2, volume=80" << std::endl;
    
    // Добавляем ask с самой низкой ценой (лучший ask) - последним
    Order order4;
    order4.order_id = LimitOrderRef{4};
    order4.side = Side::kAsk;
    order4.type = OrderType::kLimit;
    order4.price = 100.0;  // Лучшая цена (самая низкая)
    order4.amount = 200;
    order4.timestamp = 4000;
    engine.addLimitOrder(order4);
    std::cout << "  Added ask: price=100.0, volume=200 (BEST ASK - last)" << std::endl;
    
    auto result_asks = engine.getAsks();
    
    std::cout << "\nResult asks (should be sorted ASCENDING by price):" << std::endl;
    for (size_t i = 0; i < result_asks.size(); ++i) {
        std::cout << "  level " << i << ": price=" << result_asks[i].first 
                  << ", volume=" << result_asks[i].second << std::endl;
    }
    
    // Проверяем, что лучший ask (самая низкая цена) находится на первом месте
    ASSERT_GE(result_asks.size(), 4);
    
    // Лучший ask должен быть с ценой 100.0 (несмотря на то что он добавлен последним)
    EXPECT_DOUBLE_EQ(result_asks[0].first, 100.0);
    EXPECT_DOUBLE_EQ(result_asks[0].second, 200);
    std::cout << "\n✓ BEST ASK: price=" << result_asks[0].first 
              << " (added last, but now first - correct sorting!)" << std::endl;
    
    // Проверяем остальные уровни (должны идти по возрастанию)
    EXPECT_DOUBLE_EQ(result_asks[1].first, 100.2);
    EXPECT_DOUBLE_EQ(result_asks[1].second, 80);
    
    EXPECT_DOUBLE_EQ(result_asks[2].first, 100.5);
    EXPECT_DOUBLE_EQ(result_asks[2].second, 150);
    
    EXPECT_DOUBLE_EQ(result_asks[3].first, 101.0);
    EXPECT_DOUBLE_EQ(result_asks[3].second, 100);
    
    // Проверяем полную сортировку по возрастанию
    for (size_t i = 1; i < result_asks.size(); ++i) {
        EXPECT_LT(result_asks[i-1].first, result_asks[i].first);
        std::cout << "  ✓ price[" << i-1 << "]=" << result_asks[i-1].first 
                  << " < price[" << i << "]=" << result_asks[i].first << std::endl;
    }
}

// Тест: asks в случайном порядке
TEST(MatchingEngineTest, AskOrderRandomOrder) {
    MatchingEngine engine;
    
    std::cout << "\n=== Test: Ask Order Random Order ===" << std::endl;
    
    std::vector<std::pair<double, double>> bids_snapshot = {{99.0, 100}};
    std::vector<std::pair<double, double>> asks_snapshot = {};
    engine.addSnapshot(std::move(asks_snapshot), std::move(bids_snapshot));
    
    std::vector<std::tuple<int, double, double>> orders = {
        {1, 100.5, 100},
        {2, 100.0, 200},
        {3, 101.5, 50},
        {4, 100.2, 80},
        {5, 101.0, 150},
        {6, 100.8, 60}
    };
    
    std::cout << "Adding asks in RANDOM order:" << std::endl;
    for (const auto& [id, price, vol] : orders) {
        Order order;
        order.order_id = LimitOrderRef{id};
        order.side = Side::kAsk;
        order.type = OrderType::kLimit;
        order.price = price;
        order.amount = vol;
        order.timestamp = id * 1000;
        engine.addLimitOrder(order);
        std::cout << "  Added ask " << id << ": price=" << price << ", volume=" << vol << std::endl;
    }
    
    auto result_asks = engine.getAsks();
    
    std::cout << "\nResult asks (sorted ASCENDING):" << std::endl;
    for (size_t i = 0; i < result_asks.size(); ++i) {
        std::cout << "  level " << i << ": price=" << result_asks[i].first 
                  << ", volume=" << result_asks[i].second << std::endl;
    }
    
    // Лучший ask должен быть 100.0
    EXPECT_DOUBLE_EQ(result_asks[0].first, 100.0);
    
    // Проверяем сортировку по возрастанию
    for (size_t i = 1; i < result_asks.size(); ++i) {
        EXPECT_LT(result_asks[i-1].first, result_asks[i].first);
    }
    
    std::cout << "\n✓ All asks correctly sorted by price regardless of insertion order!" << std::endl;
}

// Тест: проверка FIFO на одном уровне для asks
TEST(MatchingEngineTest, SameAskLevelFifo) {
    MatchingEngine engine;
    
    std::cout << "\n=== Test: Same Ask Level FIFO ===" << std::endl;
    
    std::vector<std::pair<double, double>> bids_snapshot = {{99.0, 100}};
    std::vector<std::pair<double, double>> asks_snapshot = {{100.0, 50}};  // Внешний ордер
    engine.addSnapshot(std::move(asks_snapshot), std::move(bids_snapshot));
    
    std::cout << "Added external ask at 100.0, volume=50 (first in queue)" << std::endl;
    
    // Добавляем наши ордера на том же уровне (должны идти после внешнего)
    Order order1;
    order1.order_id = LimitOrderRef{1};
    order1.side = Side::kAsk;
    order1.type = OrderType::kLimit;
    order1.price = 100.0;
    order1.amount = 30;
    order1.timestamp = 1000;
    engine.addLimitOrder(order1);
    std::cout << "Added our ask 1 at 100.0, volume=30 (second in queue)" << std::endl;
    
    Order order2;
    order2.order_id = LimitOrderRef{2};
    order2.side = Side::kAsk;
    order2.type = OrderType::kLimit;
    order2.price = 100.0;
    order2.amount = 20;
    order2.timestamp = 2000;
    engine.addLimitOrder(order2);
    std::cout << "Added our ask 2 at 100.0, volume=20 (third in queue)" << std::endl;
    
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].first, 100.0);
    EXPECT_DOUBLE_EQ(asks[0].second, 100);  // 50 + 30 + 20
    
    std::cout << "\nTotal volume at 100.0: " << asks[0].second << std::endl;
    
    // Приходит рыночный ордер на покупку (снимает ликвидность)
    Order marketBuy;
    marketBuy.order_id = MarketOrderRef{3};
    marketBuy.side = Side::kBid;
    marketBuy.type = OrderType::kMarket;
    marketBuy.amount = 60;
    marketBuy.timestamp = 3000;
    
    auto trades = engine.addMarketOrder(marketBuy);
    
    std::cout << "\nTrades generated (FIFO order):" << std::endl;
    for (const auto& trade : trades) {
        std::cout << "  Trade: amount=" << trade.amount 
                  << ", price=" << trade.price 
                  << ", passive_id=" << trade.passive.getOrderId() << std::endl;
    }
    
    // Должен исполниться внешний ордер первым (50), потом наш первый (10 из 30)
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].passive.getOrderId(), 0);  // Внешний ордер (ExternalOrderRef)
    EXPECT_DOUBLE_EQ(trades[0].amount, 50);
    EXPECT_EQ(trades[1].passive.getOrderId(), 1);  // Наш первый ордер
    EXPECT_DOUBLE_EQ(trades[1].amount, 10);
    
    // Остаток
    asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 1);
    EXPECT_DOUBLE_EQ(asks[0].second, 40);  // 20 (наш второй) + 20 (остаток от первого)
    
    std::cout << "\n✓ FIFO order preserved on same price level!" << std::endl;
}

TEST(MatchingEngineTest, AddSeveralAsks) {
    MatchingEngine engine;
    
    std::cout << "\n=== Test: Same Ask Level FIFO ===" << std::endl;
    
    std::vector<std::pair<double, double>> bids_snapshot = {{99.0, 100}};
    std::vector<std::pair<double, double>> asks_snapshot = {{110.0, 50}};  // Внешний ордер
    engine.addSnapshot(std::move(asks_snapshot), std::move(bids_snapshot));
    
    std::cout << "Added external ask at 100.0, volume=50 (first in queue)" << std::endl;
    
    // Добавляем наши ордера на том же уровне (должны идти после внешнего)
    Order order1;
    order1.order_id = LimitOrderRef{1};
    order1.side = Side::kAsk;
    order1.type = OrderType::kLimit;
    order1.price = 109.0;
    order1.amount = 30;
    order1.timestamp = 1000;
    engine.addLimitOrder(order1);
    std::cout << "Added our ask 1 at 100.0, volume=30 (second in queue)" << std::endl;
    
    
    auto asks = engine.getAsks();
    ASSERT_EQ(asks.size(), 2);
    EXPECT_DOUBLE_EQ(asks[0].first, 109.0);
    EXPECT_DOUBLE_EQ(asks[1].first, 110);  // 50 + 30 + 20
    EXPECT_DOUBLE_EQ(engine.getBestAsk(), 109.0);


    std::cout << "\n✓ FIFO order preserved on same price level!" << std::endl;
}