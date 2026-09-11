// SPDX-License-Identifier: MIT
//
// ThreadPool v2 — Thread-local storage
// Per-worker state cached on TLS to avoid atomic loads on hot path.
//
#pragma once

#include "config.hpp"

#include <cstddef>

namespace vistella::tp {

inline thread_local std::size_t* tls_worker_index_ = nullptr;

inline thread_local class ThreadPool* tls_pool_ = nullptr;

}  // namespace vistella::tp
