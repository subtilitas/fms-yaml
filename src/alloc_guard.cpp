// SPDX-License-Identifier: MIT
//
// Global operator new / delete replacements used to prove the "no dynamic
// memory after setup" property.  Linking this file is optional.
#include "fms/alloc_guard.hpp"

#include <cstdio>
#include <cstdlib>
#include <new>

namespace fms::alloc_guard {
namespace {

bool        g_armed  = false;
bool        g_fatal  = true;
std::size_t g_violations = 0;
std::size_t g_total       = 0;

}  // namespace

void arm(bool fatal) noexcept {
  g_fatal = fatal;
  g_armed = true;
}

void disarm() noexcept { g_armed = false; }

bool armed() noexcept { return g_armed; }

std::size_t violations() noexcept { return g_violations; }

std::size_t total_allocations() noexcept { return g_total; }

void reset_counters() noexcept {
  g_violations = 0;
  g_total      = 0;
}

namespace detail {

void* allocate(std::size_t size) noexcept {
  ++g_total;
  if (g_armed) {
    ++g_violations;
    if (g_fatal) {
      // Cannot throw and cannot allocate to report - write and die.
      std::fputs("[fms] heap allocation after setup phase - aborting\n", stderr);
      std::abort();
    }
  }
  if (size == 0) {
    size = 1;
  }
  return std::malloc(size);
}

void release(void* p) noexcept { std::free(p); }

}  // namespace detail
}  // namespace fms::alloc_guard

// ---------------------------------------------------------------------------
// Replacement operators.  The throwing forms keep their standard exception
// specification (a replacement may not narrow it), but since the project is
// built without exceptions they return nullptr on failure instead of throwing
// std::bad_alloc.
// ---------------------------------------------------------------------------
void* operator new(std::size_t size) {
  return fms::alloc_guard::detail::allocate(size);
}

void* operator new[](std::size_t size) {
  return fms::alloc_guard::detail::allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  return fms::alloc_guard::detail::allocate(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return fms::alloc_guard::detail::allocate(size);
}

void operator delete(void* p) noexcept { fms::alloc_guard::detail::release(p); }
void operator delete[](void* p) noexcept { fms::alloc_guard::detail::release(p); }
void operator delete(void* p, std::size_t) noexcept { fms::alloc_guard::detail::release(p); }
void operator delete[](void* p, std::size_t) noexcept { fms::alloc_guard::detail::release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { fms::alloc_guard::detail::release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { fms::alloc_guard::detail::release(p); }
