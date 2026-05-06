#include <iostream>
#include <vector>
#include <tuple>
#include <fstream>
#include <iomanip>
#include <thread>
#include <future>
#include <atomic>

#include "mm/hftbacktester/hftbacktester.h"
#include "mm/history_manager/history_manager.h"
#include "mm/strategy/avelaneda_stoikov/avalaneda_stoikov_microprice.h"
#include "mm/strategy/avelaneda_stoikov/config.h"

// Структура для хранения результатов одного прогона
struct BacktestResult {
    std::string name;
    double gamma;
    double kappa;
    double sigma;
    double order_size;
    double max_inventory;
    double final_equity;
    double total_profit;
    double realized_pnl;
    double unrealized_pnl;
    double turnover;
    double fill_rate;
    double max_drawdown;
    int total_orders;
    int total_fills;
    double avg_inventory;
    double final_inventory;
    double avg_entry_price;
    int64_t execution_time_ms;
    double perfomance;// = execution_time_ms / history_manager.getTotalEvents
};

// Атомарные счетчики
std::atomic<int> completed_count{0};
std::atomic<int> total_count{0};

// Функция для запуска одного бектеста
BacktestResult runBacktest(std::shared_ptr<HistoryManager> hm,
                           const std::string& name, 
                           double gamma, 
                           double kappa, 
                           double sigma,
                           double order_size,
                           double max_inventory) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Создаем бектестер с общим HistoryManager
    HFTBacktesterConfig config;
    config.cash = 7000.0;
    config.inventory = 0.0;
    HFTBacktester backtester(std::move(config), hm);
    
    // Настраиваем стратегию с учетом max_inventory
    ASParams params = {gamma, kappa, sigma, 1.0, max_inventory};
    auto strategy = std::make_unique<AvellanedaStoikovMicroPrice>(params, order_size);
    backtester.setStrategy(std::move(strategy));
    
    // Запускаем стратегию
    backtester.runStrategy();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto execution_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    // Собираем результаты
    BacktestResult result;
    result.name = name;
    result.gamma = gamma;
    result.kappa = kappa;
    result.sigma = sigma;
    result.order_size = order_size;
    result.max_inventory = max_inventory;
    result.final_equity = backtester.getEquity();
    result.total_profit = result.final_equity - 7000.0;
    result.realized_pnl = backtester.getRealizedPnL();
    result.unrealized_pnl = backtester.getUnrealizedPnL();
    result.turnover = backtester.getTurnover();
    result.total_orders = backtester.getTotalOrdersPlaced();
    result.total_fills = backtester.getTotalFills();
    result.fill_rate = (result.total_orders > 0) ? 
                        (100.0 * result.total_fills / result.total_orders) : 0.0;
    result.final_inventory = backtester.getInventory();
    result.avg_entry_price = backtester.getAvgEntryPrice();
    result.execution_time_ms = execution_time;
    auto total_events = hm->getTotalEvents();
    result.perfomance = total_events? result.execution_time_ms * 1.0 / hm->getTotalEvents() : 0;
    result.perfomance = result.perfomance * 1000;//convert to mks from ms

    // Прогресс
    int completed = ++completed_count;
    int total = total_count.load();
    
    static std::atomic_flag cout_flag = ATOMIC_FLAG_INIT;
    while (cout_flag.test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    
    std::cout << "[" << completed << "/" << total << "] " 
              << name << ": "
              << "PnL=$" << result.total_profit
              << " (realized=" << result.realized_pnl 
              << ", unrealized=" << result.unrealized_pnl << ")"
              << ", Fill=" << result.fill_rate << "%"
              << ", MaxInv=" << max_inventory
              << ", Time=" << execution_time << "ms" << std::endl;
    
    cout_flag.clear(std::memory_order_release);
    
    return result;
}

// Сохранение результатов в CSV
void saveResultsToCSV(const std::vector<BacktestResult>& results, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return;
    }
    
    file << "name,gamma,kappa,sigma,order_size,max_inventory,final_equity,total_profit,"
         << "realized_pnl,unrealized_pnl,turnover,total_orders,total_fills,fill_rate,"
         << "final_inventory,avg_entry_price,execution_time_ms\n";
    
    for (const auto& r : results) {
        file << r.name << ","
             << r.gamma << ","
             << r.kappa << ","
             << r.sigma << ","
             << r.order_size << ","
             << r.max_inventory << ","
             << r.final_equity << ","
             << r.total_profit << ","
             << r.realized_pnl << ","
             << r.unrealized_pnl << ","
             << r.turnover << ","
             << r.total_orders << ","
             << r.total_fills << ","
             << r.fill_rate << ","
             << r.final_inventory << ","
             << r.avg_entry_price << ","
             << r.execution_time_ms << "," 
             << r.perfomance << "\n";
    }
    
    file.close();
    std::cout << "Results saved to: " << filename << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "╔═════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     CMF HFT Backtester - Avellaneda-Stoikov MicroPrice 2018         ║\n";
    std::cout << "║           Parameter Sweep - Parallel Testing                        ║\n";
    std::cout << "╚═════════════════════════════════════════════════════════════════════╝\n";
    
    std::string l2_file = "../../../data/synced_lob.csv";
    std::string trade_file = "../../../data/synced_trades.csv";
    
    if (argc >= 2) l2_file = argv[1];
    if (argc >= 3) trade_file = argv[2];
    
    // Загрузка данных
    std::cout << "\nLoading L2 data..." << std::endl;
    auto hm = std::make_shared<HistoryManager>();
    hm->loadL2Data(l2_file);
    std::cout << "Loading Trade data..." << std::endl;
    hm->loadTradeData(trade_file);
    std::cout << "Loaded " << hm->getL2Size() << " L2 snapshots and " 
              << hm->getTradeSize() << " trades" << std::endl;
    
    std::vector<BacktestResult> results;
    
    // ============================================================
    // ПАРАМЕТРЫ ДЛЯ ТЕСТИРОВАНИЯ
    // ============================================================
    
    struct ParamSet {
        std::string name;
        double gamma;
        double kappa;
        double sigma;
        double order_size;
        double max_inventory;
    };
    
    std::vector<ParamSet> all_params;
    
    double base_gamma = 0.5;
    double base_kappa = 1000000.0;
    double base_sigma = 0.0002;
    double base_order_size = 50.0;
    double base_max_inventory = 5000.0;
    
    // 1. Тестирование разных gamma
    std::vector<double> gamma_values = {0.1, 0.5, 1.0, 2.0, 3.0, 5.0, 10.0};
    for (double gamma : gamma_values) {
        all_params.push_back({"gamma_" + std::to_string(gamma), gamma, base_kappa, base_sigma, base_order_size, base_max_inventory});
    }
    
    // 2. Тестирование разных kappa
    std::vector<double> kappa_values = {10000, 100000, 500000, 1000000, 5000000, 8000000, 10000000};
    for (double kappa : kappa_values) {
        all_params.push_back({"kappa_" + std::to_string(kappa), base_gamma, kappa, base_sigma, base_order_size, base_max_inventory});
    }
    
    // 3. Тестирование разных sigma
    std::vector<double> sigma_values = {0.0001, 0.0002, 0.0005, 0.001, 0.0015, 0.002, 0.005};
    for (double sigma : sigma_values) {
        all_params.push_back({"sigma_" + std::to_string(sigma), base_gamma, base_kappa, sigma, base_order_size, base_max_inventory});
    }
    
    // 4. Тестирование разных размеров ордеров
    std::vector<double> order_sizes = {10, 25, 50, 75, 100, 120, 150, 200, 500};
    for (double size : order_sizes) {
        all_params.push_back({"size_" + std::to_string(size), base_gamma, base_kappa, base_sigma, size, base_max_inventory});
    }
    
    // 5. Тестирование разных max_inventory
    std::vector<double> max_inventory_values = {1000, 2000, 3000, 4000, 5000, 7500, 10000};
    for (double max_inv : max_inventory_values) {
        all_params.push_back({"maxinv_" + std::to_string(max_inv), base_gamma, base_kappa, base_sigma, base_order_size, max_inv});
    }
    
    // 6. Grid search - оптимальные комбинации (расширенная сетка)
    std::vector<std::tuple<double, double, double, double, double>> grid_params = {
        // gamma, kappa, sigma, order_size, max_inventory
        {2.0, 5000000, 0.001, 100, 5000},
        {3.0, 8000000, 0.0015, 120, 5000},
        {2.5, 7000000, 0.0012, 110, 7500},
        {1.5, 3000000, 0.0008, 80, 4000},
        {4.0, 10000000, 0.002, 150, 10000},
        {0.8, 2000000, 0.0005, 60, 3000},
        {1.2, 4000000, 0.0007, 90, 6000},
        {3.5, 9000000, 0.0018, 130, 8000},
        // Более агрессивные настройки
        {5.0, 12000000, 0.0025, 200, 15000},
        {0.3, 1500000, 0.0003, 40, 2500},
        {2.0, 6000000, 0.001, 100, 2000},
        {4.0, 11000000, 0.001, 180, 12000}
    };
    
    int idx = 1;
    for (const auto& [g, k, s, sz, max_inv] : grid_params) {
        all_params.push_back({"grid_" + std::to_string(idx++), g, k, s, sz, max_inv});
    }
    
    // 7. Детальная сетка вокруг лучших параметров (grid_8: gamma=3, kappa=8e6, sigma=0.0015, size=120)
    std::vector<double> fine_gamma = {2.5, 2.8, 3.0, 3.2, 3.5};
    std::vector<double> fine_kappa = {7000000, 7500000, 8000000, 8500000, 9000000};
    std::vector<double> fine_sigma = {0.0012, 0.0014, 0.0015, 0.0016, 0.0018};
    std::vector<double> fine_sizes = {100, 110, 120, 130, 150};
    std::vector<double> fine_maxinv = {4000, 5000, 6000, 8000, 10000};
    
    int fine_idx = 1;
    for (double g : fine_gamma) {
        all_params.push_back({"fine_gamma_" + std::to_string(fine_idx++), g, 8000000, 0.0015, 120, 5000});
    }
    fine_idx = 1;
    for (double k : fine_kappa) {
        all_params.push_back({"fine_kappa_" + std::to_string(fine_idx++), 3.0, k, 0.0015, 120, 5000});
    }
    fine_idx = 1;
    for (double s : fine_sigma) {
        all_params.push_back({"fine_sigma_" + std::to_string(fine_idx++), 3.0, 8000000, s, 120, 5000});
    }
    fine_idx = 1;
    for (double sz : fine_sizes) {
        all_params.push_back({"fine_size_" + std::to_string(fine_idx++), 3.0, 8000000, 0.0015, sz, 5000});
    }
    fine_idx = 1;
    for (double max_inv : fine_maxinv) {
        all_params.push_back({"fine_maxinv_" + std::to_string(fine_idx++), 3.0, 8000000, 0.0015, 120, max_inv});
    }
    
    total_count = all_params.size();
    
    std::cout << "\nTotal parameter sets: " << total_count.load() << std::endl;
    std::cout << "Running in parallel (" << std::thread::hardware_concurrency() << " threads)...\n" << std::endl;
    
    // Параллельный запуск
    auto start_total = std::chrono::high_resolution_clock::now();
    
    std::vector<std::future<BacktestResult>> futures;
    for (const auto& p : all_params) {
        futures.push_back(std::async(std::launch::async, runBacktest, hm, p.name, 
                                      p.gamma, p.kappa, p.sigma, p.order_size, p.max_inventory));
    }
    
    for (auto& f : futures) {
        results.push_back(f.get());
    }
    
    auto end_total = std::chrono::high_resolution_clock::now();
    auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_total - start_total).count();
    
    // Сохранение результатов
    saveResultsToCSV(results, "backtest_results_summary.csv");
    
    // Вывод лучших результатов
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "BEST RESULTS SUMMARY" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    
    // Сортировка по прибыли
    auto sorted_by_profit = results;
    std::sort(sorted_by_profit.begin(), sorted_by_profit.end(),
        [](const BacktestResult& a, const BacktestResult& b) {
            return a.total_profit > b.total_profit;
        });
    
    std::cout << "\n🏆 TOP 10 BY TOTAL PROFIT:" << std::endl;
    for (int i = 0; i < std::min(10, (int)sorted_by_profit.size()); ++i) {
        const auto& r = sorted_by_profit[i];
        std::cout << "   " << (i+1) << ". " << r.name 
                  << " | Profit=$" << r.total_profit
                  << " | Realized=$" << r.realized_pnl
                  << " | Unrealized=$" << r.unrealized_pnl
                  << " | Fill=" << r.fill_rate << "%"
                  << " | Gamma=" << r.gamma
                  << " | Kappa=" << r.kappa
                  << " | Sigma=" << r.sigma
                  << " | Size=" << r.order_size
                  << " | MaxInv=" << r.max_inventory << std::endl;
    }
    
    // Лучший по прибыли
    auto best = sorted_by_profit[0];
    std::cout << "\n🏆 BEST OVERALL: " << best.name << std::endl;
    std::cout << "   gamma=" << best.gamma << ", kappa=" << best.kappa 
              << ", sigma=" << best.sigma << ", size=" << best.order_size 
              << ", max_inventory=" << best.max_inventory << std::endl;
    std::cout << "   Profit=$" << best.total_profit 
              << " (realized=" << best.realized_pnl 
              << ", unrealized=" << best.unrealized_pnl << ")"
              << ", Fill Rate=" << best.fill_rate << "%" << std::endl;
    
    // Лучший по fill rate
    auto best_fill = std::max_element(results.begin(), results.end(),
        [](const BacktestResult& a, const BacktestResult& b) {
            return a.fill_rate < b.fill_rate;
        });
    std::cout << "\n📈 BEST FILL RATE: " << best_fill->name << std::endl;
    std::cout << "   Fill Rate=" << best_fill->fill_rate << "%, Profit=$" << best_fill->total_profit << std::endl;
    
    // Лучший по realized PnL
    auto best_realized = std::max_element(results.begin(), results.end(),
        [](const BacktestResult& a, const BacktestResult& b) {
            return a.realized_pnl < b.realized_pnl;
        });
    std::cout << "\n💵 BEST REALIZED PNL: " << best_realized->name << std::endl;
    std::cout << "   Realized=$" << best_realized->realized_pnl << ", Profit=$" << best_realized->total_profit << std::endl;
    
    // Лучший по Sharpe
    std::cout << "\n⏱️  Total execution time: " << total_time << " ms (" << total_time/1000 << " sec)" << std::endl;
    std::cout << "   Average time per test: " << total_time / total_count.load() << " ms" << std::endl;
    std::cout << "\nDone. Results saved to backtest_results_summary.csv\n";
    
    return 0;
}