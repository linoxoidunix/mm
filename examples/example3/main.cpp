#include <iostream>
#include <vector>
#include <tuple>
#include <fstream>
#include <iomanip>

#include "mm/hftbacktester/hftbacktester.h"
#include "mm/strategy/avelaneda_stoikov/avalaneda_stoikov_1.h"

#include "mm/strategy/avelaneda_stoikov/config.h"

// Структура для хранения результатов одного прогона
struct BacktestResult {
    std::string name;
    double gamma;
    double kappa;
    double sigma;
    double order_size;
    double final_equity;
    double turnover;
    double fill_rate;
    double max_drawdown;
    double sharpe;
    int total_orders;
    int total_fills;
    double avg_inventory;
};

// Функция для запуска одного бектеста с заданными параметрами
BacktestResult runBacktest(HFTBacktester& backtester,
                           const std::string& name, 
                           double gamma, 
                           double kappa, 
                           double sigma,
                           double order_size,
                           const std::string& l2_file,
                           const std::string& trade_file) {
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Running: " << name << std::endl;
    std::cout << "  gamma=" << gamma << ", kappa=" << kappa 
              << ", sigma=" << sigma << ", order_size=" << order_size << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    //HFTBacktesterConfig backtester_config;
    //HFTBacktester backtester(std::move(backtester_config));
    
    ASParams config = {gamma, kappa, sigma, 1.0, 5000.0};
    auto strategy = std::make_unique<AvellanedaStoikov1>(config, order_size);
    backtester.setStrategy(std::move(strategy));
    
    //backtester.loadL2Data(l2_file);
    //backtester.loadTradeData(trade_file);
    
    backtester.runStrategy();
    
    // Собираем результаты (нужно добавить геттеры в HFTBacktester)
    BacktestResult result;
    result.name = name;
    result.gamma = gamma;
    result.kappa = kappa;
    result.sigma = sigma;
    result.order_size = order_size;
    result.final_equity = backtester.getEquity();
    result.turnover = backtester.getTurnover();
    result.total_orders = backtester.getTotalOrdersPlaced();
    result.total_fills = backtester.getTotalFills();
    result.fill_rate = (result.total_orders > 0) ? 
                        (100.0 * result.total_fills / result.total_orders) : 0.0;
    
    // Эти метрики нужно добавить в HFTBacktester
    // result.max_drawdown = backtester.getMaxDrawdown();
    // result.sharpe = backtester.getSharpeRatio();
    // result.avg_inventory = backtester.getAvgInventory();
    
    return result;
}

// Функция для сохранения результатов в CSV
void saveResultsToCSV(const std::vector<BacktestResult>& results, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return;
    }
    
    // Заголовки
    file << "name,gamma,kappa,sigma,order_size,final_pnl,turnover,total_orders,total_fills,fill_rate\n";
    
    // Данные
    for (const auto& r : results) {
        file << r.name << ","
             << r.gamma << ","
             << r.kappa << ","
             << r.sigma << ","
             << r.order_size << ","
             << r.final_equity << ","
             << r.turnover << ","
             << r.total_orders << ","
             << r.total_fills << ","
             << r.fill_rate << "\n";
    }
    
    file.close();
    std::cout << "Results saved to: " << filename << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     CMF HFT Backtester - Avellaneda-Stoikov 2008/2018    ║\n";
    std::cout << "║           Parameter Sweep - Batch Testing                ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    
    // Default file names
    std::string l2_file = "../../../data/synced_lob.csv";
    std::string trade_file = "../../../data/synced_trades.csv";
    
    // Override from command line
    if (argc >= 2) l2_file = argv[1];
    if (argc >= 3) trade_file = argv[2];
    
    std::vector<BacktestResult> results;

    HFTBacktesterConfig backtester_config;
    HFTBacktester backtester(std::move(backtester_config));
       
    backtester.loadL2Data(l2_file);
    backtester.loadTradeData(trade_file);
    
    // ============================================================
    // ПАРАМЕТРЫ ДЛЯ ТЕСТИРОВАНИЯ
    // ============================================================
    
    // 1. Тестирование разных gamma (риск-аверсность)
    std::vector<double> gamma_values = {0.1, 0.5, 1.0, 2.0, 5.0, 10.0};
    double base_kappa = 1000000.0;
    double base_sigma = 0.0002;
    double base_order_size = 50.0;
    
    std::cout << "\n=== Testing different gamma values ===" << std::endl;
    for (double gamma : gamma_values) {
        std::string name = "gamma_" + std::to_string(gamma);
        auto result = runBacktest(backtester, name, gamma, base_kappa, base_sigma, base_order_size, l2_file, trade_file);
        results.push_back(result);
    }
    
    // 2. Тестирование разных kappa (интенсивность заявок)
    std::vector<double> kappa_values = {10000, 100000, 500000, 1000000, 5000000, 10000000};
    double base_gamma = 0.5;
    
    std::cout << "\n=== Testing different kappa values ===" << std::endl;
    for (double kappa : kappa_values) {
        std::string name = "kappa_" + std::to_string(kappa);
        auto result = runBacktest(backtester, name, base_gamma, kappa, base_sigma, base_order_size, l2_file, trade_file);
        results.push_back(result);
    }
    
    // 3. Тестирование разных sigma (волатильность)
    std::vector<double> sigma_values = {0.0001, 0.0002, 0.0005, 0.001, 0.002, 0.005};
    
    std::cout << "\n=== Testing different sigma values ===" << std::endl;
    for (double sigma : sigma_values) {
        std::string name = "sigma_" + std::to_string(sigma);
        auto result = runBacktest(backtester, name, base_gamma, base_kappa, sigma, base_order_size, l2_file, trade_file);
        results.push_back(result);
    }
    
    // 4. Тестирование разных размеров ордеров
    std::vector<double> order_sizes = {10, 25, 50, 100, 200, 500};
    
    std::cout << "\n=== Testing different order sizes ===" << std::endl;
    for (double size : order_sizes) {
        std::string name = "size_" + std::to_string(size);
        auto result = runBacktest(backtester, name, base_gamma, base_kappa, base_sigma, size, l2_file, trade_file);
        results.push_back(result);
    }
    
    // 5. Оптимальные комбинации (grid search)
    std::vector<std::tuple<double, double, double, double>> grid_params = {
        {0.2, 500000, 0.0002, 30},
        {0.5, 1000000, 0.0003, 50},
        {1.0, 2000000, 0.0005, 75},
        {2.0, 5000000, 0.001, 100},
        {0.3, 750000, 0.00025, 40},
        {0.7, 1500000, 0.0004, 60},
        {1.5, 3000000, 0.0008, 80},
        {3.0, 8000000, 0.0015, 120}
    };
    
    std::cout << "\n=== Grid search - optimal combinations ===" << std::endl;
    int idx = 1;
    for (const auto& [g, k, s, sz] : grid_params) {
        std::string name = "grid_" + std::to_string(idx++);
        auto result = runBacktest(backtester, name, g, k, s, sz, l2_file, trade_file);
        results.push_back(result);
    }
    
    // Сохранение результатов
    saveResultsToCSV(results, "backtest_results_summary.csv");
    
    // Вывод лучших результатов
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "BEST RESULTS SUMMARY" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    
    // Находим лучший по PnL
    auto best_pnl = std::max_element(results.begin(), results.end(),
        [](const BacktestResult& a, const BacktestResult& b) {
            return a.final_equity < b.final_equity;
        });
    
    // Находим лучший по fill rate
    auto best_fill = std::max_element(results.begin(), results.end(),
        [](const BacktestResult& a, const BacktestResult& b) {
            return a.fill_rate < b.fill_rate;
        });
    
    // Находим лучший по Sharpe (если есть)
    
    std::cout << "\n🏆 Best PnL: " << best_pnl->name << std::endl;
    std::cout << "   gamma=" << best_pnl->gamma << ", kappa=" << best_pnl->kappa 
              << ", sigma=" << best_pnl->sigma << ", size=" << best_pnl->order_size << std::endl;
    std::cout << "   PnL=$" << best_pnl->final_equity << ", Fill Rate=" << best_pnl->fill_rate << "%" << std::endl;
    
    std::cout << "\n📈 Best Fill Rate: " << best_fill->name << std::endl;
    std::cout << "   gamma=" << best_fill->gamma << ", kappa=" << best_fill->kappa 
              << ", sigma=" << best_fill->sigma << ", size=" << best_fill->order_size << std::endl;
    std::cout << "   Fill Rate=" << best_fill->fill_rate << "%, PnL=$" << best_fill->final_equity << std::endl;
    
    std::cout << "\nDone. Results saved to backtest_results_summary.csv\n";
    
    return 0;
}