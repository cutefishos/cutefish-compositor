// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_RIPPLE_RIPPLE_VIEW_H_
#define CUTEFISH_SHELL_RIPPLE_RIPPLE_VIEW_H_

#include <functional>
#include <string>
#include <vector>

#include "shell/ripple/ripple_client.h"
#include "ui/button.h"
#include "ui/label.h"
#include "ui/list_view.h"
#include "ui/progress_bar.h"
#include "ui/view.h"

namespace cutefish::shell {

// Ripple's panel: the devices nearby to send to, an offer to answer, or a
// transfer on its way. One panel, three faces.
class RippleView : public ui::View
{
public:
    enum class Mode {
        Devices,
        Incoming,
        Transfer,
        // The send is over: "Sent", held for a moment before the panel closes.
        Done,
    };

    static constexpr int kWidth = 560;
    static constexpr int kPadding = 14;
    static constexpr double kCardRadius = 24;
    static constexpr double kRowRadius = kCardRadius - kPadding;
    static constexpr int kHeaderHeight = 44;
    static constexpr int kRowHeight = 46;
    static constexpr int kBodyGap = 8;
    static constexpr int kIconSize = 22;
    static constexpr int kMaxRows = 6;
    static constexpr int kButtonsHeight = 36;

    explicit RippleView();
    ~RippleView() override;

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    // Files waiting to be sent; zero for the plain "what is nearby" panel.
    void setPending(int count);
    void setDevices(std::vector<RipplePeer> devices);
    void setOffer(RippleOffer offer);
    void setTransfer(RippleTransfer transfer);
    // The end of either direction: "Sent" or "Received", with the sender or
    // where the files went.
    void setDone(const std::string &title, const std::string &subtitle);
    // Why the last thing failed; empty clears it.
    void setMessage(const std::string &message);

    double preferredHeight() const;
    int currentIndex() const { return m_currentIndex; }
    const std::string &direction() const { return m_transfer.direction; }
    void select(int index);

    std::function<void(int)> onSend;
    // A row clicked: the same as choosing it and pressing Return.
    std::function<void(int)> onRowActivated;
    std::function<void()> onAccept;
    std::function<void()> onDecline;
    std::function<void()> onCloseRequested;
    // A received file's folder, from the panel's last face.
    std::function<void()> onOpenFolder;
    std::function<void()> onPainted;
    std::function<void()> onHeightChanged;

    bool acceptsFocus() const override { return true; }
    // The Sent/Received face answers a click by opening the folder.
    bool acceptsPointer() const override { return m_mode == Mode::Done; }

protected:
    void pointerPress(const ui::PointerEvent &event) override;
    bool keyPress(const ui::KeyEvent &event) override;
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    class Row;

    bool handleKey(const ui::KeyEvent &event);
    void rebuild();
    int rowCount() const;

    Mode m_mode = Mode::Devices;
    int m_pending = 0;
    int m_currentIndex = 0;
    std::vector<RipplePeer> m_devices;
    RippleOffer m_offer;
    RippleTransfer m_transfer;
    std::string m_message;
    std::string m_doneTitle;
    std::string m_doneSubtitle;

    ui::Label *m_title;
    ui::Label *m_subtitle;
    ui::ListView *m_list;
    ui::Label *m_empty;
    ui::Button *m_accept;
    ui::Button *m_decline;
    ui::ProgressBar *m_progress;
    ui::Label *m_progressText;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_RIPPLE_RIPPLE_VIEW_H_
