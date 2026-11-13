// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// What the compositor's shell components cost in memory, headless through
// the compositor's own GL renderer, one step at a time:
//
//   cutefish-shell-memory-bench [--scale S] [--dark] [--wallpaper IMAGE]
//                               [--glibc-defaults] [--mmap-threshold BYTES]
//                               [--arena-max N] [--no-thp] [--cpu-panels]
//                               [--hold S] [--cycles N] [--idle S,S...]
//                               [--exit-after STEP] [--slab-probe] [--textures]
//                               COMPONENT...
//
// Steps: the renderer with a wallpaper, the Shell, then each COMPONENT
// (osd, dock, wallpaper, launcher, notifications) added and drawn for a
// second, then each one used `--cycles` times, with the peak while it was
// used, then idle as the compositor idles (a frame only when something asked
// for one) up to each `--idle` mark, then Skia's GPU cache purged and the
// heap trimmed. With wallpaper, the desktop's own picture comes from that
// component instead of the bench. Each step prints this
// process's PSS split by kind of mapping, malloc's bytes in use and free,
// the GPU memory of its DRM client, its threads and the main thread's CPU
// time, with the change since the step before. malloc is set up as the
// compositor sets it up unless --glibc-defaults. Run it on a private session
// bus (dbus-run-session): the components then reach the session's PipeWire
// and services as they do in the compositor.

#include <dlfcn.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/prctl.h>

#include <cctype>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "base/dbus.h"
#include "compositor/compositor.h"
#include "compositor/output_scale.h"
#include "core/main_loop.h"
#include "core/malloc_policy.h"
#include "core/preferences.h"
#include "decoration/decoration.h"
#include "decoration/decoration_theme.h"
#include "decoration/shadow_node.h"
#include "egl_test_util.h"
#include "input/input_manager.h"
#include "input/seat.h"
#include "loop_test_util.h"
#include "render/egl_renderer.h"
#include "render/gl_texture.h"
#include "scene/node.h"
#include "scene/scene.h"
#include "shell/dock/dock.h"
#include "shell/dock/dock_window.h"
#include "shell/launcher/launcher.h"
#include "shell/launcher/launcher_window.h"
#include "shell/notification/notification_ui.h"
#include "shell/notification/notifications.h"
#include "shell/osd/osd.h"
#include "shell/shell.h"
#include "shell/screenshot/screen_capture.h"
#include "shell/screenshot/screen_grabber.h"
#include "shell/shell_window.h"
#include "shell/wallpaper/wallpaper.h"
#include "toolkit/toolkit_host.h"
#include "ui/bitmap.h"
#include "ui/embedded_window.h"
#include "ui/view.h"
#include "wm/shadow_controller.h"
#include "wm/window_manager.h"
#include "wm/wm_shadow_controller_delegate.h"

#include <linux/input-event-codes.h>

using namespace cutefish;

namespace {

const base::Size kNative(2160, 1440);

const char *const kKinds[] = {"anon", "toolkit", "gl-driver", "icu", "fonts", "icons", "libs", "other"};

// KiB, as the kernel reports them.
struct Sample {
    std::map<std::string, int64_t> pss;
    int64_t pssTotal = 0;
    int64_t anonHuge = 0;
    int64_t mallocUsed = 0;
    int64_t mallocFree = 0;
    int64_t vram = 0;
    int64_t gtt = 0;
    int64_t threads = 0;
    double mainCpuMs = 0;
    std::multiset<std::string> threadNames;
};

std::string kindOf(const std::string &path)
{
    const auto has = [&](const char *part) { return path.find(part) != std::string::npos; };
    if (path.empty() || path == "[heap]" || path.rfind("[anon", 0) == 0 || path == "[stack]")
        return "anon";
    if (has("/fonts/") || has("fontconfig"))
        return "fonts";
    if (has("/icons/"))
        return "icons";
    if (has("libicu"))
        return "icu";
    if (has("cutefish-toolkit"))
        return "toolkit";
    if (has("LLVM") || has("gallium") || has("dri") || has("mesa") || has("libz3")
        || has("drm_amdgpu") || has("libelf") || has("libsensors"))
        return "gl-driver";
    if (has(".so"))
        return "libs";
    return "other";
}

// KiB of VRAM and GTT of this process's DRM clients, each counted once.
std::pair<int64_t, int64_t> gpuMemory()
{
    std::pair<int64_t, int64_t> total;
    std::set<std::string> clients;
    std::string line;
    for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fdinfo")) {
        std::ifstream fdinfo(entry.path());
        std::string id;
        int64_t vram = 0, gtt = 0;
        while (std::getline(fdinfo, line)) {
            int64_t value = 0;
            if (line.rfind("drm-client-id:", 0) == 0)
                id = line;
            else if (std::sscanf(line.c_str(), "drm-memory-vram: %" SCNd64, &value) == 1)
                vram = value;
            else if (std::sscanf(line.c_str(), "drm-memory-gtt: %" SCNd64, &value) == 1)
                gtt = value;
        }
        if (id.empty() || !clients.insert(id).second)
            continue;
        total.first += vram;
        total.second += gtt;
    }
    return total;
}

// The highest PSS and GPU memory seen while a step ran, in KiB.
struct Peak {
    int64_t pss = 0;
    int64_t vram = 0;
    int64_t gtt = 0;

    void update()
    {
        std::ifstream rollup("/proc/self/smaps_rollup");
        std::string line;
        while (std::getline(rollup, line)) {
            int64_t value = 0;
            if (std::sscanf(line.c_str(), "Pss: %" SCNd64, &value) == 1)
                pss = std::max(pss, value);
        }
        const auto [v, g] = gpuMemory();
        vram = std::max(vram, v);
        gtt = std::max(gtt, g);
    }
};

Peak peak;

Sample sample()
{
    Sample s;
    std::ifstream smaps("/proc/self/smaps");
    std::string line;
    std::string kind = "other";
    while (std::getline(smaps, line)) {
        const size_t dash = line.find('-');
        if (dash != std::string::npos && dash < 17 && std::isxdigit(uint8_t(line[0]))
            && line.find(':') > dash + 10) {
            std::istringstream fields(line);
            std::string range, perms, offset, device, inode, path;
            fields >> range >> perms >> offset >> device >> inode;
            std::getline(fields, path);
            path.erase(0, std::min(path.size(), path.find_first_not_of(' ')));
            kind = kindOf(path);
            continue;
        }
        int64_t value = 0;
        if (std::sscanf(line.c_str(), "Pss: %" SCNd64, &value) == 1) {
            s.pss[kind] += value;
            s.pssTotal += value;
        } else if (std::sscanf(line.c_str(), "AnonHugePages: %" SCNd64, &value) == 1) {
            s.anonHuge += value;
        }
    }

    timespec cpu {};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu);
    s.mainCpuMs = cpu.tv_sec * 1e3 + cpu.tv_nsec / 1e6;

    const struct mallinfo2 info = mallinfo2();
    s.mallocUsed = int64_t(info.uordblks + info.hblkhd) / 1024;
    s.mallocFree = int64_t(info.fordblks) / 1024;

    std::tie(s.vram, s.gtt) = gpuMemory();
    for (const auto &task : std::filesystem::directory_iterator("/proc/self/task")) {
        std::ifstream comm(task.path() / "comm");
        std::string name;
        std::getline(comm, name);
        s.threadNames.insert(name);
        ++s.threads;
    }
    return s;
}

void printHeader()
{
    std::printf("%-22s %8s", "step", "PSS");
    for (const char *kind : kKinds)
        std::printf(" %9s", kind);
    std::printf(" %8s %8s %8s %8s %8s %4s %8s\n", "thp", "m-used", "m-free", "vram", "gtt", "thr", "main-ms");
}

// MB (10^6) for PSS, as /proc and the session's memory scripts report; MiB
// for the GPU, as its fdinfo does.
void printSample(const std::string &step, const Sample &s, const Sample *before)
{
    const auto mb = [](int64_t kib) { return kib * 1024 / 1e6; };
    const auto mib = [](int64_t kib) { return kib / 1024.0; };
    std::printf("%-22s %8.1f", step.c_str(), mb(s.pssTotal));
    for (const char *kind : kKinds)
        std::printf(" %9.1f", mb(s.pss.count(kind) ? s.pss.at(kind) : 0));
    std::printf(" %8.1f %8.1f %8.1f %8.1f %8.1f %4" PRId64 " %8.1f\n", mib(s.anonHuge), mb(s.mallocUsed),
                mb(s.mallocFree), mib(s.vram), mib(s.gtt), s.threads, s.mainCpuMs);
    if (!before)
        return;
    const auto at = [](const Sample &x, const char *kind) {
        return x.pss.count(kind) ? x.pss.at(kind) : 0;
    };
    std::printf("%-22s %+8.1f", "  change", mb(s.pssTotal - before->pssTotal));
    for (const char *kind : kKinds)
        std::printf(" %+9.1f", mb(at(s, kind) - at(*before, kind)));
    std::printf(" %+8.1f %+8.1f %+8.1f %+8.1f %+8.1f %+4" PRId64 " %+8.1f\n", mib(s.anonHuge - before->anonHuge),
                mb(s.mallocUsed - before->mallocUsed), mb(s.mallocFree - before->mallocFree),
                mib(s.vram - before->vram), mib(s.gtt - before->gtt), s.threads - before->threads,
                s.mainCpuMs - before->mainCpuMs);
    std::multiset<std::string> started, ended;
    std::set_difference(s.threadNames.begin(), s.threadNames.end(), before->threadNames.begin(),
                        before->threadNames.end(), std::inserter(started, started.end()));
    std::set_difference(before->threadNames.begin(), before->threadNames.end(), s.threadNames.begin(),
                        s.threadNames.end(), std::inserter(ended, ended.end()));
    if (!started.empty() || !ended.empty()) {
        std::printf("%-22s", "  threads");
        for (const std::string &name : started)
            std::printf(" +%s", name.c_str());
        for (const std::string &name : ended)
            std::printf(" -%s", name.c_str());
        std::printf("\n");
    }
    std::fflush(stdout);
}

double msSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

class Renderer
{
public:
    bool init()
    {
        m_renderer = render::EglRendererTestAccess::create();
        if (!m_renderer)
            return false;
        const EGLint attributes[] = {EGL_WIDTH, kNative.width(), EGL_HEIGHT, kNative.height(), EGL_NONE};
        const auto surface = eglCreatePbufferSurface(m_renderer->eglDisplay(), m_renderer->eglConfig(),
                                                     attributes);
        if (surface == EGL_NO_SURFACE)
            return false;
        m_target = std::make_unique<render::EglRenderTarget>(m_renderer->eglDisplay(), surface, nullptr,
                                                             kNative);
        return true;
    }

    render::EglRenderer *renderer() const { return m_renderer.get(); }
    render::EglRenderTarget *target() const { return m_target.get(); }

private:
    std::unique_ptr<render::EglRenderer> m_renderer;
    std::unique_ptr<render::EglRenderTarget> m_target;
};

// One output with a wallpaper and a status bar's reservation, rendered as
// the compositor renders it; the shell and its components come later.
class Desktop
{
public:
    Desktop(Renderer &gl, double scale, bool dark, const ui::Bitmap &wallpaper)
        : m_gl(gl)
        , m_scale(scale)
        , m_output(0, 0, int(std::lround(kNative.width() / scale)), int(std::lround(kNative.height() / scale)))
    {
        m_theme.setDarkMode(dark);
        m_scene.setSize(m_output.size());
        m_compositor.setOutputScale(*compositor::OutputScale::fromNumerator(uint32_t(std::lround(scale * 120))));
        m_manager.setOutputBounds(m_output);
        m_input.setBounds(m_output.size());
        m_input.setEventSink(&m_manager);
        m_manager.setFrameProvider(&m_frames);
        m_manager.setVisualStyle(m_theme.visualStyle());
        m_manager.setExclusiveZone(&m_statusBar, wm::ScreenEdge::Top, 32);
        m_scene.root()->setChangeCallback([this] { m_dirty = true; });
        m_repaint = m_compositor.repaintNeeded.connect([this](bool) { m_dirty = true; });

        if (!wallpaper.isNull()) {
            render::EglRenderer *renderer = gl.renderer();
            renderer->beginFrame(gl.target());
            const ui::Bitmap bgra = wallpaper.converted(ui::Bitmap::Format::Argb32Premultiplied);
            m_wallpaper = renderer->createTexture(bgra.size(), bgra.constBits(), int(bgra.stride()),
                                                  render::PixelFormat::Bgra8888);
            renderer->cancelFrame();
            m_background->setTexture(m_wallpaper.get());
            m_background->setGeometry(m_output);
        }
    }

    ~Desktop()
    {
        render::EglRenderer *renderer = m_gl.renderer();
        renderer->beginFrame(m_gl.target());
        m_shell.reset();
        m_background->setTexture(nullptr);
        m_wallpaper.reset();
        m_scene.root()->releaseRenderResources();
        renderer->clearTextureCache();
        renderer->cancelFrame();
        m_seat.shutdown();
    }

    void createShell(base::dbus::Connection *bus)
    {
        shell::ShellEnvironment environment;
        environment.compositor = &m_compositor;
        environment.windowManager = &m_manager;
        environment.seat = &m_seat;
        environment.bus = bus;
        environment.theme = &m_theme;
        m_shell = std::make_unique<shell::Shell>(environment);
    }

    shell::Shell *shell() const { return m_shell.get(); }
    const base::Rect &output() const { return m_output; }

    void move(double x, double y)
    {
        m_time += 1;
        m_input.onPointerMotionAbsolute(stamp(), {x, y});
    }

    void click(double x, double y, uint32_t button)
    {
        move(x, y);
        m_input.onPointerButton(stamp(), button, true);
        m_input.onPointerButton(stamp(), button, false);
    }

    void key(uint32_t code)
    {
        m_input.onKey(stamp(), code, true);
        m_input.onKey(stamp(), code, false);
    }

    // Frames 16.7 ms apart, with the loop turning between them, for `ms`.
    void run(int ms)
    {
        for (int elapsed = 0; elapsed <= ms; elapsed += 17) {
            test::runFor(std::chrono::milliseconds(17));
            frame();
            if (++m_sinceSample == 6) {
                m_sinceSample = 0;
                peak.update();
            }
        }
    }

    // One frame now, how long it took to draw and for the GPU to finish it.
    double timedFrame()
    {
        test::runFor(std::chrono::milliseconds(17));
        const auto started = std::chrono::steady_clock::now();
        frame();
        glFinish();
        return msSince(started);
    }

    // What creating a panel's texture of `size` costs, as PanelTexture makes
    // one: a cleared buffer uploaded, until the GPU is done.
    double timeTextureCreation(const base::Size &size)
    {
        render::EglRenderer *renderer = m_gl.renderer();
        if (!renderer->beginFrame(m_gl.target()))
            return -1;
        const auto started = std::chrono::steady_clock::now();
        const std::vector<uint8_t> clear(size_t(size.width()) * size.height() * 4, 0);
        auto texture =
                renderer->createTexture(size, clear.data(), size.width() * 4, render::PixelFormat::Rgba8888);
        glFinish();
        const double ms = msSince(started);
        texture.reset();
        renderer->cancelFrame();
        return ms;
    }

    // What uploading `image` as a texture costs, as the lock screen's picture
    // is uploaded in its first frame, until the GPU is done.
    double timeUpload(const ui::Bitmap &image)
    {
        render::EglRenderer *renderer = m_gl.renderer();
        if (!renderer->beginFrame(m_gl.target()))
            return -1;
        const auto started = std::chrono::steady_clock::now();
        auto texture = renderer->createTexture(image.size(), image.constBits(), int(image.stride()),
                                               render::PixelFormat::Bgra8888);
        glFinish();
        const double ms = msSince(started);
        texture.reset();
        renderer->cancelFrame();
        return ms;
    }

    // Textures of `size` made with the context current, as panels and icons
    // are; empty ones when the GPU is unavailable.
    std::vector<std::unique_ptr<render::Texture>> makeTextures(int count, const base::Size &size)
    {
        std::vector<std::unique_ptr<render::Texture>> textures;
        render::EglRenderer *renderer = m_gl.renderer();
        if (!renderer->beginFrame(m_gl.target()))
            return textures;
        const std::vector<uint8_t> pixels(size_t(size.width()) * size.height() * 4, 0x80);
        for (int i = 0; i < count; ++i)
            textures.push_back(
                    renderer->createTexture(size, pixels.data(), size.width() * 4, render::PixelFormat::Rgba8888));
        glFinish();
        renderer->cancelFrame();
        return textures;
    }

    void dropTextures(std::vector<std::unique_ptr<render::Texture>> &textures)
    {
        render::EglRenderer *renderer = m_gl.renderer();
        if (!renderer->beginFrame(m_gl.target()))
            return;
        textures.clear();
        glFinish();
        renderer->cancelFrame();
    }

    // The loop turning for `ms`, with a frame on a refresh only when the
    // scene or a surface asked for one, as the compositor's output does.
    // Returns the frames drawn.
    int idle(int ms)
    {
        int frames = 0;
        for (int elapsed = 0; elapsed <= ms; elapsed += 17) {
            test::runFor(std::chrono::milliseconds(17));
            if (!m_dirty) {
                m_time += 1000.0 / 60;
                continue;
            }
            m_dirty = false;
            frame();
            ++frames;
        }
        return frames;
    }

private:
    input::Timestamp stamp() const
    {
        return input::Timestamp(std::chrono::microseconds(int64_t(m_time * 1000)));
    }

    void frame()
    {
        render::EglRenderer *renderer = m_gl.renderer();
        m_time += 1000.0 / 60;
        m_compositor.frameStarted(m_time);
        renderer->setOutputContext({"out", m_output, kNative, m_scale, 0, ++m_frame});
        renderer->frameTime = m_time;
        renderer->framePeriod = 1000.0 / 60;
        if (!renderer->beginFrame(m_gl.target()))
            return;
        m_compositor.updateTextures(*renderer);
        m_scene.render(*renderer);
        renderer->endFrame();
        renderer->frameTime = -1;
    }

    Renderer &m_gl;
    const double m_scale;
    const base::Rect m_output;
    double m_time = 100000;
    uint64_t m_frame = 0;
    int m_sinceSample = 0;
    bool m_dirty = true;
    int m_statusBar = 0;
    scene::Scene m_scene;
    scene::TextureNode *m_background =
            static_cast<scene::TextureNode *>(m_scene.root()->addChild(
                    std::make_unique<scene::TextureNode>(nullptr, "wallpaper")));
    scene::Node *m_surfaces = m_scene.root()->addChild(std::make_unique<scene::Node>("surfaces"));
    compositor::Compositor m_compositor {m_surfaces};
    decoration::Theme m_theme;
    decoration::DecorationProvider m_frames {&m_theme};
    input::Seat m_seat {&m_compositor};
    input::InputManager m_input {&m_seat};
    wm::WindowManager m_manager {&m_compositor, &m_seat};
    decoration::ShadowProvider m_shadows {&m_theme};
    wm::WmShadowControllerDelegate m_shadowDelegate;
    wm::ShadowController m_shadowController {&m_manager, &m_shadows, &m_shadowDelegate};
    std::unique_ptr<render::Texture> m_wallpaper;
    base::Subscription m_repaint;
    std::unique_ptr<shell::Shell> m_shell;
};


// This run's own directory, removed at exit.
std::filesystem::path scratchDirectory()
{
    return std::filesystem::temp_directory_path() / ("cutefish-shell-memory-bench-" + std::to_string(getpid()));
}

// A file or directory of this run's own, so nothing here writes the user's.
std::string scratchPath(const std::string &name)
{
    static const std::filesystem::path directory = [] {
        std::filesystem::create_directories(scratchDirectory());
        return scratchDirectory();
    }();
    return (directory / name).string();
}

// The user's shell.conf, copied so that nothing here writes to it.
std::string shellConf()
{
    static const std::string copy = [] {
        const std::string path = scratchPath("shell.conf");
        std::error_code error;
        std::filesystem::copy_file(preferencesFile("shell"), path,
                                   std::filesystem::copy_options::overwrite_existing, error);
        return path;
    }();
    return copy;
}

// The pictures the wallpaper step goes through; the first is the one shown
// before it is used.
std::vector<std::string> &wallpaperFiles()
{
    static std::vector<std::string> files;
    return files;
}

// A component: added to the shell, then used as a person would once.
struct Component {
    std::function<void(Desktop &, base::dbus::Connection *)> add;
    std::function<void(Desktop &)> exercise;
};

shell::Dock *dockComponentService = nullptr;

// Owns a dock service no dock window shows, for the shell to destroy with
// the other components.
struct DockServiceHolder : shell::Component {
    std::unique_ptr<shell::Dock> dock;
};

// The dock's service for the launcher: the dock component's when it was
// added, made here otherwise.
shell::Dock *dockService(Desktop &desktop, base::dbus::Connection *bus)
{
    if (dockComponentService)
        return dockComponentService;
    static shell::Dock *own = nullptr;
    if (!own) {
        auto holder = std::make_unique<DockServiceHolder>();
        holder->dock = std::make_unique<shell::Dock>(bus, desktop.shell()->tasks(), desktop.shell()->launcher(),
                                                     shellConf());
        own = holder->dock.get();
        desktop.shell()->addComponent(std::move(holder));
    }
    return own;
}

std::map<std::string, Component> components()
{
    std::map<std::string, Component> all;
    static shell::Osd *osd = nullptr;
    all["osd"] = {
            [](Desktop &desktop, base::dbus::Connection *bus) {
                osd = desktop.shell()->addComponent(std::make_unique<shell::Osd>(
                        desktop.shell(), shell::createSessionOsdLevels(bus)));
            },
            [](Desktop &desktop) {
                osd->volumeUp();
                desktop.run(600);
                osd->volumeDown();
                osd->brightnessUp();
                desktop.run(600);
                osd->brightnessDown();
                desktop.run(2000);
            }};
    // The OSD shown once, then left to time out and fade with only the
    // frames it asks for, as after a volume key. Needs osd.
    all["osd-quiet"] = {
            [](Desktop &, base::dbus::Connection *) {},
            [](Desktop &desktop) {
                osd->volumeUp();
                desktop.run(300);
                osd->volumeDown();
                desktop.idle(5000);
            }};
    // The OSD shown again 1 s after it faded, while its glass textures are
    // held, and after 12 s of idle, past their release where there is one;
    // first frames in ms. Needs osd.
    all["osd-reopen"] = {
            [](Desktop &, base::dbus::Connection *) {},
            [](Desktop &desktop) {
                for (const int idleMs : {1000, 12000}) {
                    for (int run = 0; run < 10; ++run) {
                        osd->volumeUp();
                        const double first = desktop.timedFrame();
                        desktop.run(300);
                        osd->volumeDown();
                        desktop.idle(3000);
                        desktop.idle(idleMs);
                        osd->volumeUp();
                        const double again = desktop.timedFrame();
                        desktop.run(300);
                        osd->volumeDown();
                        desktop.idle(3000 + 12000);
                        std::printf("osd reopen after %d ms idle run %d: first frame %.2f ms (opening %.2f)\n",
                                    idleMs, run, again, first);
                    }
                }
                std::fflush(stdout);
            }};
    // A full-output picture uploaded as the lock screen's is, ten times; ms.
    all["upload"] = {
            [](Desktop &, base::dbus::Connection *) {},
            [](Desktop &desktop) {
                ui::Bitmap picture(kNative, ui::Bitmap::Format::Argb32Premultiplied);
                picture.fill(0xff336699u);
                for (int run = 0; run < 10; ++run)
                    std::printf("upload %dx%d run %d: %.2f ms\n", kNative.width(), kNative.height(), run,
                                desktop.timeUpload(picture));
                std::fflush(stdout);
            }};
    static shell::DockWindow *dock = nullptr;
    all["dock"] = {
            [](Desktop &desktop, base::dbus::Connection *bus) {
                auto service = std::make_unique<shell::Dock>(bus, desktop.shell()->tasks(),
                                                             desktop.shell()->launcher(),
                                                             shellConf());
                dockComponentService = service.get();
                dock = desktop.shell()->addComponent(
                        std::make_unique<shell::DockWindow>(desktop.shell(), std::move(service)));
                test::runUntil([] { return dockComponentService->applications().isLoaded(); },
                               std::chrono::seconds(10));
            },
            [](Desktop &desktop) {
                const base::Rect bounds = dock->window()->geometry();
                const double y = bounds.y() + bounds.height() / 2.0;
                const auto icons = dock->icons();
                for (size_t i = 0; i < icons.size(); ++i) {
                    desktop.move(bounds.x() + bounds.width() * (i + 0.5) / icons.size(), y);
                    desktop.run(300);
                }
                desktop.click(bounds.x() + bounds.width() * 1.5 / std::max<size_t>(icons.size(), 1), y,
                              BTN_RIGHT);
                desktop.run(600);
                desktop.key(KEY_ESC);
                desktop.move(desktop.output().width() * 0.45, desktop.output().height() * 0.45);
                desktop.run(1200);
            }};
    // The dock's menu opened and closed, with only its closing frames after,
    // as the compositor goes idle after a popup closes. Needs dock.
    all["dock-menu"] = {
            [](Desktop &, base::dbus::Connection *) {},
            [](Desktop &desktop) {
                const base::Rect bounds = dock->window()->geometry();
                const double y = bounds.y() + bounds.height() / 2.0;
                const size_t icons = std::max<size_t>(dock->icons().size(), 1);
                desktop.click(bounds.x() + bounds.width() * 1.5 / icons, y, BTN_RIGHT);
                desktop.run(600);
                desktop.key(KEY_ESC);
                desktop.run(250);
            }};
    static shell::Wallpaper *wallpaper = nullptr;
    all["wallpaper"] = {
            [](Desktop &desktop, base::dbus::Connection *) {
                wallpaper = desktop.shell()->addComponent(std::make_unique<shell::Wallpaper>(
                        desktop.shell(), scratchPath("wallpapers"), scratchPath("services.conf")));
                wallpaper->setBackground({0, wallpaperFiles().front(), {}});
                test::runUntil([] { return wallpaper->isReady(); }, std::chrono::seconds(10));
            },
            [](Desktop &desktop) {
                // Each picture once, then back to the first, as a person
                // trying a few in Settings would.
                std::vector<std::string> files = wallpaperFiles();
                files.push_back(files.front());
                for (size_t i = 1; i < files.size(); ++i) {
                    wallpaper->setBackground({0, files[i], {}});
                    const auto shown = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    while (!wallpaper->isReady() && std::chrono::steady_clock::now() < shown)
                        desktop.run(100);
                    desktop.run(500);
                }
            }};
    static shell::LauncherWindow *launcher = nullptr;
    all["launcher"] = {
            [](Desktop &desktop, base::dbus::Connection *bus) {
                auto service = std::make_unique<shell::Launcher>(bus, desktop.shell()->tasks(),
                                                                 desktop.shell()->launcher(),
                                                                 dockService(desktop, bus), shellConf());
                launcher = desktop.shell()->addComponent(std::make_unique<shell::LauncherWindow>(
                        desktop.shell(), std::move(service), dockService(desktop, bus), wallpaper));
            },
            [](Desktop &desktop) {
                launcher->launcher().show();
                desktop.run(1500);
                launcher->launcher().hide();
                desktop.run(1000);
            }};
    // The launcher opened again, as it is and with its whole content
    // repainted first as a released texture would make it, then the texture
    // creation that release adds; first frames in ms. Needs launcher.
    all["launcher-reopen"] = {
            [](Desktop &, base::dbus::Connection *) {},
            [](Desktop &desktop) {
                const auto open = [&](bool repaint) {
                    if (repaint)
                        launcher->window()->window()->contentView()->requestPaint();
                    launcher->launcher().show();
                    const double first = desktop.timedFrame();
                    const double second = desktop.timedFrame();
                    desktop.run(1500);
                    launcher->launcher().hide();
                    desktop.run(1000);
                    return std::pair(first, second);
                };
                for (const bool repaint : {false, true}) {
                    for (int run = 0; run < 10; ++run) {
                        const auto [first, second] = open(repaint);
                        std::printf("reopen %s run %d: first frame %.2f ms, second %.2f ms\n",
                                    repaint ? "repainted" : "kept", run, first, second);
                    }
                }
                const base::Size size(launcher->window()->size().width() * 3 / 2,
                                      launcher->window()->size().height() * 3 / 2);
                for (int run = 0; run < 10; ++run)
                    std::printf("texture %dx%d creation run %d: %.2f ms\n", size.width(), size.height(), run,
                                desktop.timeTextureCreation(size));
                std::fflush(stdout);
            }};
    // The capture toolbar opened on a fresh ScreenCapture, whose overlay is
    // made and drawn for the first time, then again with the overlay kept;
    // first frames in ms, six instances.
    static std::vector<shell::ScreenCapture *> captures;
    all["capture"] = {
            [](Desktop &desktop, base::dbus::Connection *) {
                for (int i = 0; i < 6; ++i) {
                    shell::ScreenCaptureServices services;
                    services.settingsPath = shellConf();
                    captures.push_back(desktop.shell()->addComponent(
                            std::make_unique<shell::ScreenCapture>(desktop.shell(), std::move(services))));
                }
            },
            [](Desktop &desktop) {
                for (size_t i = 0; i < captures.size(); ++i) {
                    double first[2];
                    for (double &ms : first) {
                        captures[i]->startCaptureMode();
                        ms = desktop.timedFrame();
                        desktop.run(600);
                        captures[i]->cancel();
                        desktop.run(600);
                    }
                    std::printf("capture %zu: first frame %.2f ms new, %.2f ms kept\n", i, first[0], first[1]);
                }
                std::fflush(stdout);
            }};
    static shell::NotificationUi *notifications = nullptr;
    // Whether the bench owns the notification names, so that what it sends
    // reaches it and not a session that already has a notification server.
    static bool notificationsServed = false;
    all["notifications"] = {
            [](Desktop &desktop, base::dbus::Connection *bus) {
                shell::Notifications::Files files {shellConf(), scratchPath("notification-history.json"),
                                                   scratchPath("notification-images")};
                notifications = desktop.shell()->addComponent(std::make_unique<shell::NotificationUi>(
                        desktop.shell(), std::make_unique<shell::Notifications>(
                                                 bus, desktop.shell()->applications(), &desktop.shell()->tasks(),
                                                 files)));
                notificationsServed = bus && notifications->registerServices();
                if (!notificationsServed)
                    std::fprintf(stderr, "notifications: the bus names are taken; nothing is sent\n");
            },
            [](Desktop &desktop) {
                // A burst from one program, each with an icon, as a chat or
                // a build would send; the banners then time out.
                if (notificationsServed)
                    std::system("for i in $(seq 1 20); do notify-send -a Bench -i dialog-information "
                                "\"Message $i\" \"Line one of the body $i\"; done &");
                desktop.run(4000);
                desktop.run(8000);
            }};
    return all;
}

} // namespace

int main(int argc, char **argv)
{
    double scale = 1.5;
    bool dark = false;
    int holdSeconds = 0;
    int cycles = 1;
    std::string exitAfter;
    bool slabProbe = false;
    bool textureReport = false;
    std::vector<int> idleMarks;
    std::string wallpaperPath = "/usr/share/backgrounds/cutefishos/default.jpg";
    std::vector<std::string> wanted;
    if (std::none_of(argv + 1, argv + argc, [](const char *arg) { return std::string(arg) == "--glibc-defaults"; }))
        core::applyMallocPolicy();
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--scale" && i + 1 < argc)
            scale = std::stod(argv[++i]);
        else if (arg == "--dark")
            dark = true;
        else if (arg == "--glibc-defaults")
            continue;
        else if (arg == "--cpu-panels")
            toolkit::setGpuRenderingAllowed(false);
        else if (arg == "--hold" && i + 1 < argc)
            holdSeconds = std::stoi(argv[++i]);
        else if (arg == "--no-thp")
            prctl(PR_SET_THP_DISABLE, 1, 0, 0, 0);
        else if (arg == "--mmap-threshold" && i + 1 < argc)
            mallopt(M_MMAP_THRESHOLD, std::stoi(argv[++i]));
        else if (arg == "--arena-max" && i + 1 < argc)
            mallopt(M_ARENA_MAX, std::stoi(argv[++i]));
        else if (arg == "--wallpaper" && i + 1 < argc)
            wallpaperPath = argv[++i];
        else if (arg == "--wallpapers" && i + 1 < argc) {
            std::istringstream list(argv[++i]);
            for (std::string file; std::getline(list, file, ',');)
                wallpaperFiles().push_back(file);
        } else if (arg == "--slab-probe")
            slabProbe = true;
        else if (arg == "--textures")
            textureReport = true;
        else if (arg == "--exit-after" && i + 1 < argc)
            exitAfter = argv[++i];
        else if (arg == "--cycles" && i + 1 < argc)
            cycles = std::max(1, std::stoi(argv[++i]));
        else if (arg == "--idle" && i + 1 < argc) {
            std::istringstream list(argv[++i]);
            for (std::string mark; std::getline(list, mark, ',');)
                idleMarks.push_back(std::stoi(mark));
        } else
            wanted.push_back(arg);
    }
    auto all = components();
    for (const std::string &name : wanted) {
        if (!all.count(name)) {
            std::fprintf(stderr, "unknown component %s; known:", name.c_str());
            for (const auto &[known, component] : all)
                std::fprintf(stderr, " %s", known.c_str());
            std::fprintf(stderr, "\n");
            return 2;
        }
    }

    const bool ownWallpaper = std::find(wanted.begin(), wanted.end(), "wallpaper") == wanted.end();
    if (wallpaperFiles().empty())
        wallpaperFiles() = {wallpaperPath, "/usr/share/backgrounds/cutefishos/wallpaper-1.jpg",
                            "/usr/share/backgrounds/cutefishos/wallpaper-2.jpg"};

    // The GL allocation tracker, when it is preloaded for a look at who
    // holds which texture.
    using Report = void (*)(const char *);
    const auto glReport = reinterpret_cast<Report>(dlsym(RTLD_DEFAULT, "gltrack_report"));

    core::MainLoop loop;
    base::dbus::Connection *bus = base::dbus::Connection::session(core::mainLoop());
    Sample last = sample();
    printHeader();
    printSample("process", last, nullptr);
    // With `showPeak`, also the highest PSS and GPU memory since the last step.
    const auto step = [&](const std::string &name, bool showPeak = false) {
        const Sample now = sample();
        printSample(name, now, &last);
        last = now;
        if (showPeak)
            std::printf("%-22s PSS %.1f MB, vram %.1f MiB, gtt %.1f MiB\n", "  peak", peak.pss * 1024 / 1e6,
                        peak.vram / 1024.0, peak.gtt / 1024.0);
        if (glReport)
            glReport(name.c_str());
        peak = {};
        // Leaves with everything of the step still allocated, for a heap
        // profiler to list as what the step holds.
        if (!exitAfter.empty() && name.rfind(exitAfter, 0) == 0) {
            std::fflush(stdout);
            std::exit(0);
        }
    };

    Renderer gl;
    if (!gl.init()) {
        std::fprintf(stderr, "no EGL renderer\n");
        return 1;
    }
    const ui::Bitmap wallpaper = ownWallpaper
            ? ui::Bitmap::decodeFile(wallpaperPath, kNative, ui::Bitmap::Scaling::Cover)
            : ui::Bitmap();
    {
        Desktop desktop(gl, scale, dark, wallpaper);
        desktop.run(500);
        step("renderer+wallpaper");
        // What the driver does with small textures: made, freed, left idle,
        // then one more made, which is when it can take freed space back.
        // Each step waits 8 s for a look at the driver's buffer list.
        if (slabProbe) {
            const auto probe = [&](const std::string &name) {
                step(name);
                std::printf("probe pid %d\n", int(getpid()));
                std::fflush(stdout);
                desktop.idle(8000);
            };
            probe("slab: renderer");
            auto small = desktop.makeTextures(300, {64, 64});
            probe("slab: 300 at 64x64");
            auto medium = desktop.makeTextures(300, {200, 200});
            probe("slab: 300 at 200x200");
            desktop.dropTextures(small);
            desktop.dropTextures(medium);
            probe("slab: freed");
            desktop.idle(3000);
            probe("slab: freed, idle");
            // Allocations of each slab size, then one too large for a slab,
            // which is what lets the driver take freed slabs and buffers back.
            for (int round = 0; round < 2; ++round) {
                for (const base::Size &size : {base::Size(64, 64), base::Size(200, 200)}) {
                    auto one = desktop.makeTextures(1, size);
                    desktop.dropTextures(one);
                }
                desktop.idle(1000);
                auto large = desktop.makeTextures(1, {1024, 1024});
                desktop.dropTextures(large);
                desktop.idle(1000);
            }
            probe("slab: after reclaim");
        }
        auto started = std::chrono::steady_clock::now();
        desktop.createShell(bus);
        std::printf("created shell in %.1f ms\n", msSince(started));
        desktop.run(500);
        step("shell");
        for (const std::string &name : wanted) {
            started = std::chrono::steady_clock::now();
            all[name].add(desktop, bus);
            std::printf("created %s in %.1f ms\n", name.c_str(), msSince(started));
            desktop.run(1000);
            step("+" + name);
        }
        for (const std::string &name : wanted) {
            peak.update();
            for (int cycle = 0; cycle < cycles; ++cycle)
                all[name].exercise(desktop);
            step(name + " used" + (cycles > 1 ? " x" + std::to_string(cycles) : ""), true);
        }
        int idled = 0;
        for (const int mark : idleMarks) {
            const int frames = desktop.idle((mark - idled) * 1000);
            idled = mark;
            step("idle " + std::to_string(mark) + " s, " + std::to_string(frames) + " fr");
        }
        desktop.run(3000);
        step("idle 3 s");
        toolkit::purgeGpuResources();
        // The driver keeps freed buffers about a second for reuse.
        desktop.run(2000);
        step("skia purged");
        malloc_trim(0);
        step("malloc_trim");
        // The compositor's own GL textures by owner, as the debug service
        // reports them.
        if (textureReport) {
            const render::GlTextureUsage usage = render::GlTexture::usage();
            std::printf("textures: %zu owned %.1f MiB, %zu imported\n", usage.owned.textures,
                        usage.owned.bytes / 1048576.0, usage.imported.textures);
            for (const auto &[label, count] : usage.byLabel)
                std::printf("  %-20s %4zu %7.1f MiB\n", label ? label : "(unlabelled)", count.textures,
                            count.bytes / 1048576.0);
        }
        // For a look from outside, e.g. at the GPU driver's buffer list.
        if (holdSeconds > 0) {
            std::printf("holding pid %d for %d s\n", int(getpid()), holdSeconds);
            std::fflush(stdout);
            // As the compositor idles, a step a minute, to see what the
            // kernel does meanwhile (khugepaged collapsing trimmed heap).
            for (int held = 0; held < holdSeconds;) {
                const int chunk = std::min(60, holdSeconds - held);
                desktop.idle(chunk * 1000);
                held += chunk;
                step("held " + std::to_string(held) + " s");
            }
        }
    }
    std::error_code error;
    std::filesystem::remove_all(scratchDirectory(), error);
    return 0;
}
