#pragma once

#include "order.h"

#include <atomic>
#include <stdexcept>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// FIX Protocol Session Reader
//
// WHAT IS FIX?
// FIX (Financial Information eXchange) is the universal messaging standard for
// electronic trading. A FIX "New Order Single" (35=D) is a pipe-delimited
// string of tag=value pairs:
//
//   11=ORD00001|35=D|55=AAPL|54=1|44=150.00|38=50
//
// This reader maps the file into virtual address space (mmap), then parses
// each line in-place — zero heap allocation per line.
//
// TEMPLATE CALLBACK vs std::function
// The previous design stored a std::function<void(Order)> member and called it
// via a virtual dispatch pointer on every line:
//
//   if (onOrder_) onOrder_(order);   ← indirect call, ~5–15 ns overhead
//
// The new design passes the callback as a template parameter to start().
// The compiler sees the full call chain at instantiation time and inlines the
// callback directly into the parse loop — eliminating the indirect call.
// This is the same technique used by STL algorithms (std::sort takes a
// comparator template parameter rather than a function pointer for the same
// reason: the hot comparison is inlined, not dispatched).
//
// Zero-copy parser (parseFixLine) is public so benchmarks can call it directly.
class FixSessionReader {
public:
    // Opens filepath with mmap, scans each '\n'-delimited line, calls
    // callback(order) for every successfully parsed FIX NOS message.
    //
    // Callback is a forwarding reference: accepts lambdas, function objects,
    // free functions — anything callable with signature void(Order).
    // The compiler instantiates a separate version of start() for each distinct
    // Callback type, inlining the call at the use site.
    template <typename Callback>
    void start(const std::string& filepath, Callback&& callback);

    // Sets running_ = false; the parse loop exits on its next iteration.
    void stop();

    // Zero-copy in-place parser — public so benchmarks bypass the mmap session.
    Order parseFixLine(const char* line, std::size_t len);

private:
    std::atomic_bool running_{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Template definition must live in the header.
//
// WHY TEMPLATES MUST BE IN HEADERS
// When the compiler sees `fixReader.start(path, myLambda)`, it instantiates
// start<decltype(myLambda)>. To do so it needs the full definition — not just
// the declaration. If start() were defined in a .cpp, the compiler would only
// see the declaration here, fail to instantiate, and the linker would find no
// matching symbol. (This is the "separate compilation model" for templates.)
// ─────────────────────────────────────────────────────────────────────────────
template <typename Callback>
void FixSessionReader::start(const std::string& filepath, Callback&& callback) {
    const int fd = ::open(filepath.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("FixSessionReader: cannot open " + filepath);

    struct stat sb;
    if (::fstat(fd, &sb) < 0) {
        ::close(fd);
        throw std::runtime_error("FixSessionReader: fstat failed");
    }

    const std::size_t file_size = static_cast<std::size_t>(sb.st_size);
    if (file_size == 0) { ::close(fd); return; }

    const char* data = static_cast<const char*>(
        ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
    ::close(fd);

    if (data == MAP_FAILED)
        throw std::runtime_error("FixSessionReader: mmap failed");

    ::madvise(const_cast<char*>(data), file_size, MADV_SEQUENTIAL);

    running_ = true;
    const char* p   = data;
    const char* end = data + file_size;

    while (p < end && running_) {
        const char* line_end = static_cast<const char*>(
            ::memchr(p, '\n', static_cast<std::size_t>(end - p)));
        if (!line_end) line_end = end;

        const std::size_t len = static_cast<std::size_t>(line_end - p);
        if (len > 0) {
            Order order = parseFixLine(p, len);
            // Direct inlined call — no std::function pointer dereference.
            // std::forward preserves value category: lvalue refs stay lvalue,
            // rvalue refs (temporaries/lambdas) stay rvalue.
            std::forward<Callback>(callback)(order);
        }
        p = line_end + 1;
    }

    ::munmap(const_cast<char*>(data), file_size);
}
