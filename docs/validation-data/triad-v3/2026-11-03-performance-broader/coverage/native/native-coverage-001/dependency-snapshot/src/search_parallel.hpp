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

// Visit contiguous logical ranges [begin, end), each with at most grain indices.
// Grain must be nonzero. Logical count is independent of the bounded CPU worker
// slots: this allocates no per-index or per-range state, even for SIZE_MAX count.
// The pool, caller participation and nested-call rules are shared with the
// per-index API above. Each range is invoked exactly once and every invocation
// finishes before return; a throwing callback may leave its own range unfinished.
// After joining, rethrow the exception from the lowest failing range begin.
void parallel_search_ranges(
    std::size_t count, std::size_t concurrency, std::size_t grain,
    const std::function<void(std::size_t worker, std::size_t begin, std::size_t end)>& work);

} // namespace datapump::modem::detail
