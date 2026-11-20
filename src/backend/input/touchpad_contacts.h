// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_BACKEND_INPUT_TOUCHPAD_CONTACTS_H_
#define CUTEFISH_BACKEND_INPUT_TOUCHPAD_CONTACTS_H_

#include "base/geometry.h"
#include "input/touchpad_click.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace cutefish::backend {

// libinput does not hand out a touchpad's contacts, so the kernel's multitouch
// slots are queried on the fd libinput has open.
class TouchpadContacts
{
public:
    // nullptr when the device has no multitouch slots or no resolution.
    static std::unique_ptr<TouchpadContacts> create(int fd);

    void update(input::Timestamp now);

    // The fingers down now, palms left out; empty if the device is revoked.
    std::vector<input::TouchpadContact> contacts() const;
    // Whether the pad itself is pressed down, which a tap is not.
    bool clicked() const;

    base::SizeF size() const { return m_size; }
    int fd() const { return m_fd; }

private:
    explicit TouchpadContacts(int fd) : m_fd(fd) { }

    bool readSlots(uint32_t code, std::vector<int32_t> *values) const;
    base::PointF millimetres(int32_t x, int32_t y) const;

    int m_fd = -1;
    int m_slotCount = 0;
    int32_t m_minX = 0;
    int32_t m_minY = 0;
    double m_resolutionX = 0;
    double m_resolutionY = 0;
    bool m_hasToolType = false;
    base::SizeF m_size;

    struct Slot {
        int32_t trackingId = -1;
        base::PointF origin;
        input::Timestamp downTime;
    };
    std::vector<Slot> m_slots;
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_INPUT_TOUCHPAD_CONTACTS_H_
