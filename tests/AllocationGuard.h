#pragma once
#include <atomic>
#include <cstddef>

/** While a ScopedAllocationCheck is alive on this thread, every heap allocation is counted.
    Used to verify that processBlock() never allocates. */
namespace tgtest
{
    std::size_t& allocationCountThisThread() noexcept;
    bool& allocationTrackingThisThread() noexcept;

    struct ScopedAllocationCheck
    {
        ScopedAllocationCheck()  { allocationCountThisThread() = 0; allocationTrackingThisThread() = true; }
        ~ScopedAllocationCheck() { allocationTrackingThisThread() = false; }
        std::size_t count() const noexcept { return allocationCountThisThread(); }
    };
}
