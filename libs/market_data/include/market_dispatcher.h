#pragma once

#include "tick.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class MarketDispatcher {
public:
    void subscribe(const std::string& symbol, const std::function<void(Tick)>& callback);
    void dispatch(const Tick& tick);
    int  subscriberCount(const std::string& symbol) const;

private:
    std::unordered_map<std::string, std::vector<std::function<void(Tick)>>> listCallback;
};
