#include "udp_feed_subscriber.h"

#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

void UDPFeedSubscriber::start(int port, MarketDispatcher& dispatcher) {
    int serverSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket < 0) { perror("socket failed"); exit(EXIT_FAILURE); }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(serverSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    running_ = true;

    // alignas(Tick) guarantees the buffer has the same alignment as a Tick object.
    // sizeof(Tick) is the exact wire size — no over-read possible.
    //
    // The reinterpret_cast on the next line is now well-defined because Tick is a
    // trivially-copyable POD type (all members are scalars or char arrays).
    // C++ [basic.types] permits reading a trivially-copyable object from raw bytes
    // provided the memory is suitably aligned — which alignas(Tick) ensures.
    alignas(Tick) char buffer[sizeof(Tick)];

    sockaddr_in clientAddr{};
    socklen_t   clientAddrLen = sizeof(clientAddr);

    while (running_) {
        const int bytes = static_cast<int>(
            ::recvfrom(serverSocket, buffer, sizeof(buffer), 0,
                       reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen)
        );
        if (bytes < 0) { perror("recvfrom failed"); break; }
        if (static_cast<std::size_t>(bytes) < sizeof(Tick)) continue; // malformed

        const Tick tick = *reinterpret_cast<const Tick*>(buffer);
        dispatcher.dispatch(tick);
    }

    ::close(serverSocket);
}

void UDPFeedSubscriber::stop() { running_ = false; }
