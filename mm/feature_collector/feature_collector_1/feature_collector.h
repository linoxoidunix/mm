#pragma once

#include <vector>
#include <deque>
#include <cmath>

#include "mm/feature_collector/i_feature_collector.h"
#include "mm/type/fill.h"
#include "mm/type/me_l2snapshot.h"
#include "mm/type/market_context.h"
#include "mm/constants/constants.h"

struct Features {
    double mid;
    double spread;
    double microprice;

    double imbalance1;
    double imbalance5;

    double signed_volume;
    double trade_imbalance;

    double inventory;
    double inv_norm;

    double volatility;
};

struct FeatureCollectorConfig{
    double max_inventory = 0;
};

class MyFeatureCollector : public IFeatureCollector {
public:
    MyFeatureCollector(FeatureCollectorConfig &&_config) : config(std::move(_config)){}
    
    void onMarketContext(const MarketContext& ctx) override {
        if (ctx.l2.bids.empty() || ctx.l2.asks.empty()) return;

        double best_bid = ctx.l2.bids[0].first;
        double best_ask = ctx.l2.asks[0].first;

        double mid = (best_bid + best_ask) / 2.0;
        double spread = best_ask - best_bid;

        double microprice = computeMicroprice(ctx.l2);

        double imb1 = computeImbalance1(ctx.l2);
        double imb5 = computeImbalance5(ctx.l2);

        double inv = ctx.inventory;
        double inv_norm = inv / config.max_inventory;

        double vol = updateVolatility(mid);

        Features f{
            mid,
            spread,
            microprice,
            imb1,
            imb5,
            signed_volume_rolling,
            trade_imbalance_rolling,
            inv,
            inv_norm,
            vol
        };

        features.push_back(f);

        last_mid = mid;
    }

    void onFill(const Fill& fill) override {
        double sign = (fill.side == Side::kBid ? 1 : -1) * fill.amount;

        signed_volume_rolling += sign;

        if (fill.side == Side::kBid)
            buy_vol += fill.amount;
        else
            sell_vol += fill.amount;

        trade_imbalance_rolling = buy_vol - sell_vol;
    }

private:
    std::vector<Features> features;

    double last_mid = 0.0;

    double signed_volume_rolling = 0.0;
    double trade_imbalance_rolling = 0.0;

    double buy_vol = 0.0;
    double sell_vol = 0.0;

    FeatureCollectorConfig config;

    std::deque<double> mid_window;

    // ----------------------

    double computeMicroprice(const MEL2Snapshot& snap) const {
        if (snap.bids.empty() || snap.asks.empty()) return 0.0;
        
        double best_bid = snap.bids[0].first;
        double best_ask = snap.asks[0].first;
        
        // Взвешенный объем на первом уровне
        double bid_volume = snap.bids[0].second;
        double ask_volume = snap.asks[0].second;
        
        // Учет второго уровня (если есть) для стабильности
        if (snap.bids.size() > 1) bid_volume += snap.bids[1].second;
        if (snap.asks.size() > 1) ask_volume += snap.asks[1].second;
        
        double total_volume = bid_volume + ask_volume;
        if (total_volume < kVolEps) return (best_bid + best_ask) / 2.0;
        
        return (best_bid * ask_volume + best_ask * bid_volume) / total_volume;
    }

    double computeImbalance1(const MEL2Snapshot& snap) {
        double bid = snap.bids[0].second;
        double ask = snap.asks[0].second;
        return (bid - ask) / (bid + ask + 1e-12);
    }

    double computeImbalance5(const MEL2Snapshot& snap) {
        double bid_sum = 0, ask_sum = 0;

        for (int i = 0; i < 5; i++) {
            if (i < snap.bids.size()) bid_sum += snap.bids[i].second;
            if (i < snap.asks.size()) ask_sum += snap.asks[i].second;
        }

        return (bid_sum - ask_sum) / (bid_sum + ask_sum + 1e-12);
    }

    double updateVolatility(double mid) {
        mid_window.push_back(mid);
        if (mid_window.size() > 50)
            mid_window.pop_front();

        if (mid_window.size() < 10)
            return 0.0;

        double mean = 0;
        for (double x : mid_window) mean += x;
        mean /= mid_window.size();

        double var = 0;
        for (double x : mid_window)
            var += (x - mean) * (x - mean);

        return std::sqrt(var / mid_window.size());
    }
};