#include "fix_session_reader.h"

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <stdexcept>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// mmap-based FIX session reader.
//
// Why mmap instead of std::fstream / read():
//   fstream calls read() in a loop, which involves:
//     1. A context switch into the kernel for each call
//     2. A copy from the kernel page cache into a userspace buffer
//   mmap maps the file's pages directly into our virtual address space.
//   No copies, no syscalls per-line — just pointer arithmetic on memory.
//   The kernel's readahead fills pages in the background; MADV_SEQUENTIAL
//   tells it to prefetch aggressively since we traverse the file linearly.
//
// For a 10,000-order FIX file (~400 KB), this is overkill. But for a real
// FIX session replaying millions of historical orders, mmap saturates
// memory bandwidth instead of being gated on syscall overhead.
void FixSessionReader::start(const std::string& filepath) {
    const int fd = ::open(filepath.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("FixSessionReader: cannot open " + filepath);

    struct stat sb;
    if (::fstat(fd, &sb) < 0) {
        ::close(fd);
        throw std::runtime_error("FixSessionReader: fstat failed");
    }

    const std::size_t file_size = static_cast<std::size_t>(sb.st_size);
    if (file_size == 0) { ::close(fd); return; }

    // MAP_PRIVATE: our reads don't affect the file; changes (none here) are private.
    // PROT_READ: read-only mapping prevents accidental writes corrupting the file.
    const char* data = static_cast<const char*>(
        ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0)
    );
    ::close(fd);  // fd can be closed immediately — the mapping keeps the file alive.

    if (data == MAP_FAILED)
        throw std::runtime_error("FixSessionReader: mmap failed");

    // Hint to the VM subsystem: pages will be accessed sequentially.
    // The kernel increases its readahead window, front-loading page faults
    // before we access each page, turning random I/O into streaming I/O.
    ::madvise(const_cast<char*>(data), file_size, MADV_SEQUENTIAL);

    running_ = true;
    const char* p   = data;
    const char* end = data + file_size;

    while (p < end && running_) {
        // memchr is a heavily optimised SIMD scan — faster than a char-by-char loop.
        const char* line_end = static_cast<const char*>(
            ::memchr(p, '\n', static_cast<std::size_t>(end - p))
        );
        if (!line_end) line_end = end;

        const std::size_t len = static_cast<std::size_t>(line_end - p);
        if (len > 0) {
            Order order = parseFixLine(p, len);
            if (onOrder_) onOrder_(order);
        }

        p = line_end + 1;
    }

    ::munmap(const_cast<char*>(data), file_size);
}

void FixSessionReader::stop() { running_ = false; }

void FixSessionReader::setOrderCallback(const std::function<void(Order)>& callback) {
    onOrder_ = callback;
}

// Zero-copy FIX tag parser. Operates entirely on the mmap'd buffer — no string
// allocations, no stream objects, no copies beyond the final Order struct.
//
// A FIX message line looks like:
//   11=ORD00001|35=D|55=AAPL|54=1|44=150.00|38=50
//
// We scan forward using memchr to locate '=' and '|' boundaries,
// then dispatch on the two-character tag string.
Order FixSessionReader::parseFixLine(const char* line, std::size_t len) {
    Order order{};
    order.arrival_ns = std::chrono::steady_clock::now().time_since_epoch().count();

    const char* p   = line;
    const char* end = line + len;

    while (p < end) {
        const char* eq = static_cast<const char*>(
            ::memchr(p, '=', static_cast<std::size_t>(end - p))
        );
        if (!eq) break;

        const char* sep = static_cast<const char*>(
            ::memchr(eq + 1, '|', static_cast<std::size_t>(end - (eq + 1)))
        );
        if (!sep) sep = end;

        const std::size_t tag_len = static_cast<std::size_t>(eq - p);
        const char*       val     = eq + 1;
        const std::size_t val_len = static_cast<std::size_t>(sep - val);

        if (tag_len == 2) {
            const char t0 = p[0], t1 = p[1];

            if (t0 == '1' && t1 == '1') {
                // Tag 11: ClOrdID
                const std::size_t n = std::min(val_len, sizeof(order.id) - 1);
                std::memcpy(order.id, val, n);
                order.id[n] = '\0';

            } else if (t0 == '5' && t1 == '5') {
                // Tag 55: Symbol
                const std::size_t n = std::min(val_len, sizeof(order.symbol) - 1);
                std::memcpy(order.symbol, val, n);
                order.symbol[n] = '\0';

            } else if (t0 == '5' && t1 == '4') {
                // Tag 54: Side — '1' = Buy, '2' = Sell
                order.order_type = (val_len > 0 && val[0] == '1')
                    ? OrderType::BUY : OrderType::SELL;

            } else if (t0 == '4' && t1 == '4') {
                // Tag 44: Price — atof on the mmap'd bytes (no copy needed for atof)
                // Safe because p points into a null-terminated-like region: the newline
                // acts as a sentinel and atof stops at the first non-numeric character.
                char buf[32];
                const std::size_t n = std::min(val_len, sizeof(buf) - 1);
                std::memcpy(buf, val, n);
                buf[n] = '\0';
                order.price = std::atof(buf);

            } else if (t0 == '3' && t1 == '8') {
                // Tag 38: OrderQty — fast integer parse, no atoi/stoi heap cost
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
