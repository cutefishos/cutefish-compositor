// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_CORE_MALLOC_POLICY_H_
#define CUTEFISH_CORE_MALLOC_POLICY_H_

#include <malloc.h>

namespace cutefish::core {

// Before any thread starts. Keeps glibc from raising its mmap threshold to the
// largest block freed and from giving each thread an arena that keeps its frees.
inline void applyMallocPolicy()
{
    mallopt(M_MMAP_THRESHOLD, 128 * 1024);
    mallopt(M_ARENA_MAX, 4);
}

} // namespace cutefish::core

#endif // CUTEFISH_CORE_MALLOC_POLICY_H_
