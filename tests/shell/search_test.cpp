// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <string>

#include "shell/search/search_answer.h"

namespace cutefish::shell {
namespace {

TEST(SearchAnswerTest, EvaluatesArithmeticWithPrecedence)
{
    const auto sum = answerFor("2 + 3 * 4");
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ("14", sum->value);

    const auto grouped = answerFor("(2 + 3) * 4");
    ASSERT_TRUE(grouped.has_value());
    EXPECT_EQ("20", grouped->value);

    const auto power = answerFor("2 ^ 3 ^ 2");
    ASSERT_TRUE(power.has_value());
    EXPECT_EQ("512", power->value);
}

TEST(SearchAnswerTest, RejectsInvalidArithmeticAndDeepNesting)
{
    EXPECT_FALSE(answerFor("1 / 0").has_value());
    EXPECT_FALSE(answerFor("1 % 0").has_value());
    EXPECT_FALSE(answerFor("1 + .").has_value());
    EXPECT_FALSE(answerFor(".").has_value());

    const std::string deep = std::string(1000, '(') + "1" + std::string(1000, ')');
    EXPECT_FALSE(answerFor(deep).has_value());
}

TEST(SearchAnswerTest, ConvertsUnits)
{
    const auto miles = answerFor("10 km to miles");
    ASSERT_TRUE(miles.has_value());
    EXPECT_EQ("6.21371 miles", miles->value);

    const auto fahrenheit = answerFor("0 c to f");
    ASSERT_TRUE(fahrenheit.has_value());
    EXPECT_EQ("32 f", fahrenheit->value);

    EXPECT_FALSE(answerFor("10 km to kg").has_value());
}

} // namespace
} // namespace cutefish::shell
