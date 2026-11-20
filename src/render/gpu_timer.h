// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

namespace cutefish::render {

class EglContext;

// Unlike a native fence, a timestamp query does not make the driver submit at
// once.
class GpuTimer
{
public:
    using Time = std::chrono::steady_clock::time_point;

    // Its name goes back to the pool when the mark is dropped, read or not.
    class Mark
    {
    public:
        Mark() = default;
        Mark(Mark &&other) noexcept;
        Mark &operator=(Mark &&other) noexcept;
        ~Mark();

        bool isValid() const { return m_query != 0; }

    private:
        friend class GpuTimer;
        Mark(std::shared_ptr<std::vector<unsigned int>> pool, unsigned int query);
        std::shared_ptr<std::vector<unsigned int>> m_pool;
        unsigned int m_query = 0;
    };

    explicit GpuTimer(EglContext &context);

    bool available() const { return m_queryCounter != nullptr; }

    // The context must be current.
    Mark mark();

    // On the steady clock; empty until it has, or when the GPU's clock jumped.
    // The context must be current.
    std::optional<Time> reached(const Mark &mark);

private:
    std::shared_ptr<std::vector<unsigned int>> m_pool =
            std::make_shared<std::vector<unsigned int>>();
    void *m_genQueries = nullptr;
    void *m_queryCounter = nullptr;
    void *m_getQueryObjectui = nullptr;
    void *m_getQueryObjectui64 = nullptr;
};

} // namespace cutefish::render
