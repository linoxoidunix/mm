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
    PriceSnapshot target_data; // Та самая компактная структура из MyTargetCollector
};

class DataCollector : public IDataCollector {
public:
    DataCollector(std::unique_ptr<IFeatureCollector> fc, 
                  std::unique_ptr<ITargetCollector> tc)
        : feature_collector_(std::move(fc)), 
          target_collector_(std::move(tc)) {
        
        // Резервируем память для предотвращения частых аллокаций
        dataset_.reserve(10000000);
    }

    void onTick(const MarketContext& ctx) override {
        // 1. Обновляем внутренние состояния коллекторов
        feature_collector_->onTick(ctx);
        target_collector_->onTick(ctx);

        // 2. Извлекаем данные за текущий тик
        // Предполагаем, что у ваших классов есть методы доступа к последнему состоянию
       
    }

    void onFill(const Fill& fill) override {
        // Оба коллектора должны знать о сделках для корректности метрик
        feature_collector_->onFill(fill);
        target_collector_->onFill(fill);
    }

    const std::vector<DataRow>& get_dataset() const {
        return dataset_;
    }

    void clear() {
        dataset_.clear();
    }

private:
    std::unique_ptr<IFeatureCollector> feature_collector_;
    std::unique_ptr<ITargetCollector> target_collector_;
    
    std::vector<DataRow> dataset_;
};