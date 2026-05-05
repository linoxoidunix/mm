#include <iostream>
#include <vector>
#include <tuple>
#include <fstream>
#include <iomanip>
#include <filesystem>

#include "mm/hftbacktester/hftbacktester.h"
#include "mm/strategy/avelaneda_stoikov/avalaneda_stoikov_1.h"

#include "mm/strategy/avelaneda_stoikov/config.h"

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

int main(int argc, char* argv[]) {
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
   
    std::filesystem::remove("test_l2.csv");
    std::filesystem::remove("test_trade.csv");
    
    return 0;
}