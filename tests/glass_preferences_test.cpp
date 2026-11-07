// Copyright 2026 Anatolij <anatolijphp@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <optional>
#include <string>

#include "base/settings.h"
#include "base/signal.h"
#include "loop_test_util.h"
#include "render/glass.h"
#include "shell/glass_preferences.h"
#include "shell/shell_data.h"
#include "temporary_directory.h"
#include "ui/theme.h"

using namespace cutefish;
using namespace cutefish::shell;

namespace {

class GlassPreferencesTest : public ::testing::Test
{
protected:
    std::string path() const { return m_directory.filePath("shell.conf"); }

    void write(double tint, double blur, std::optional<bool> frosted = std::nullopt)
    {
        base::Settings settings(path());
        settings.setValue("Appearance/GlassTint", tint);
        settings.setValue("Appearance/GlassBlur", blur);
        if (frosted)
            settings.setValue("Appearance/GlassFrosted", *frosted);
        ASSERT_TRUE(settings.sync());
    }

private:
    test::TemporaryDirectory m_directory;
};

// A colour component as the materials carry it: 16 bits, then wl_fixed.
float component(double value)
{
    return glassFixed(std::round(value * 65535.0) / 65535.0);
}

} // namespace

TEST_F(GlassPreferencesTest, TheDefaultsStandWithoutAFile)
{
    const GlassPreferences preferences(path());
    EXPECT_EQ(preferences.tint(), 0.5);
    EXPECT_EQ(preferences.blur(), 0.3);
    EXPECT_FALSE(preferences.frosted());
    EXPECT_EQ(preferences.generation(), 0u);
}

TEST_F(GlassPreferencesTest, ValuesOutOfRangeAreClampedAndJunkIsTheDefault)
{
    write(2.0, -1.0, true);
    const GlassPreferences clamped(path());
    EXPECT_EQ(clamped.tint(), 1.0);
    EXPECT_EQ(clamped.blur(), 0.0);
    EXPECT_TRUE(clamped.frosted());

    {
        base::Settings settings(path());
        settings.setValue("Appearance/GlassTint", "nan");
        settings.setValue("Appearance/GlassBlur", "soft");
        settings.setValue("Appearance/GlassFrosted", "sometimes");
        ASSERT_TRUE(settings.sync());
    }
    const GlassPreferences junk(path());
    EXPECT_EQ(junk.tint(), 0.5);
    EXPECT_EQ(junk.blur(), 0.3);
    EXPECT_FALSE(junk.frosted());
}

TEST_F(GlassPreferencesTest, AChangeInTheFileIsPickedUpOnce)
{
    GlassPreferences preferences(path());
    int changes = 0;
    const base::Subscription subscription = preferences.changed.connect([&changes] { ++changes; });

    write(0.2, 0.9, true);
    ASSERT_TRUE(test::runUntil([&] { return preferences.generation() == 1; }));
    EXPECT_EQ(preferences.tint(), 0.2);
    EXPECT_EQ(preferences.blur(), 0.9);
    EXPECT_TRUE(preferences.frosted());
    EXPECT_EQ(changes, 1);

    // The same values written again change nothing.
    write(0.2, 0.9, true);
    test::runFor(std::chrono::milliseconds(200));
    EXPECT_EQ(preferences.generation(), 1u);
    EXPECT_EQ(changes, 1);

    // One of them changed counts.
    write(0.2, 0.9, false);
    ASSERT_TRUE(test::runUntil([&] { return preferences.generation() == 2; }));
    EXPECT_FALSE(preferences.frosted());
    EXPECT_EQ(changes, 2);
}

TEST(TintedGlass, AtTheDefaultsItIsTheDockAsItWas)
{
    for (const bool dark : {false, true}) {
        const render::GlassParams glass = tintedGlass(0.5, 0.3, dark);
        EXPECT_FLOAT_EQ(glass.blurSigma, glassFixed(1.0));
        EXPECT_FLOAT_EQ(glass.displacement, glassFixed(36.0));
        EXPECT_FLOAT_EQ(glass.saturation, glassFixed(1.3));
        EXPECT_FLOAT_EQ(glass.rim, glassFixed(1.0));
        EXPECT_FLOAT_EQ(glass.chroma, 0.0f);
        const double alpha = dark ? 0.29 : 0.25;
        EXPECT_FLOAT_EQ(glass.tintAlpha, component(alpha));
        if (dark) {
            // The theme's own grey.
            const ui::Color background = ui::Theme::current().background;
            EXPECT_FLOAT_EQ(glass.tintRed, component(background.r / 255.0));
            EXPECT_FLOAT_EQ(glass.tintGreen, component(background.g / 255.0));
            EXPECT_FLOAT_EQ(glass.tintBlue, component(background.b / 255.0));
        } else {
            EXPECT_FLOAT_EQ(glass.tintRed, component(0.150 / alpha));
            EXPECT_FLOAT_EQ(glass.tintGreen, glass.tintRed);
            EXPECT_FLOAT_EQ(glass.tintBlue, glass.tintRed);
        }
    }
}

TEST(TintedGlass, TheTintSliderFadesTheLensAndThickensTheTint)
{
    const render::GlassParams clear = tintedGlass(0.0, 0.3, false);
    const render::GlassParams tinted = tintedGlass(1.0, 0.3, false);
    EXPECT_FLOAT_EQ(clear.displacement, glassFixed(36.0));
    EXPECT_FLOAT_EQ(tinted.displacement, 0.0f);
    EXPECT_FLOAT_EQ(clear.blurSigma, glassFixed(0.6));
    EXPECT_FLOAT_EQ(tinted.blurSigma, glassFixed(8.0));
    EXPECT_LT(clear.tintAlpha, tinted.tintAlpha);
    // More Tinted is as opaque as the frosted panels.
    EXPECT_FLOAT_EQ(tinted.tintAlpha, component(kFrostedGlassAlpha));
    EXPECT_FLOAT_EQ(tintedGlass(1.0, 0.3, true).tintAlpha, component(kFrostedGlassAlpha));
    EXPECT_FLOAT_EQ(tinted.tintRed, component(1.0));
    // The default is untouched by the raised ceiling.
    EXPECT_FLOAT_EQ(tintedGlass(0.5, 0.3, false).tintAlpha, component(0.25));
}

TEST(TintedGlass, TheBlurSliderScalesTheBlurUpToTheKernel)
{
    EXPECT_DOUBLE_EQ(glassBlurFactor(0.3), 1.0);
    EXPECT_NEAR(glassBlurFactor(0.0), 0.1, 1e-9);
    EXPECT_NEAR(glassBlurFactor(1.0), 10.0, 1e-9);
    EXPECT_DOUBLE_EQ(glassBlurFactor(std::nan("")), 1.0);

    float last = -1;
    for (double blur = 0; blur <= 1.0001; blur += 0.1) {
        const float sigma = tintedGlass(0.5, blur, false).blurSigma;
        EXPECT_GE(sigma, last);
        last = sigma;
    }
    EXPECT_FLOAT_EQ(tintedGlass(0.5, 1.0, false).blurSigma, glassFixed(kMaxGlassBlurSigma));
    EXPECT_FLOAT_EQ(tintedGlass(1.0, 1.0, false).blurSigma, glassFixed(kMaxGlassBlurSigma));
    // Only the blur moves.
    const render::GlassParams sharp = tintedGlass(0.5, 0.0, true);
    const render::GlassParams soft = tintedGlass(0.5, 1.0, true);
    EXPECT_FLOAT_EQ(sharp.displacement, soft.displacement);
    EXPECT_FLOAT_EQ(sharp.tintAlpha, soft.tintAlpha);
    EXPECT_FLOAT_EQ(sharp.saturation, soft.saturation);
}

TEST(TintedGlass, FrostedHasNoLensAndTheWidestBlur)
{
    const render::GlassParams frosted = frostedTintedGlass(0.5, true);
    EXPECT_FLOAT_EQ(frosted.displacement, 0.0f);
    EXPECT_FLOAT_EQ(frosted.blurSigma, glassFixed(kMaxGlassBlurSigma));
    EXPECT_FLOAT_EQ(frosted.tintAlpha, tintedGlass(0.5, 0.3, true).tintAlpha);
    EXPECT_FLOAT_EQ(frosted.tintRed, tintedGlass(0.5, 0.3, true).tintRed);
}
