// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_WALLPAPER_WALLPAPER_CACHE_H_
#define CUTEFISH_SHELL_WALLPAPER_WALLPAPER_CACHE_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/geometry.h"
#include "base/thread_pool.h"
#include "base/timer.h"
#include "ui/bitmap.h"

namespace cutefish::shell {

// One decode per path and device size is shared by every user, the last few stay
// in memory, and results go to a disk cache for the next session.
class WallpaperCache
{
public:
    // Bump when decode() draws a different picture, so old cache files miss.
    static constexpr uint32_t kCacheVersion = 1;
    // Decodes kept in memory: the pictures on show, and until kExpiry after
    // the last request the one before, so switching back meanwhile is instant.
    static constexpr int kMaxCachedImages = 2;
    // How long a decode no screen shows stays after the last request; then it
    // comes back from the disk cache.
    static constexpr std::chrono::seconds kExpiry {60};
    static constexpr int kMaxCachedFiles = 4;

    using Done = std::function<void(const ui::Bitmap &image)>;

    // Empty `directory` is ~/.cache/cutefish/wallpapers.
    explicit WallpaperCache(std::string directory = {});
    ~WallpaperCache();

    WallpaperCache(const WallpaperCache &) = delete;
    WallpaperCache &operator=(const WallpaperCache &) = delete;

    // Null when the file cannot be read. A decode in memory is handed over before this
    // returns; otherwise `done` runs on the loop while the returned handle is kept.
    [[nodiscard]] std::shared_ptr<void> load(const std::string &path, const base::Size &deviceSize, Done done);

    // Null for none.
    ui::Bitmap cached(const std::string &path, const base::Size &deviceSize) const;
    std::pair<int, size_t> memory() const;
    // Those decodes stay in memory however long ago they were asked for.
    void setShown(const std::vector<std::pair<std::string, base::Size>> &pictures);

    // On any thread.
    static ui::Bitmap decode(const std::string &path, const base::Size &deviceSize);
    static ui::Bitmap cachedOrDecode(const std::string &path, const base::Size &deviceSize,
                                     const std::string &directory);
    // An edit or a replacement misses; empty for a file that is not there.
    static std::string cacheKey(const std::string &path, const base::Size &deviceSize);
    static std::string defaultDirectory();

private:
    friend class WallpaperCacheTestAccess;

    struct Key {
        std::string path;
        int width = 0;
        int height = 0;

        auto operator<=>(const Key &) const = default;
    };

    struct Request {
        Done done;
    };

    struct InFlight {
        base::Job job;
        std::vector<std::weak_ptr<Request>> requests;
    };

    void finish(const Key &key, const ui::Bitmap &image);
    bool isShown(const Key &key) const;
    // With `restart` false, only when it is not counting already.
    void scheduleExpiry(bool restart);

    std::string m_directory;
    std::map<Key, InFlight> m_inFlight;
    // Most recent first.
    std::list<std::pair<Key, ui::Bitmap>> m_images;
    std::vector<Key> m_shown;
    std::unique_ptr<base::Timer> m_expiry;
    std::chrono::milliseconds m_expiryDelay = kExpiry;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_WALLPAPER_WALLPAPER_CACHE_H_
