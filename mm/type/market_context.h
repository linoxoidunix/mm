#pragma once
#include "mm/type/me_l2snapshot.h"

// Структура для передачи текущего состояния рынка стратегии
struct MarketContext {
    MEL2Snapshot l2;
    double inventory;
    double cash;
    int64_t start_timestamp;
    int64_t current_timestamp;
};