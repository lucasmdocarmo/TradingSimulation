#include "udp_feed_subscriber.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

void UDPFeedSubscriber::start(int port, MarketDispatcher &dispatcher) {
  int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
  if (serverSocket < 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  sockaddr_in serverAddress;
  memset(&serverAddress, 0, sizeof(serverAddress));

  serverAddress.sin_family = AF_INET;
  serverAddress.sin_port = htons(port);
  serverAddress.sin_addr.s_addr = INADDR_ANY;
  int bindResult = bind(serverSocket, (struct sockaddr *)&serverAddress,
                        sizeof(serverAddress));
  if (bindResult < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  };

  running_ = true;

  char buffer[1024];
  sockaddr_in clientAddress;
  socklen_t clientAddressLen = sizeof(clientAddress);
  
  // Continuous listening loop
  while (running_) {
    // Blocks until a datagram is received
    int bytesReceived =
        recvfrom(serverSocket, buffer, sizeof(buffer), 0,
                 (struct sockaddr *)&clientAddress, &clientAddressLen);
    if (bytesReceived < 0) {
      perror("recvfrom failed");
      break;
    }
    
    // Low-latency deserialisation: cast the raw binary buffer directly to a Tick struct
    Tick tick = *reinterpret_cast<Tick *>(buffer);
    
    // Forward the tick to the dispatcher
    dispatcher.dispatch(tick);
  }

  close(serverSocket);
}

void UDPFeedSubscriber::stop() { running_ = false; }
