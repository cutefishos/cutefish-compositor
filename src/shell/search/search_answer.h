// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SEARCH_SEARCH_ANSWER_H_
#define CUTEFISH_SHELL_SEARCH_SEARCH_ANSWER_H_

#include <optional>
#include <string>

namespace cutefish::shell {

// Inline answer shown beside search results: a calculation or conversion.
struct SearchAnswer
{
    // The answer alone, ready to be copied: "42", "6.21 miles".
    std::string value;
    // What was worked out, as the person asked it.
    std::string expression;
};

// Empty when the words are not a calculation or a conversion.
std::optional<SearchAnswer> answerFor(const std::string &query);

// Asks for exchange rates in the background when the cache is a day old; the
// first currency conversion after that says it is updating.
void refreshCurrencyRatesIfStale();

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SEARCH_SEARCH_ANSWER_H_
