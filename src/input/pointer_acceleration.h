// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_INPUT_POINTER_ACCELERATION_H_
#define CUTEFISH_INPUT_POINTER_ACCELERATION_H_

#include <vector>

namespace cutefish::input {

// Logical pixels per inch of mouse travel at a hand speed in inches per second.
// `speed` is the settings slider, -1 to 1, with a step per table entry; 0 is the default.
double pointerGain(double inchesPerSecond, double speed);

// Counts per millisecond in, pixels per millisecond out; the mouse's resolution is folded in.
struct AccelerationPoints {
    double step = 0;
    std::vector<double> points;
};

AccelerationPoints accelerationPoints(int dotsPerInch, double speed);

// The default resolution from udev's MOUSE_DPI ("1000@125", or a list with
// the default starred, "400 *800 1600@125"); libinput's 1000 when absent.
int mouseDpi(const char *property);

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_POINTER_ACCELERATION_H_
