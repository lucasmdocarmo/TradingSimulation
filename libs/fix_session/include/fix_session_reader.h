#pragma once
#include "order.h"
#include <atomic>
#include <functional>

/**
 * @class FixSessionReader
 * @brief Parses simulated FIX protocol messages from a file.
 *
 * Reads an order file line-by-line where each line is a pipe-delimited
 * FIX message. It extracts the relevant tags to construct an Order and
 * triggers a callback for further processing.
 */
class FixSessionReader {
public:
  /**
   * @brief Starts the file reading and parsing loop.
   * @param filepath Path to the simulated FIX orders file.
   */
  void start(const std::string &filepath);

  /**
   * @brief Signals the reader loop to stop.
   */
  void stop();

  /**
   * @brief Registers the callback to be invoked for every successfully parsed order.
   */
  void setOrderCallback(const std::function<void(Order)> &callback);

private:
  /**
   * @brief Parses a single pipe-delimited FIX message line into an Order object.
   */
  Order parseFixMessage(const std::string &line);
  std::atomic_bool running_;
  std::function<void(Order)> onOrder_;
};
