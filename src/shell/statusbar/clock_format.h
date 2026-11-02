// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_STATUSBAR_CLOCK_FORMAT_H_
#define CUTEFISH_SHELL_STATUSBAR_CLOCK_FORMAT_H_

#include <string>

namespace cutefish::shell::clock_format {

// The status bar clock's format, as base::Locale::toString takes it: the
// weekday, abbreviated month, day and time in the order and words CLDR gives
// the locale (an ICU id, "zh_Hant_SG"), without the commas of its
// written-out form ("Sat Sep 26 1:40 PM").
std::string pattern(const std::string &localeName, bool twentyFourHour);

// From a CLDR (ICU) pattern; empty when it has a field that format cannot show.
std::string toLocaleFormat(const std::string &icuPattern);

// A base::Locale::toString format with a seconds field outside its quotes.
bool showsSeconds(const std::string &format);

} // namespace cutefish::shell::clock_format

#endif // CUTEFISH_SHELL_STATUSBAR_CLOCK_FORMAT_H_
