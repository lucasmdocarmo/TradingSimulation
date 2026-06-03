#pragma once
#include "instrument.h"
#include <optional>
#include <string>
class InstrumentStore {

public:
  void loadFromCSV(const std::string &filePath);
  const Instrument &lookup(const std::string &symbol) const;
  const std::unordered_map<std::string, Instrument> &all() const;
  std::size_t size() const;

private:
  std::unordered_map<std::string, Instrument> instruments_;
  static AssetClass parseAssetClass(const std::string &s);
  static std::optional<OptionType> parseOptionType(const std::string &s);
  static std::optional<OptionStyle> parseOptionStyle(const std::string &s);
  static std::optional<std::chrono::year_month_day>
  parseDate(const std::string &s);

  static std::string trim(const std::string &s);
};
