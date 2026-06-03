#pragma once
#include <string>

struct Position {

  std::string symbol;
  double quantity;
  double averageEntryPrice;
  double realisedPnl;
  double unrealisedPnl;
};
