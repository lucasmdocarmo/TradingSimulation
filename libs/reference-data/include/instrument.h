#pragma once

#include <chrono>
#include <optional>
#include <string>

// Asset class taxonomy — the four instrument types traded in this system.
// Each class has different pricing mechanics, risk characteristics, and
// regulatory treatment.
enum class AssetClass {
    Equity,  // Stocks (AAPL, MSFT, GOOGL, TSLA) — ownership of a company
    Future,  // Futures contracts (ESZ4) — obligation to buy/sell at expiry
    Option,  // Options (AAPL240119C150) — right but not obligation to buy/sell
    FX       // Foreign exchange spot (GBPUSD) — currency pairs
};

// For options: whether the holder has the right to BUY (Call) or SELL (Put)
// the underlying at the strike price.
enum class OptionType { Call, Put, None };

// European: can only be exercised at expiry.
// American: can be exercised at any time up to and including expiry.
// American options are harder to price (require binomial trees or Monte Carlo)
// because the early-exercise premium must be modelled.
enum class OptionStyle { European, American, None };

// Static reference data for a single tradeable instrument.
//
// std::optional<T> is used for fields that only apply to some instrument types:
//   - strike / optionType / optionStyle are only valid for options
//   - underlying is only relevant for derivatives (options, futures)
//   - expiry is only relevant for instruments with a maturity date
//
// std::optional<T> explicitly communicates "this field may not exist" and
// forces callers to check with .has_value() before accessing, preventing
// silent use of uninitialised data — a common source of bugs with sentinel
// values like -1 or empty string.
struct Instrument {
    std::string  symbol;
    std::string  description;
    AssetClass   assetClass;
    std::string  currency;
    double       tickSize;      // minimum price movement (e.g. 0.01 for equities)
    double       contractSize;  // units per contract (e.g. 100 for an equity option lot)

    std::optional<double>      strike;      // option: strike price
    std::optional<OptionType>  optionType;  // option: Call or Put
    std::optional<OptionStyle> optionStyle; // option: European or American
    std::optional<std::string> underlying;  // derivative: underlying symbol (e.g. "AAPL")
    std::optional<std::chrono::year_month_day> expiry; // derivative: expiry date (C++20 calendar)
};
