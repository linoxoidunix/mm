#pragma once

// Avellaneda-Stoikov parameters
// используется для hft_backtester
// этот кофиг старый
// удалить
struct ASParamsOld {
    double gamma;      // risk aversion
    double kappa;      // resilience (order flow intensity)
    double sigma;      // volatility
    double T;          // time horizon (seconds)
    double inventory_target;
    
    ASParamsOld() : gamma(0.1), kappa(1.5), sigma(0.0001), 
                 T(3600.0), inventory_target(0.0) {}
};