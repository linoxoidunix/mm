#pragma once

#include <vector>
#include <memory>
#include "mm/type/market_context.h"
#include "mm/type/fill.h"

// Предполагаем наличие интерфейсов ваших коллекторов
#include "mm/data_collector/i_data_collector.h"
#include "mm/target_collector/target_collector_1/price_snapshot.h"
#include "mm/feature_collector/i_feature_collector.h"
#include "mm/target_collector/i_target_collector.h"

// Структура, объединяющая результат одного шага
struct DataRow {
    std::vector<double> features;
    std::vector<double> targets; // Та самая компактная структура из MyTargetCollector
};

class DataCollector : public IDataCollector {
public:
    DataCollector(std::unique_ptr<IFastFeatureCollector> fc, 
                  std::unique_ptr<IFastTargetCollector> tc)
        : feature_collector_(std::move(fc)), 
          target_collector_(std::move(tc)) {
        
        // Резервируем память для предотвращения частых аллокаций
        dataset_.reserve(10000000);
    }

    void onMarketContext(const MarketContext& ctx) override {
        // 1. Обновляем внутренние состояния коллекторов
        auto features = feature_collector_->onMarketContext(ctx);
        auto targets = target_collector_->onMarketContext(ctx);

        // 2. Извлекаем данные за текущий тик
        // Предполагаем, что у ваших классов есть методы доступа к последнему состоянию
        DataRow row{std::move(features), std::move(targets)};

        dataset_.push_back(std::move(row));
    }

    void onFill(const Fill& fill) override {
        // Оба коллектора должны знать о сделках для корректности метрик
        auto features = feature_collector_->onFill(fill);
        target_collector_->onFill(fill);
    }

    const std::vector<DataRow>& get_dataset() const {
        return dataset_;
    }

    void clear() {
        dataset_.clear();
    }

private:
    std::unique_ptr<IFastFeatureCollector> feature_collector_;
    std::unique_ptr<IFastTargetCollector> target_collector_;
    
    std::vector<DataRow> dataset_;
};