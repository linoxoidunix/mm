# HFT Backtester

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://isocpp.org/)
[![License](https://img.shields.io/badge/License-GPL-green.svg)](LICENSE)

A high-performance backtesting framework for High-Frequency Trading strategies, implementing the Avellaneda-Stoikov (2008) market-making model with microprice extensions (2018).

# Core Capabilities

## Matching Engine
- ✅ Support limit orders
- ✅ Support cancel order by ID
- ✅ Support market orders
- ✅ Support cancel all orders
- ✅ Support ordered order L3 (Level 3 order book)

## History Manager

Supports downloading L2 order book data and trade data in the following formats:

---

### 📘 Limit Order Book (L2) Data

Market depth snapshots with **up to N price levels** on each side:

| Column        | Description                                      |
|---------------|--------------------------------------------------|
| `row`         | Sequential record identifier                     |
| `timestamp`   | Unix timestamp (nanoseconds)                     |
| `ask{i}.price`| Ask price at level `i` (`0` = best ask)          |
| `ask{i}.qty`  | Ask quantity available at level `i`              |
| `bid{i}.price`| Bid price at level `i` (`0` = best bid)          |
| `bid{i}.qty`  | Bid quantity available at level `i`              |

---

### 📗 Trade Data (Executed Transactions)

Historical trade feed with **aggressor side identification**:

| Column      | Description                                              |
|-------------|----------------------------------------------------------|
| `row`       | Sequential record identifier                             |
| `timestamp` | Unix timestamp (nanoseconds)                             |
| `side`      | Trade initiator: `buy` (aggressive buyer) or `sell`      |
| `price`     | Execution price of the trade                             |
| `amount`    | Volume traded (in base asset units)                      |

### Strategy Framework

The backtester is built around the `IStrategy` interface, which defines the interaction between trading logic and the simulation engine.  
All strategies are executed through this abstraction during replay of historical data.

Reference implementations include:

- **Avellaneda–Stoikov (2008)** — Inventory-aware optimal market making model  
- **Avellaneda–Stoikov + Microprice (2018)** — Enhanced fair price estimation using order book imbalance  

---

### Extensibility

To implement a custom strategy:

1. Inherit from `IStrategy`
2. Implement the required event handlers (e.g. market updates, fills)
3. Define quoting and inventory management logic

### Output & Analytics

#### Grid Search Reports
The backtester generates comprehensive reports with the following metrics:

| Parameter | Description |
|-----------|-------------|
| `gamma` | Risk aversion coefficient |
| `kappa` | Resilience (order flow intensity) |
| `sigma` | Volatility |
| `order_size` | Base order size in units |
| `max_inventory` | Maximum inventory reached |
| `final_equity` | Final portfolio equity |
| `total_profit` | Total profit/loss |
| `realized_pnl` | Realized profit/loss from closed positions |
| `unrealized_pnl` | Unrealized profit/loss from open positions |
| `turnover` | Total traded volume |
| `total_orders` | Number of orders placed |
| `total_fills` | Number of fills executed |
| `fill_rate` | Percentage of orders filled |
| `final_inventory` | Final inventory position |
| `avg_entry_price` | Average entry price across all fills |
| `execution_time_ms` | Total simulation execution time in milliseconds |

## 🔧 Requirements

| Component | Version/Requirement |
|-----------|---------------------|
| **Compiler** | GCC 15+ |
| **Build System** | CMake 3.31.11+ |
| **Operating System** | Linux |
| **Dependencies** | None (uses only C++23 standard library) |
| **Optional** | Google Test for unit tests |

## ⚙️ Building

### Quick Start (Linux)

```bash
# Clone the repository
git clone https://github.com/linoxoidunix/mm.git
cd mm

# Build with CMake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=native -DNDEBUG"
make -j16
```

## 📥 Downloading Data

Before running any strategy, download the L2 + trades historical data:

```bash
scripts/download_data.sh
```


## 🚀 Example: Strategy Execution with Parameter Grid Search

### 1. Avellaneda–Stoikov with Microprice Extension
Run the strategy on historical data with parameterized variables:

```bash
cd build/examples/example4
./example4 ../../../data/synced_lob.csv ../../../data/synced_trades.csv
```
Results will be written to backtest_results_summary.csv

### 2. Avellaneda–Stoikov
Run the strategy on historical data with parameterized variables:

```bash
cd build/examples/example6
./example6 ../../../data/synced_lob.csv ../../../data/synced_trades.csv
```
Results will be written to backtest_results_summary.csv

## 📈 Future Work / Roadmap

This section outlines planned improvements to the backtesting engine and research pipeline, focusing on increased market realism, feature engineering, and integration with machine learning-based decision systems.

---

### 1. Execution Realism Enhancements

- Introduce **order transmission latency** (message delay between signal generation and order submission)
- Introduce **execution latency** (delay between order arrival and potential matching/execution)
- Model **queue dynamics more explicitly** for limit order book priority effects
- Add **partial fill stochasticity** based on order book liquidity and trade intensity

---

### 2. Transaction Cost Model

- Introduce configurable **commission / fee structure**
  - Maker / taker fees
  - Tiered fee schedules (volume-based discounts)
- Add optional **slippage model**
  - Market impact for large orders
  - Spread-dependent execution cost adjustments

---

### 3. Feature Engineering & Dataset Pipeline

- Complete implementation of **feature extraction layer**
  - Order book imbalance
  - Microprice deviation
  - Spread dynamics
  - Trade flow imbalance (buy/sell pressure)
- Define and standardize **ML targets**
  - Mid-price movement prediction (classification / regression)
  - Short-term return prediction
  - Volatility forecasting
- Add **rolling-window dataset builder** for time-series ML training

---

### 4. End-to-End ML Pipeline

Build a full research workflow:

> Feature Engineering → Label Generation → Model Training → Signal Generation → Backtesting → Performance Evaluation

- Integrate ML models into strategy layer (`IStrategy` extension)
- Support **offline training + online inference simulation**
- Enable **strategy comparison between ML-driven and rule-based models**
- Add **hyperparameter optimization / grid search pipeline**

---
