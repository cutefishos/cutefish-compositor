// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/shell.h"

#include <malloc.h>

#include <algorithm>
#include <chrono>

#include "base/dbus.h"
#include "base/settings.h"
#include "core/preferences.h"
#include "decoration/decoration_theme.h"
#include "display/display_manager.h"
#include "shell/common/app_launcher.h"
#include "shell/common/application_badges.h"
#include "shell/common/applications.h"
#include "shell/common/file_thread.h"
#include "shell/common/task_manager.h"
#include "shell/common/wm_task_windows.h"
#include "shell/shell_host.h"
#include "shell/shell_window.h"
#include "ui/embedded_window.h"
#include "shell/shell_input.h"
#include "shell/window_renderer.h"
#include "toolkit/toolkit_host.h"
#include "ui/application.h"
#include "ui/theme.h"
#include "wm/overview/overview_controller.h"
#include "wm/window.h"
#include "wm/window_manager.h"

namespace cutefish::shell {

class Shell::ThemeFollower : public decoration::ThemeObserver
{
public:
    ThemeFollower(Shell *shell, decoration::Theme *theme, base::dbus::Connection *bus,
                  const std::string &settingsPath)
        : m_shell(shell)
        , m_theme(theme)
        , m_settingsPath(settingsPath.empty() ? preferencesFile("services") : settingsPath)
    {
        reloadPreferences(false);
        if (bus) {
            m_appearance = base::dbus::Proxy(bus, "org.cutefish.Services",
                                             "/org/cutefish/Services/Appearance",
                                             "org.cutefish.Services.Appearance");
            // The service writes services.conf before it signals.
            m_iconThemeChanged = m_appearance.onSignal(
                    "iconThemeChanged", [this](const base::dbus::Message &) { reloadPreferences(true); });
            m_accentChanged = m_appearance.onSignal(
                    "accentColorChanged", [this](const base::dbus::Message &) { reloadPreferences(true); });
            m_customAccentChanged = m_appearance.onSignal(
                    "customAccentColorChanged", [this](const base::dbus::Message &) { reloadPreferences(true); });
            m_tintedIconsChanged = m_appearance.onSignal(
                    "tintedIconsChanged", [this](const base::dbus::Message &) { reloadPreferences(true); });
        }
        if (m_theme)
            m_theme->addObserver(this);
        apply();
    }

    ~ThemeFollower() override
    {
        if (m_theme)
            m_theme->removeObserver(this);
    }

    void onThemeChanged() override { reloadPreferences(true); }

private:
    void reloadPreferences(bool triggerApply)
    {
        const base::Settings settings(m_settingsPath);
        m_iconTheme = settings.stringValue("Appearance/IconTheme", "Cutefish");
        m_fontFamily = settings.stringValue("Appearance/Font", "");
        m_fontPointSize = settings.doubleValue("Appearance/FontSize", 9);
        m_accentIndex = int(settings.intValue("Appearance/AccentColor", 0));
        m_customAccentColor = settings.stringValue("Appearance/CustomAccentColor", "");
        m_tintedIcons = settings.boolValue("Appearance/TintedIcons", false);
        if (triggerApply)
            apply();
    }


static std::optional<ui::Color> parseHexUiColor(const std::string &hex)
{
    if (hex.size() != 7 || hex.front() != '#')
        return std::nullopt;
    uint32_t rgb = 0;
    for (size_t i = 1; i < 7; ++i) {
        const char c = hex[i];
        uint32_t nib = 0;
        if (c >= '0' && c <= '9')
            nib = uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f')
            nib = uint32_t(10 + (c - 'a'));
        else if (c >= 'A' && c <= 'F')
            nib = uint32_t(10 + (c - 'A'));
        else
            return std::nullopt;
        rgb = (rgb << 4) | nib;
    }
    return ui::Color::rgb(rgb);
}

    void apply()
    {
        static constexpr ui::Color kAccents[] = {ui::accent::blue,   ui::accent::red,
                                                 ui::accent::green,  ui::accent::purple,
                                                 ui::accent::pink,   ui::accent::orange,
                                                 ui::accent::grey};
        const bool dark = m_theme && m_theme->darkMode();
        ui::Theme theme = dark ? ui::Theme::dark() : ui::Theme::light();
        const std::string &customHex = !m_customAccentColor.empty()
                ? m_customAccentColor
                : (m_theme ? m_theme->customAccentColor() : std::string());
        const int accent = m_theme ? m_theme->accentColor() : m_accentIndex;
        if (const auto custom = parseHexUiColor(customHex))
            theme.highlight = *custom;
        else if (accent >= 0 && accent < int(std::size(kAccents)))
            theme.highlight = kAccents[accent];
        theme.tintedIcons = m_tintedIcons || (m_theme && m_theme->tintedIcons());
        theme.iconTheme = m_iconTheme;
        theme.fontFamily = m_fontFamily;
        theme.fontPointSize = m_fontPointSize > 0 ? m_fontPointSize : 9;
        theme.continuousCorners = ui::Theme::current().continuousCorners;
        ui::Theme::setCurrent(theme);
        if (m_shell) {
            for (ShellWindow *w : m_shell->windows()) {
                if (w && w->window())
                    w->window()->requestPaint();
                if (w)
                    w->requestFrame();
            }
        }
    }

    Shell *m_shell;
    decoration::Theme *m_theme;
    std::string m_settingsPath;
    base::dbus::Proxy m_appearance;
    base::dbus::Match m_iconThemeChanged;
    base::dbus::Match m_accentChanged;
    base::dbus::Match m_customAccentChanged;
    base::dbus::Match m_tintedIconsChanged;
    std::string m_iconTheme;
    std::string m_fontFamily;
    double m_fontPointSize = 9;
    int m_accentIndex = 0;
    std::string m_customAccentColor;
    bool m_tintedIcons = false;
};

Shell::Shell(const ShellEnvironment &environment)
    : m_environment(environment)
{
    // Before any window: it sets the toolkit up on the main loop.
    ui::Application &application = toolkit::application();
    if (bus())
        application.followSessionLanguage(bus());
    m_languageChanged = application.languageChanged.connect([this] {
        if (wm::OverviewController *overview = windowManager()->overview())
            overview->retranslate();
    });
    m_renderer = std::make_unique<WindowRenderer>(m_environment.compositor);
    m_input = std::make_unique<ShellInput>(this);
    m_host = std::make_unique<ShellHost>(this, m_environment.inputMethod);
    m_theme = std::make_unique<ThemeFollower>(this, m_environment.theme, m_environment.bus,
                                              m_environment.servicesSettings);
    m_outputsChanged = windowManager()->outputsChanged.connect([this] { updateOutputs(); });
    updateOutputs();
    m_trimTimer.setSingleShot(true);
    m_trimTimer.onTimeout = [] {
        fileThread().post([] { malloc_trim(0); });
    };
    releaseFreedMemorySoon();
}

Shell::~Shell()
{
    // Components first, while the windows they own can still reach the input
    // client and the renderer.
    while (!m_components.empty())
        m_components.pop_back();
    m_outputsChanged.reset();
    m_languageChanged.reset();
    m_badges.reset();
    m_launcher.reset();
    if (m_tasks)
        windowManager()->setApplicationResolver({});
    m_tasks.reset();
    m_taskWindows.reset();
    m_applications.reset();
    m_theme.reset();
    m_host.reset();
    m_input.reset();
    m_renderer.reset();
}

display::DisplayManager *Shell::displayManager() const
{
    return windowManager()->displayManager();
}

const Shell::Output *Shell::output(const std::string &name) const
{
    if (m_outputs.empty())
        return nullptr;
    const auto it = std::find_if(m_outputs.begin(), m_outputs.end(),
                                 [&](const Output &output) { return output.name == name; });
    return it != m_outputs.end() ? &*it : &m_outputs.front();
}

const Shell::Output *Shell::outputAt(const base::Point &point) const
{
    if (m_outputs.empty())
        return nullptr;
    const auto it = std::find_if(m_outputs.begin(), m_outputs.end(),
                                 [&](const Output &output) { return output.bounds.contains(point); });
    return it != m_outputs.end() ? &*it : &m_outputs.front();
}

Applications &Shell::applications()
{
    if (!m_applications)
        m_applications = std::make_unique<RegistryApplications>();
    return *m_applications;
}

TaskWindows &Shell::taskWindows()
{
    if (!m_taskWindows)
        m_taskWindows = std::make_unique<WmTaskWindows>(*windowManager());
    return *m_taskWindows;
}

TaskManager &Shell::tasks()
{
    if (!m_tasks) {
        m_tasks = std::make_unique<TaskManager>(taskWindows(), applications());
        windowManager()->setApplicationResolver([tasks = m_tasks.get()](const wm::Window *window) {
            return tasks->applicationOf(window->id());
        });
    }
    return *m_tasks;
}

ApplicationLauncher &Shell::launcher()
{
    if (!m_launcher)
        m_launcher = std::make_unique<ApplicationLauncher>(bus(), m_environment.createActivationToken);
    return *m_launcher;
}

ApplicationBadges &Shell::badges()
{
    if (!m_badges)
        m_badges = std::make_unique<ApplicationBadges>(bus());
    return *m_badges;
}

void Shell::openSettings(const std::vector<std::string> &arguments)
{
    const std::string token = m_environment.createActivationToken ? m_environment.createActivationToken()
                                                                  : std::string();
    ApplicationLauncher::startDetached("cutefish-settings", arguments,
                                       token.empty() ? std::vector<std::string>()
                                                     : std::vector<std::string> {"XDG_ACTIVATION_TOKEN=" + token});
}

void Shell::setApplications(std::unique_ptr<Applications> applications)
{
    m_applications = std::move(applications);
}

void Shell::setTaskWindows(std::unique_ptr<TaskWindows> windows)
{
    m_taskWindows = std::move(windows);
}

base::Rect Shell::workArea(const Output &output) const
{
    return windowManager()->restingWorkAreaForOutput(output.bounds);
}

void Shell::releaseFreedMemorySoon()
{
    m_trimTimer.start(std::chrono::seconds(10));
}

void Shell::addWindow(ShellWindow *window)
{
    m_windows.push_back(window);
}

void Shell::removeWindow(ShellWindow *window)
{
    m_windows.erase(std::remove(m_windows.begin(), m_windows.end(), window), m_windows.end());
}

void Shell::updateOutputs()
{
    std::vector<Output> outputs;
    const display::DisplayManager *displays = displayManager();
    if (displays && displays->applied()) {
        for (const auto &logical : displays->applied()->logicalDisplays) {
            if (logical.bounds.isEmpty())
                continue;
            outputs.push_back(
                    {logical.waylandName, logical.bounds, logical.scale.value(), logical.primary});
        }
    }
    if (outputs.empty()) {
        for (const wm::WindowManager::OutputArea &area : windowManager()->outputAreas())
            outputs.push_back({{}, area.bounds, area.scale, outputs.empty()});
    }
    std::stable_partition(outputs.begin(), outputs.end(),
                          [](const Output &output) { return output.primary; });
    if (outputs == m_outputs)
        return;
    m_outputs = std::move(outputs);
    outputsChanged();
}

} // namespace cutefish::shell
