#include "fix_session_reader.h"

#include <cstring>
#include <cstdlib>
#include <chrono>

// start() and setOrderCallback() have been removed from this file.
// start() is now a template defined in fix_session_reader.h so the compiler
// can instantiate it with the caller's specific lambda type and inline the
// callback into the parse loop. See the header for the full explanation.

void FixSessionReader::stop() { running_ = false; }

// Zero-copy FIX tag parser. Operates directly on the mmap'd buffer.
// No std::string allocations, no stream objects.
//
// A FIX message line looks like:
//   11=ORD00001|35=D|55=AAPL|54=1|44=150.00|38=50
//
// We scan forward with memchr to locate '=' and '|' boundaries,
// then dispatch on the two-character tag string.
Order FixSessionReader::parseFixLine(const char* line, std::size_t len) {
    Order order{};
    // Stamp arrival time as early as possible — before any tag parsing.
    // This is the T0 for E2E matching latency (T0 → Execution::arrival_ns).
    order.arrival_ns = std::chrono::steady_clock::now().time_since_epoch().count();

    const char* p   = line;
    const char* end = line + len;

    while (p < end) {
        const char* eq = static_cast<const char*>(
            ::memchr(p, '=', static_cast<std::size_t>(end - p)));
        if (!eq) break;

        const char* sep = static_cast<const char*>(
            ::memchr(eq + 1, '|', static_cast<std::size_t>(end - (eq + 1))));
        if (!sep) sep = end;

        const std::size_t tag_len = static_cast<std::size_t>(eq - p);
        const char*       val     = eq + 1;
        const std::size_t val_len = static_cast<std::size_t>(sep - val);

        if (tag_len == 2) {
            const char t0 = p[0], t1 = p[1];

            if (t0 == '1' && t1 == '1') {
                const std::size_t n = std::min(val_len, sizeof(order.id) - 1);
                std::memcpy(order.id, val, n);
                order.id[n] = '\0';

            } else if (t0 == '5' && t1 == '5') {
                const std::size_t n = std::min(val_len, sizeof(order.symbol) - 1);
                std::memcpy(order.symbol, val, n);
                order.symbol[n] = '\0';

            } else if (t0 == '5' && t1 == '4') {
                order.order_type = (val_len > 0 && val[0] == '1')
                    ? OrderType::BUY : OrderType::SELL;

            } else if (t0 == '4' && t1 == '4') {
                char buf[32];
                const std::size_t n = std::min(val_len, sizeof(buf) - 1);
                std::memcpy(buf, val, n);
                buf[n] = '\0';
                order.price = std::atof(buf);

            } else if (t0 == '3' && t1 == '8') {
                int32_t qty = 0;
                for (std::size_t i = 0; i < val_len; ++i)
                    qty = qty * 10 + (val[i] - '0');
                order.quantity = qty;
            }
        }
        p = (sep < end) ? sep + 1 : end;
    }
    return order;
}
