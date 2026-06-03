#include "tick.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <random>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

struct SimSymbol {
    const char* symbol;
    double      basePrice;
    double      currentPrice;
    double      vol;
};

std::vector<SimSymbol> symbols = {
    {"AAPL",   150.00, 150.00, 0.15},
    {"MSFT",   300.00, 300.00, 0.20},
    {"GOOGL",  140.00, 140.00, 0.12},
    {"TSLA",   200.00, 200.00, 0.25},
    {"ESZ4",  4500.00, 4500.00, 1.50},
    {"GBPUSD",   1.25,   1.25, 0.002},
};

std::mt19937                          generator(std::random_device{}());
std::uniform_int_distribution<int>    z1(0, static_cast<int>(symbols.size()) - 1);
std::normal_distribution<double>      z0(0.0, 1.0);

int setupSocket() {
    return ::socket(AF_INET, SOCK_DGRAM, 0);
}

void sendLoop() {
    const int sockfd = setupSocket();

    sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(9000);
    dest.sin_addr.s_addr = ::inet_addr("127.0.0.1");

    for (int i = 0; i < 1'000'000; ++i) {
        SimSymbol& sym = symbols[static_cast<std::size_t>(z1(generator))];

        sym.currentPrice += z0(generator) * sym.vol;
        if (sym.currentPrice < 0.01) sym.currentPrice = sym.basePrice;

        Tick tick{};
        // strncpy into the fixed char[8] field — safe wire format, no std::string.
        strncpy(tick.symbol, sym.symbol, sizeof(tick.symbol) - 1);
        tick.last        = sym.currentPrice;
        tick.bid         = sym.currentPrice - 0.01;
        tick.ask         = sym.currentPrice + 0.01;
        tick.timestamp_ns = std::chrono::steady_clock::now().time_since_epoch().count();

        // sizeof(Tick) bytes sent as raw binary. The subscriber's reinterpret_cast
        // is valid because Tick is a POD type with matching layout on both ends
        // (same process, same ABI). For cross-machine use, add explicit byte-order
        // conversion (hton* / ntoh*) and a protocol version header.
        ::sendto(sockfd, reinterpret_cast<const char*>(&tick), sizeof(Tick), 0,
                 reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));

        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    ::close(sockfd);
    std::cout << "1,000,000 ticks sent\n";
}

int main() {
    sendLoop();
    return 0;
}
