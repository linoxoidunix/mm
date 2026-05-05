#include <gtest/gtest.h>
#include <fstream>
#include "mm/order_book/order_book.h"
#include "mm/type/external_trade.h"

TEST(OrderBookTest, ApplyL2) {
    OrderBook ob;
    
    // Создаем L2 снимок
    L2Snapshot snap;
    snap.timestamp = 1000;
    snap.bids = {{99.5, 100}, {99.0, 200}, {98.5, 150}};
    snap.asks = {{100.0, 100}, {100.5, 200}, {101.0, 150}};
    
    ob.applyL2(snap);
    
    EXPECT_EQ(ob.getBestBid(), 99.5);
    EXPECT_EQ(ob.getBestAsk(), 100.0);
    EXPECT_EQ(ob.getBestBidVolume(), 100);
    EXPECT_EQ(ob.getBestAskVolume(), 100);
    EXPECT_EQ(ob.getTimestamp(), 1000);
}

TEST(OrderBookTest, ApplyTradeBuy) {
    OrderBook ob;
    
    L2Snapshot snap;
    snap.timestamp = 1000;
    snap.asks = {{100.0, 100}, {100.5, 200}};
    ob.applyL2(snap);
    
    // Трейд покупки (снимает asks)
    ExternalTrade trade;
    trade.timestamp = 1500;
    trade.side = TradeSide::kBuy;
    trade.price = 100.0;
    trade.amount = 60;
    
    ob.applyTrade(trade);
    
    EXPECT_DOUBLE_EQ(ob.getBestAsk(), 100.0);
    EXPECT_DOUBLE_EQ(ob.getBestAskVolume(), 40);  // 100 - 60 = 40
}

TEST(OrderBookTest, ApplyTradeBuyMultipleLevels) {
    OrderBook ob;
    
    L2Snapshot snap;
    snap.timestamp = 1000;
    snap.asks = {{100.0, 30}, {100.5, 50}, {101.0, 100}};
    ob.applyL2(snap);
    
    // Трейд на 70 снимает первый уровень (30) и часть второго (40)
    ExternalTrade trade;
    trade.timestamp = 1500;
    trade.side = TradeSide::kBuy;
    trade.amount = 70;
    
    ob.applyTrade(trade);
    
    const auto& asks = ob.getAsks();
    ASSERT_EQ(asks.size(), 2);
    EXPECT_DOUBLE_EQ(asks[0].price, 100.5);
    EXPECT_DOUBLE_EQ(asks[0].volume, 10);  // 50 - 40 = 10
    EXPECT_DOUBLE_EQ(asks[1].price, 101.0);
    EXPECT_DOUBLE_EQ(asks[1].volume, 100);
}

TEST(OrderBookTest, ApplyTradeSell) {
    OrderBook ob;
    
    L2Snapshot snap;
    snap.timestamp = 1000;
    snap.bids = {{99.5, 100}, {99.0, 200}};
    ob.applyL2(snap);
    
    // Трейд продажи (снимает bids)
    ExternalTrade trade;
    trade.timestamp = 1500;
    trade.side = TradeSide::kSell;
    trade.amount = 60;
    
    ob.applyTrade(trade);
    
    EXPECT_DOUBLE_EQ(ob.getBestBid(), 99.5);
    EXPECT_DOUBLE_EQ(ob.getBestBidVolume(), 40);  // 100 - 60 = 40
}

TEST(OrderBookTest, ApplyTradeSellMultipleLevels) {
    OrderBook ob;
    
    L2Snapshot snap;
    snap.timestamp = 1000;
    snap.bids = {{99.5, 30}, {99.0, 50}, {98.5, 100}};
    ob.applyL2(snap);
    
    ExternalTrade trade;
    trade.timestamp = 1500;
    trade.side = TradeSide::kSell;
    trade.amount = 70;
    
    ob.applyTrade(trade);
    
    const auto& bids = ob.getBids();
    ASSERT_EQ(bids.size(), 2);
    EXPECT_DOUBLE_EQ(bids[0].price, 99.0);
    EXPECT_DOUBLE_EQ(bids[0].volume, 10);  // 50 - 40 = 10
    EXPECT_DOUBLE_EQ(bids[1].price, 98.5);
    EXPECT_DOUBLE_EQ(bids[1].volume, 100);
}

TEST(OrderBookTest, SequentialEvents) {
    OrderBook ob;
    
    // L2 #1
    L2Snapshot snap1;
    snap1.timestamp = 1000;
    snap1.asks = {{100.0, 100}};
    snap1.bids = {{99.5, 100}};
    ob.applyL2(snap1);
    
    // ExternalTrade покупки 30
    ExternalTrade trade1;
    trade1.timestamp = 1500;
    trade1.side = TradeSide::kBuy;
    trade1.amount = 30;
    ob.applyTrade(trade1);
    
    EXPECT_DOUBLE_EQ(ob.getBestAskVolume(), 70);
    
    // L2 #2 - новый снимок (маркет изменился)
    L2Snapshot snap2;
    snap2.timestamp = 2000;
    snap2.asks = {{101.0, 200}};
    snap2.bids = {{100.5, 200}};
    ob.applyL2(snap2);
    
    EXPECT_DOUBLE_EQ(ob.getBestAsk(), 101.0);
    EXPECT_DOUBLE_EQ(ob.getBestAskVolume(), 200);
    
    // ExternalTrade продажи 50
    ExternalTrade trade2;
    trade2.timestamp = 2500;
    trade2.side = TradeSide::kSell;
    trade2.amount = 50;
    ob.applyTrade(trade2);
    
    EXPECT_DOUBLE_EQ(ob.getBestBidVolume(), 150);
}

TEST(OrderBookTest, TradeLargerThanAvailable) {
    OrderBook ob;
    
    L2Snapshot snap;
    snap.timestamp = 1000;
    snap.asks = {{100.0, 30}};
    ob.applyL2(snap);
    
    // Трейд больше доступного объема
    ExternalTrade trade;
    trade.timestamp = 1500;
    trade.side = TradeSide::kBuy;
    trade.amount = 50;
    
    ob.applyTrade(trade);
    
    // Весь ask должен быть снят
    EXPECT_TRUE(ob.getAsks().empty());
}