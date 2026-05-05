#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include "mm/type/l2snapshot.h"
#include "mm/type/external_trade.h"
#include "mm/util/to_num.h"
#include "mm/constants/constants.h"

class HistoryManager {

public:
    HistoryManager(size_t _reserve_size = 10'000'000):reserve_size(_reserve_size){
        l2_data_.reserve(reserve_size);
        trade_data_.reserve(reserve_size);
    };
    
    // Загрузка данных
    void loadL2Data(const std::string& filename) {
        l2_data_.clear();
        std::ifstream file(filename);
        if (!file.is_open()) return;
        
        std::string line;
        bool first = true;
        while (std::getline(file, line)) {
            if (line.empty() || first) { first = false; continue; }
            L2Snapshot snap = parseL2Line(line);
            l2_data_.emplace_back(std::move(snap));
        }
    }
    
    void loadTradeData(const std::string& filename) {
        trade_data_.clear();
        std::ifstream file(filename);
        if (!file.is_open()) return;
        
        std::string line;
        bool first = true;
        while (std::getline(file, line)) {
            if (line.empty() || first) { first = false; continue; }
            ExternalTrade trade = parseTradeLine(line);
            trade_data_.emplace_back(std::move(trade));
        }
    }
    
    // Доступ к данным
    const std::vector<L2Snapshot>& getL2Data() const { return l2_data_; }
    const std::vector<ExternalTrade>& getTradeData() const { return trade_data_; }
    
    size_t getL2Size() const { return l2_data_.size(); }
    size_t getTradeSize() const { return trade_data_.size(); }
    size_t getTotalEvents() const { return l2_data_.size() + trade_data_.size(); }
    
    // Получение конкретных событий

    //unsafe индекс не пррверяется
    const L2Snapshot& getL2(size_t idx) const { return l2_data_[idx]; }
    //unsafe индекс не пррверяется
    const ExternalTrade& getTrade(size_t idx) const { return trade_data_[idx]; }
    
    // Очистка
    void clear() {
        l2_data_.clear();
        trade_data_.clear();
    }
    
private:
    size_t reserve_size;
    std::vector<L2Snapshot> l2_data_;
    std::vector<ExternalTrade> trade_data_;
    
    L2Snapshot parseL2Line(const std::string& line) const {
        L2Snapshot snap;
        std::string_view sv(line);
        size_t pos = 0;
        
        auto next = [&](size_t& p) -> std::string_view {
            if (p >= sv.size()) return "";
            size_t start = p; 
            size_t end = sv.find(',', p);
            if (end == std::string_view::npos) { 
                p = sv.size(); 
                return sv.substr(start); 
            }
            p = end + 1; 
            return sv.substr(start, end - start);
        };
        
        // 1. Обязательные поля
        snap.row = to_num<uint32_t>(next(pos));
        snap.timestamp = to_num<uint64_t>(next(pos));
        
        // 2. Читаем пары цена/объем, пока не кончится строка
        while (pos < sv.size()) {
            // Пытаемся считать Ask
            std::string_view a_p_sv = next(pos);
            if (a_p_sv.empty()) break; // Конец строки
            double a_p = to_num<double>(a_p_sv);
            double a_v = to_num<double>(next(pos));
            
            // Пытаемся считать Bid
            double b_p = to_num<double>(next(pos));
            double b_v = to_num<double>(next(pos));
            
            // Добавляем только если цена не 0 (традиционный признак пустого уровня)
            if (a_p > kPriceEps) snap.asks.emplace_back(a_p, a_v);
            if (b_p > kPriceEps) snap.bids.emplace_back(b_p, b_v);
        }
        
        return snap;
    }
    
    ExternalTrade parseTradeLine(const std::string& line) const {
        std::string_view sv(line);
        size_t pos = 0;
        
        auto next = [&](size_t& p) {
            size_t start = p; 
            size_t end = sv.find(',', p);
            p = (end == std::string_view::npos) ? sv.size() : end + 1;
            return sv.substr(start, (end == std::string_view::npos) ? sv.size() - start : end - start);
        };
        
        uint64_t row = to_num<uint64_t>(next(pos));
        int64_t ts = to_num<int64_t>(next(pos));
        auto s_sv = next(pos);
        TradeSide side = (!s_sv.empty() && s_sv[0] == 'b') ? TradeSide::kBuy : TradeSide::kSell;
        double pr = to_num<double>(next(pos));
        double am = to_num<double>(next(pos));
        
        return ExternalTrade{row, ts, side, pr, am};
    }
};