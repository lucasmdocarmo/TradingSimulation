#include "fix_session_reader.h"
#include "order.h"
#include <fstream>
#include <sstream>
using namespace std;
Order FixSessionReader::parseFixMessage(const std::string &line) {
  stringstream ss(line);
  string item;
  Order order{};

  // Split the line by the pipe character ('|') standard in FIX messages
  while (getline(ss, item, '|')) {
    int position = item.find('=');
    string tag = item.substr(0, position);
    string value = item.substr(position + 1);

    // Map FIX tags to standard Order struct fields
    if (tag == "11") {
      // Tag 11: ClOrdID (Client Order ID)
      order.id = value;
    }
    if (tag == "55") {
      // Tag 55: Symbol
      order.symbol = value;
    }
    if (tag == "54") {
      // Tag 54: Side (1 = Buy, 2 = Sell)
      order.order_type = value == "1" ? OrderType::BUY : OrderType::SELL;
    }
    if (tag == "44") {
      // Tag 44: Price
      order.price = stod(value);
    }
    if (tag == "38") {
      // Tag 38: OrderQty
      order.quantity = stoi(value);
    };
  }
  return order;
};

void FixSessionReader::start(const std::string &filepath) {
  running_ = true;
  ifstream file(filepath);
  if (!file.is_open()) {
    throw std::runtime_error("Could not open file");
  }
  if (file.is_open()) {
    string line;
    while (getline(file, line) && running_) {
      stringstream ss(line);
      if (line.empty()) {
        continue;
      }
      Order order = parseFixMessage(line);
      if (onOrder_) {
        onOrder_(order);
      }
    }
  }
}

void FixSessionReader::stop() { running_ = false; };
void FixSessionReader::setOrderCallback(
    const std::function<void(Order)> &callback) {
  onOrder_ = callback;
}
