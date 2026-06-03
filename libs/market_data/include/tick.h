#pragma once
#include <string>
struct Tick {
  std::string symbol;
  double bid;
  double ask;
  double last;
  std::chrono::system_clock::time_point timestamp;
};
