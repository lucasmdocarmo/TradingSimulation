#include "tick.h"
#include <arpa/inet.h>
#include <chrono>
#include <ctime>
#include <iostream>
#include <netinet/in.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

struct SimSymbol {
  std::string symbol;
  double basePrice;
  double currentPrice; // ADD: tracks random walk
  double vol;          // ADD: per-symbol volatility
};

// CHANGE: AMZN→TSLA, added ESZ4+GBPUSD, added currentPrice+vol columns
std::vector<SimSymbol> symbols = {
    {"AAPL", 150.00, 150.00, 0.15},   {"MSFT", 300.00, 300.00, 0.20},
    {"GOOGL", 140.00, 140.00, 0.12},  {"TSLA", 200.00, 200.00, 0.25},
    {"ESZ4", 4500.00, 4500.00, 1.50}, {"GBPUSD", 1.25, 1.25, 0.002},
};

std::mt19937 generator(std::random_device{}());
std::uniform_int_distribution<int> z1(0, symbols.size() - 1);

// CHANGE: takes vol so each symbol drifts at its own speed
double generateNoise(double vol) {
  static std::normal_distribution<double> z0(0.0, 1.0);
  return z0(generator) * vol;
}

int setupSocket() {
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  return sockfd;
}

void sendLoop() {
  int sockfd = setupSocket();
  sockaddr_in dest;
  dest.sin_family = AF_INET;
  dest.sin_port = htons(9000);
  dest.sin_addr.s_addr = inet_addr("127.0.0.1");

  for (int i = 0; i < 100000; i++) { // CHANGE: was 10000
    int index = z1(generator);
    SimSymbol &sym = symbols[index];

    sym.currentPrice +=
        generateNoise(sym.vol); // CHANGE: random walk not fixed noise
    if (sym.currentPrice < 0.01)
      sym.currentPrice = sym.basePrice; // floor

    double last = sym.currentPrice; // CHANGE: use drifted price
    Tick tick;
    tick.symbol = sym.symbol;
    tick.last = last;
    tick.bid = last - 0.01;
    tick.ask = last + 0.01;
    tick.timestamp = std::chrono::system_clock::now();
    sendto(sockfd, reinterpret_cast<const char *>(&tick), sizeof(Tick), 0,
           reinterpret_cast<const sockaddr *>(&dest), sizeof(dest));
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  close(sockfd);
  std::cout << "100,000 ticks sent\n";
}

int main() {
  sendLoop();
  return 0;
}
