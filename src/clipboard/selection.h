// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/{data_source,data_offer,data_device,seat}.{h,cc}
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "base/signal.h"
#include "base/weak_ptr.h"

namespace cutefish::clipboard {

// send() always consumes the supplied fd.
class DataSource
{
public:
    virtual ~DataSource() = default;

    bool offer(const std::string &mime);

    const std::vector<std::string> &formats() const { return m_formats; }

    bool supports(const std::string &mime) const;

    bool alive() const { return m_alive; }

    bool used() const { return m_used; }

    void markUsed() { m_used = true; }

    void invalidate();
    void cancel();
    virtual void send(const std::string &mime, int fd) = 0;

    virtual void target(const char *mime) { }

    virtual void action(uint32_t action) { }

    virtual void dropPerformed() { }

    virtual void finished() { }

    base::Signal<> invalidated;

protected:
    virtual void cancelled() { }

private:
    std::vector<std::string> m_formats;
    bool m_alive = true;
    bool m_used = false;
    base::WeakPtrFactory<DataSource> m_weakFactory {this};
};

class SelectionController
{
public:
    SelectionController() = default;
    SelectionController(const SelectionController &) = delete;
    SelectionController &operator=(const SelectionController &) = delete;
    ~SelectionController();
    using Recipient = const void *; // Adapter-owned identity; never dereferenced.

    std::shared_ptr<DataSource> source() const { return m_source; }

    uint64_t generation() const { return m_generation; }

    uint64_t focusGeneration() const { return m_focusGeneration; }

    Recipient recipient() const { return m_recipient; }

    // While isolated, nothing is offered and no source may be set, in either direction.
    void setIsolated(bool isolated);

    bool isIsolated() const { return m_isolated; }

    void setRecipient(Recipient recipient);
    // Records only actual delivered keyboard-enter/key or pointer-button events.
    void recordSerial(Recipient recipient, uint32_t serial);
    bool authorize(Recipient recipient, uint32_t serial);
    // `serial` need only be newer than the last one `selection` accepted.
    bool authorize(Recipient recipient, uint32_t serial, SelectionController &selection);
    bool setSource(std::shared_ptr<DataSource> source);
    // Expected-source check protects a replacement from old teardown callbacks.
    void sourceDestroyed(DataSource *source);

    base::WeakPtr<SelectionController> weakPtr() { return m_weakFactory.getWeakPtr(); }

    base::Signal<> selectionChanged;
    base::Signal<> recipientChanged;
    // At the end of destruction, once the selection is gone.
    base::Signal<> destroyed;

private:
    bool m_destroying = false;
    bool m_isolated = false;
    std::shared_ptr<DataSource> m_source;
    std::shared_ptr<DataSource> m_savedSource;
    Recipient m_savedRecipient = nullptr;
    Recipient m_recipient = nullptr;
    uint64_t m_generation = 0;
    uint64_t m_focusGeneration = 0;
    uint64_t m_sequence = 0;
    uint64_t m_lastAccepted = 0;

    struct Serial {
        uint32_t value;
        uint64_t sequence;
    };

    std::deque<Serial> m_serials;
    base::Subscription m_sourceConnection;
    base::WeakPtrFactory<SelectionController> m_weakFactory {this};
};

// Losing eligibility invalidates future receives, even if the recipient regains focus.
class DataOffer
{
public:
    explicit DataOffer(SelectionController *controller);
    DataOffer(const DataOffer &) = delete;
    DataOffer &operator=(const DataOffer &) = delete;
    bool valid() const;
    void invalidate();

    const std::vector<std::string> &formats() const { return m_formats; }

    void receive(const std::string &mime, int fd);

private:
    bool m_eligible = true;
    base::WeakPtr<SelectionController> m_controller;
    std::weak_ptr<DataSource> m_source;
    std::vector<std::string> m_formats;
    uint64_t m_generation;
    uint64_t m_focusGeneration;
    base::Subscription m_selectionChanged;
    base::Subscription m_recipientChanged;
};

} // namespace cutefish::clipboard
