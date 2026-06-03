#pragma once

#include "order.h"
#include <atomic>
#include <functional>
#include <string>

class FixSessionReader {
public:
    void start(const std::string& filepath);
    void stop();
    void setOrderCallback(const std::function<void(Order)>& callback);

private:
    // Zero-copy line parser: operates directly on the mmap'd buffer.
    // Avoids std::stringstream, std::string heap allocation, and getline syscalls.
    Order parseFixLine(const char* line, std::size_t len);

    std::atomic_bool             running_{false};
    std::function<void(Order)>   onOrder_;
};
