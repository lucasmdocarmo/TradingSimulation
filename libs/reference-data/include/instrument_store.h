#pragma once

#include "instrument.h"

#include <optional>
#include <string>
#include <unordered_map>

// In-memory reference data store.
//
// In a real trading system, reference data (instrument definitions, contract
// specs, exchange calendars) is loaded once at startup from a database or file
// and then only read — never written — during the trading session. This makes
// it safe to access from multiple threads without locking, since concurrent
// reads of a std::unordered_map are safe in C++ as long as no writes occur.
//
// This store is populated before any threads are started (see main.cpp), so
// the data is fully initialised and visible to all threads by the time the
// hot and cold paths begin.
//
// The unordered_map key is the instrument symbol (e.g. "AAPL"), giving
// average O(1) lookups — important if lookup is ever added to the hot path.
class InstrumentStore {
public:
    // Parses instruments.csv. Expected columns:
    //   symbol, description, asset_class, currency, tick_size, contract_size,
    //   strike (optional), option_type (optional), option_style (optional),
    //   underlying (optional), expiry (optional, YYYY-MM-DD)
    void loadFromCSV(const std::string& filePath);

    // Throws std::runtime_error if symbol is not found.
    // Callers must ensure the symbol exists (e.g. via the FIX order validator).
    const Instrument& lookup(const std::string& symbol) const;

    // Read-only access to all instruments — used by the Risk Engine to iterate
    // the full universe when computing portfolio-level metrics.
    const std::unordered_map<std::string, Instrument>& all() const;

    std::size_t size() const;

private:
    std::unordered_map<std::string, Instrument> instruments_;

    // Static helpers: isolated parsing logic that doesn't need instance state.
    // Declared static so they cannot accidentally access instruments_ — a
    // compile-time guarantee of their statelessness.
    static AssetClass                              parseAssetClass(const std::string& s);
    static std::optional<OptionType>               parseOptionType(const std::string& s);
    static std::optional<OptionStyle>              parseOptionStyle(const std::string& s);
    static std::optional<std::chrono::year_month_day> parseDate(const std::string& s);
    static std::string                             trim(const std::string& s);
};
