#pragma once
#include <cstddef>
#include <limits>
namespace sdb::process_memory {
struct Stats {std::size_t limit,live,peak,allocations,rejected;};
// Link sdb::process_memory into the executable. Accounts all intercepted C++
// new/new[] calls, including alignment headers, not arbitrary malloc/GPU/RSS.
// A process launcher supplies the OS-level envelope for uninstrumented heaps.
void set_limit(std::size_t bytes);
Stats stats() noexcept;
} // namespace sdb::process_memory
