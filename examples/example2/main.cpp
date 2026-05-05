// hft_backtester_complete.cpp
// Complete HFT Backtester for CMF Entrance Exam
// Implements Avellaneda-Stoikov (2008) with microprice extension (2018)

#include <iostream>
#include "mm/hftbacktester/hftbacktester.h"
#include "mm/strategy/avelaneda_stoikov/avalaneda_stoikov_1.h"

#include "mm/strategy/avelaneda_stoikov/config.h"
// ==============================
// Main Function
// ==============================

int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     CMF HFT Backtester - Avellaneda-Stoikov 2008/2018    ║\n";
    std::cout << "║           Advanced Quantitative Analytics Program        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    
    // Default file names
    std::string l2_file = "../../../data/synced_lob.csv";
    std::string trade_file = "../../../data/synced_trades.csv";
    
    double gamma = 0.5;
    double kappa = 8000000;
    double sigma = 0.0002;
    // double gamma = 0.1;      // Низкая риск-аверсность (позволяет большие позиции)
    // double kappa = 1000.0;   // Умеренный спред
    // double sigma = 0.001;
    // Override from command line if provided
    if (argc >= 2) l2_file = argv[1];
    if (argc >= 3) trade_file = argv[2];
    try {
        if (argc >= 4) gamma = std::stod(argv[3]);
        if (argc >= 5) kappa = std::stod(argv[4]);
        if (argc >= 6) sigma = std::stod(argv[5]);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing numeric arguments: " << e.what() << std::endl;
        return 1;
    }

    HFTBacktesterConfig backtester_config;
    // Create backtester instance
    HFTBacktester backtester(std::move(backtester_config));
    ASParams config = {
        gamma,          // gamma: не трогаем
        kappa,      // kappa: 
                    // Это сузит Model Spread до разумных пределов.
        sigma,       // sigma: 
                    // Это должно наконец дать "Inv Risk" > 0 в логах.
        1.0,          
        5000.0        
    };
    auto strategy = std::make_unique<AvellanedaStoikov1>(config, 50.0);
    backtester.setStrategy(std::move(strategy));
    
    
    // Load data
    std::cout << "\nLoading data...\n";
    backtester.loadL2Data(l2_file);
    backtester.loadTradeData(trade_file);
    
    // Run simulation
    backtester.runStrategy();
    
    // Generate reports
    backtester.generateReport();
    // backtester.exportResults("backtest_results.csv");
    // backtester.exportFills("fills.csv");
    
    std::cout << "\nDone.\n";
    
    return 0;
}