#pragma once
#include <chrono>
#include <optional>
#include <string>

enum class AssetClass { Equity, Future, Option, FX };

enum class OptionType { Call, Put, None };

enum class OptionStyle { European, American, None };

struct Instrument {
  std::string symbol;
  std::string description;
  AssetClass assetClass;
  std::string currency;
  double tickSize;
  double contractSize;
  std::optional<double> strike;
  std::optional<OptionType> optionType;
  std::optional<OptionStyle> optionStyle;
  std::optional<std::string> underlying;
  std::optional<std::chrono::year_month_day> expiry;
};
