#pragma once

#include <cstddef>
#include <functional>

namespace datapump::modem::detail {

// Zero selects all but one available CPU (and at least one). Explicit requests
// are capped at the CPUs available to this process, including Linux affinity.
std::size_t search_concurrency(std::size_t requested = 0);

// Visit each index exactly once, with a private worker slot below concurrency.
// The calling thread participates. Batches share a persistent pool and concurrent
// submissions are serialized. Nested calls run serially using worker slot zero.
// Every job finishes before return, including on failure; the exception from the
// lowest failing index is rethrown after all workers have finished.
void parallel_search(std::size_t count, std::size_t concurrency,
                     const std::function<void(std::size_t worker, std::size_t index)>& work);

} // namespace datapump::modem::detail
