// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/ripple/ripple_window.h"

#include <cmath>
#include <string>

#include "base/i18n.h"
#include "base/json.h"
#include "base/paths.h"
#include "base/process.h"
#include "base/logging.h"
#include "core/main_loop.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "ui/theme.h"
#include "wm/window_manager.h"

namespace cutefish::shell {

using cutefish::i18n::format;
using cutefish::i18n::tr;

namespace {

const base::LogCategory cfRipple("cutefish.ripple", base::LogLevel::Warning);

// The search panel's liquid glass, so the two float alike.
render::GlassParams panelGlassLiquid()
{
    const bool dark = ui::Theme::current().darkMode;
    render::GlassParams glass = dark ? popupGlass(0.14, 0.15, 0.18, 0.50)
                                     : popupGlass(1.0, 1.0, 1.0, 0.46);
    glass.blurSigma = 8.0f;
    glass.saturation = 1.35f;
    glass.specularIntensity = 0.55f;
    glass.shininess = 60.0f;
    glass.sheen = 0.35f;
    return glass;
}

} // namespace

RippleWindow::RippleWindow(Shell *shell, base::dbus::Connection *bus)
    : m_shell(shell)
    , m_bus(bus)
    , m_client(std::make_unique<RippleClient>(core::mainLoop()))
    , m_closeTimer(core::mainLoop())
{
    m_window = std::make_unique<ShellWindow>(shell, wm::WindowRole::Picker);
    auto view = std::make_unique<RippleView>();
    m_view = view.get();
    m_view->onSend = [this](int index) { sendTo(index); };
    m_view->onRowActivated = [this](int index) { sendTo(index); };
    m_view->onAccept = [this] { acceptOffer(); };
    m_view->onDecline = [this] { declineOffer(); };
    m_view->onOpenFolder = [this] {
        if (!m_doneFolder.empty())
            base::Process::startDetached("xdg-open", {m_doneFolder});
        close();
    };
    m_view->onCloseRequested = [this] { close(); };
    m_view->onPainted = [this] { applyGlass(); };
    m_view->onHeightChanged = [this] {
        if (isOpen())
            place();
    };
    m_window->setContentView(std::move(view));
    m_window->setSize(base::Size(RippleView::kWidth, int(std::lround(m_view->preferredHeight()))));
    m_window->onKeyboardFocusChanged = [this](bool focused) {
        if (!focused && isOpen())
            close();
    };
    m_window->onCloseRequested = [this] { close(); };
    applyGlass();

    m_subscriptions.push_back(m_shell->outputsChanged.connect([this] {
        if (isOpen())
            place();
    }));
    m_subscriptions.push_back(m_client->peersChanged.connect([this] {
        refreshPeers();
    }));
    m_subscriptions.push_back(m_client->incoming.connect([this](RippleOffer offer) {
        m_offer = std::move(offer);
        m_session.clear();
        m_pending.clear();
        m_view->setPending(0);
        m_view->setOffer(m_offer);
        open();
    }));
    m_subscriptions.push_back(m_client->progress.connect([this](RippleTransfer transfer) {
        m_view->setTransfer(std::move(transfer));
        if (!isOpen())
            open();
        else
            place();
    }));
    m_subscriptions.push_back(m_client->finished.connect(
            [this](const std::string &session, bool ok, const std::string &error,
                   const std::string &folder) {
                if (!m_session.empty() && !session.empty() && session != m_session)
                    return;
                m_session.clear();
                if (!error.empty()) {
                    m_view->setMessage(error);
                    return;
                }
                if (!ok)
                    return;

                // As AirDrop: the panel that carried the transfer says how it
                // ended, for a moment, before it closes.
                const bool sent = m_view->mode() == RippleView::Mode::Transfer
                        && m_view->direction() == "send";
                if (sent) {
                    m_doneFolder.clear();
                    m_view->setDone(tr("Sent"), format(tr("to {name}"), {{"name", m_sendingTo}}));
                } else {
                    m_doneFolder = folder;
                    std::string shown = folder;
                    const std::string home = base::paths::home();
                    if (shown.starts_with(home + "/"))
                        shown = "~" + shown.substr(home.size());
                    m_view->setDone(tr("Received"), shown);
                }
                m_closeTimer.onTimeout = [this] { close(); };
                m_closeTimer.setSingleShot(true);
                m_closeTimer.setInterval(std::chrono::milliseconds(1400));
                m_closeTimer.start();
            }));

    m_closeTimer.setSingleShot(true);
    m_closeTimer.onTimeout = [this] { close(); };
}

RippleWindow::~RippleWindow()
{
    m_subscriptions.clear();
    m_object.reset();
    m_window.reset();
    m_client.reset();
}

bool RippleWindow::isOpen() const
{
    return m_window->isShown();
}

base::Rect RippleWindow::geometry() const
{
    const int width = RippleView::kWidth;
    const int height = int(std::lround(m_view->preferredHeight()));
    const Shell::Output *output = m_shell->output({});
    if (!output)
        return base::Rect(0, 0, width, height);
    const base::Rect area = m_shell->workArea(*output);
    return base::Rect(int(std::lround(area.x() + (area.width() - width) / 2.0)), area.y() + 96, width,
                      height);
}

void RippleWindow::place()
{
    m_window->setGeometry(geometry());
    applyGlass();
}

void RippleWindow::applyGlass()
{
    const int height = geometry().height();
    if (height == m_glassHeight)
        return;
    m_glassHeight = height;
    m_window->setBackgroundBlur(base::Rect(0, 0, RippleView::kWidth, height),
                                int(RippleView::kCardRadius), panelGlassLiquid());
}

void RippleWindow::refreshPeers()
{
    if (!m_client->available())
        return;
    m_peers = m_client->peers();
    m_view->setDevices(m_peers);
}

void RippleWindow::open()
{
    m_closeTimer.stop();
    m_client->setPanelVisible(true);
    m_client->announce();
    m_window->setGeometry(geometry());
    applyGlass();
    m_window->show();
    m_view->setFocus();
}

void RippleWindow::close()
{
    m_closeTimer.stop();
    m_doneFolder.clear();
    // An offer the user walked away from can still be answered from a
    // notification; the daemon keeps it for two minutes.
    if (!m_offer.session.empty()) {
        m_client->notifyOffer(m_offer.session);
        m_offer = {};
    }
    m_client->setPanelVisible(false);
    if (isOpen())
        m_window->hide();
}

void RippleWindow::share(const std::string &pathsJson)
{
    m_pending.clear();
    if (const auto parsed = base::json::Value::parse(pathsJson)) {
        if (parsed->isArray()) {
            for (const base::json::Value &value : parsed->toArray()) {
                if (value.isString() && !value.toString().empty())
                    m_pending.push_back(value.toString());
            }
        }
    }
    if (m_pending.empty())
        return;
    m_view->setMessage({});
    m_view->setPending(int(m_pending.size()));
    m_view->setMode(RippleView::Mode::Devices);
    refreshPeers();
    open();
}

void RippleWindow::sendTo(int index)
{
    if (m_pending.empty() || index < 0 || index >= int(m_peers.size()))
        return;
    const RipplePeer &peer = m_peers[size_t(index)];
    const std::string session = m_client->send(m_pending, peer.fingerprint);
    if (session.empty()) {
        m_view->setMessage(tr("Cannot send to that device"));
        return;
    }
    m_pending.clear();
    m_view->setPending(0);
    RippleTransfer transfer;
    transfer.direction = "send";
    m_view->setTransfer(transfer);
    m_session = session;
    m_sendingTo = peer.alias.empty() ? peer.host : peer.alias;
}

void RippleWindow::acceptOffer()
{
    if (m_offer.session.empty())
        return;
    std::vector<std::string> ids;
    for (const RippleOfferFile &file : m_offer.files)
        ids.push_back(file.id);
    m_client->respond(m_offer.session, ids);
    m_session = m_offer.session;
    RippleTransfer transfer;
    transfer.session = m_offer.session;
    transfer.direction = "receive";
    m_view->setTransfer(transfer);
    m_offer = {};
}

void RippleWindow::declineOffer()
{
    if (m_offer.session.empty()) {
        close();
        return;
    }
    m_client->respond(m_offer.session, {});
    m_offer = {};
    close();
}

bool RippleWindow::registerService()
{
    if (m_object)
        return true;
    if (!m_bus)
        return false;
    auto object = std::make_unique<base::dbus::Object>(m_bus, "/RipplePanel", "org.cutefish.RipplePanel");
    object->addMethod("share", [this](const std::string &paths) { share(paths); },
                      base::dbus::ArgumentNames {{"paths"}, {}});
    if (!object->publish()
        || !m_bus->requestName("org.cutefish.RipplePanel", base::dbus::Connection::DoNotQueue)) {
        CF_WARNING(cfRipple) << "Cannot own org.cutefish.RipplePanel";
        return false;
    }
    m_object = std::move(object);
    return true;
}

} // namespace cutefish::shell
