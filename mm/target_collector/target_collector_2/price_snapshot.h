#pragma once

// Компактная структура для хранения только данных о цене
struct PriceSnapshot {
    float mid_price;   // Используем float для экономии памяти, если точности достаточно
    float microprice;
};