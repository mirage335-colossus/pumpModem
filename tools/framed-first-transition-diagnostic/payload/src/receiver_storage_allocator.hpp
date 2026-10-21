#pragma once

#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>

namespace datapump::live::detail {

// PRIVATE diagnostic overlay: retain the rejected quota request without changing
// the std::bad_alloc catch contract or the account/allocator object layout.
class ReceiverAllocationLimit final : public std::bad_alloc {
public:
    ReceiverAllocationLimit(std::size_t requested, std::size_t allocated,
                            std::size_t limit) noexcept
        : requested_bytes(requested), allocated_bytes(allocated),
          allocation_limit(limit) {}
    const std::size_t requested_bytes;
    const std::size_t allocated_bytes;
    const std::size_t allocation_limit;
};

// Tracks allocator-requested bytes retained by containers using this account.
// The account object and its shared_ptr control block are separate overhead.
class ReceiverAllocationAccount final {
public:
    explicit ReceiverAllocationAccount(
        std::size_t limit = std::numeric_limits<std::size_t>::max()) noexcept
        : limit_(limit) {}

    ReceiverAllocationAccount(const ReceiverAllocationAccount&) = delete;
    ReceiverAllocationAccount& operator=(const ReceiverAllocationAccount&) = delete;

    [[nodiscard]] std::size_t allocated_bytes() const noexcept {
        const std::lock_guard lock(mutex_);
        return allocated_bytes_;
    }

    [[nodiscard]] std::size_t allocation_limit() const noexcept {
        const std::lock_guard lock(mutex_);
        return limit_;
    }

    // A lower limit can temporarily be below current use. In that state all
    // nonzero allocations fail until deallocation brings use below the limit.
    void set_allocation_limit(std::size_t limit) noexcept {
        const std::lock_guard lock(mutex_);
        limit_ = limit;
    }

private:
    template<class> friend class ReceiverStorageAllocator;

    mutable std::mutex mutex_;
    std::size_t allocated_bytes_ = 0;
    std::size_t limit_;
};

// Allocator suitable for std::vector and its rebound element types. A shared
// account travels with allocator copies, so old allocations can still be
// charged back after the owner that created a container has been replaced.
template<class T>
class ReceiverStorageAllocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using is_always_equal = std::false_type;

    template<class U>
    struct rebind { using other = ReceiverStorageAllocator<U>; };

    ReceiverStorageAllocator()
        : account_(std::make_shared<ReceiverAllocationAccount>()) {}

    explicit ReceiverStorageAllocator(
        std::shared_ptr<ReceiverAllocationAccount> account) noexcept
        : account_(std::move(account)) {}

    template<class U>
    ReceiverStorageAllocator(const ReceiverStorageAllocator<U>& other) noexcept
        : account_(other.account_) {}

    [[nodiscard]] T* allocate(size_type count) {
        if (count > max_size()) throw std::bad_alloc();
        const auto bytes = count * sizeof(T);
        if (!account_) throw std::bad_alloc();

        // Hold the account lock across allocation. This makes the limit check
        // aggregate across allocator copies while recording bytes only after
        // the underlying allocation succeeds.
        const std::lock_guard lock(account_->mutex_);
        if (account_->allocated_bytes_ > account_->limit_ ||
            bytes > account_->limit_ - account_->allocated_bytes_)
            throw ReceiverAllocationLimit(bytes, account_->allocated_bytes_,
                                          account_->limit_);
        T* result = std::allocator<T>{}.allocate(count);
        account_->allocated_bytes_ += bytes;
        return result;
    }

    void deallocate(T* pointer, size_type count) noexcept {
        if (!account_ || count > max_size()) std::terminate();
        const auto bytes = count * sizeof(T);
        const std::lock_guard lock(account_->mutex_);
        if (bytes > account_->allocated_bytes_) std::terminate();
        std::allocator<T>{}.deallocate(pointer, count);
        account_->allocated_bytes_ -= bytes;
    }

    [[nodiscard]] size_type max_size() const noexcept {
        return std::numeric_limits<size_type>::max() / sizeof(T);
    }

    [[nodiscard]] const std::shared_ptr<ReceiverAllocationAccount>&
    account() const noexcept { return account_; }

    template<class U>
    friend bool operator==(const ReceiverStorageAllocator& left,
                           const ReceiverStorageAllocator<U>& right) noexcept {
        return left.account_.get() == right.account_.get();
    }

private:
    template<class> friend class ReceiverStorageAllocator;
    std::shared_ptr<ReceiverAllocationAccount> account_;
};

} // namespace datapump::live::detail
