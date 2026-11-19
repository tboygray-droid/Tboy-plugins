#include "AllocationGuard.h"
#include <cstdlib>
#include <new>

namespace tgtest
{
    std::size_t& allocationCountThisThread() noexcept { static thread_local std::size_t c = 0; return c; }
    bool& allocationTrackingThisThread() noexcept     { static thread_local bool t = false; return t; }
}

static void* countedAlloc (std::size_t size)
{
    if (tgtest::allocationTrackingThisThread())
        ++tgtest::allocationCountThisThread();
    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* operator new (std::size_t size)                         { return countedAlloc (size); }
void* operator new[] (std::size_t size)                       { return countedAlloc (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept   { try { return countedAlloc (size); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { try { return countedAlloc (size); } catch (...) { return nullptr; } }
void operator delete (void* p) noexcept                       { std::free (p); }
void operator delete[] (void* p) noexcept                     { std::free (p); }
void operator delete (void* p, std::size_t) noexcept          { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept        { std::free (p); }
