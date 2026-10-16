#include "TR8SLookAndFeel.h"
#include <cmath>

namespace ReplicantTR8S {

TR8SLookAndFeel::TR8SLookAndFeel() {
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0b1218));
    setColour(juce::ComboBox::textColourId, juce::Colour(0xff7ff7ff));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff24485c));
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff101820));
    setColour(juce::PopupMenu::textColourId, juce::Colour(0xffd0f4ff));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff1f5a73));
}

juce::Colour TR8SLookAndFeel::getChannelColour(int ch) {
    static const juce::Colour kColours[11] = {
        juce::Colour(0xffff2b3a), // 0: BD - Crimson Red
        juce::Colour(0xffff7a1a), // 1: SD - Bright Orange
        juce::Colour(0xffffbe1a), // 2: LT - Amber Gold
        juce::Colour(0xffe6e619), // 3: MT - Yellow
        juce::Colour(0xff84e824), // 4: HT - Chartreuse Green
        juce::Colour(0xffc93bff), // 5: RS - Magenta Violet
        juce::Colour(0xffff3ba8), // 6: HC - Hot Pink
        juce::Colour(0xff1ae5ff), // 7: CH - Cyan
        juce::Colour(0xff1a9cff), // 8: OH - Electric Blue
        juce::Colour(0xff38ff9c), // 9: CC - Emerald Aqua
        juce::Colour(0xff52ffda)  // 10: RC - Mint Teal
    };
    return kColours[juce::jlimit(0, 10, ch)];
}

void TR8SLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                       juce::Slider& slider) {
    float radius = static_cast<float>(juce::jmin(width, height)) * 0.42f;
    float cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Colour accentCol = slider.findColour(juce::Slider::rotarySliderFillColourId);
    if (accentCol.isTransparent())
        accentCol = juce::Colour(0xff00e5ff);

    // 1. Outer tick marks
    int numTicks = 11;
    for (int i = 0; i < numTicks; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(numTicks - 1);
        float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle) - juce::MathConstants<float>::halfPi;
        float r1 = radius * 1.06f;
        float r2 = radius * ((i == 0 || i == 5 || i == 10) ? 1.22f : 1.15f);
        g.setColour(t <= sliderPos ? accentCol.withAlpha(0.85f) : juce::Colour(0xff58626c));
        g.drawLine(cx + std::cos(a) * r1, cy + std::sin(a) * r1,
                   cx + std::cos(a) * r2, cy + std::sin(a) * r2,
                   (i == 0 || i == 5 || i == 10) ? 1.5f : 1.0f);
    }

    // 2. Drop shadow
    g.setColour(juce::Colours::black.withAlpha(0.65f));
    g.fillEllipse(cx - radius + 1.5f, cy - radius + 2.5f, radius * 2.0f, radius * 2.0f);

    // 3. Fluted dark rubber skirt
    juce::ColourGradient skirtGrad(juce::Colour(0xff343a42), cx - radius * 0.5f, cy - radius * 0.5f,
                                   juce::Colour(0xff14171b), cx + radius * 0.5f, cy + radius * 0.5f, true);
    g.setGradientFill(skirtGrad);
    g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    g.setColour(juce::Colour(0xff0a0c0e));
    g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.3f);

    // Knurling ridges around skirt
    g.setColour(juce::Colour(0xff454d57).withAlpha(0.45f));
    for (int i = 0; i < 16; ++i) {
        float ka = angle + i * (juce::MathConstants<float>::twoPi / 16.0f);
        g.drawLine(cx + std::cos(ka) * (radius * 0.76f), cy + std::sin(ka) * (radius * 0.76f),
                   cx + std::cos(ka) * (radius * 0.96f), cy + std::sin(ka) * (radius * 0.96f), 1.0f);
    }

    // 4. Inner brushed metallic cap
    float innerR = radius * 0.72f;
    juce::ColourGradient capGrad(juce::Colour(0xff4a525d), cx - innerR, cy - innerR,
                                 juce::Colour(0xff1e2329), cx + innerR, cy + innerR, false);
    g.setGradientFill(capGrad);
    g.fillEllipse(cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);

    g.setColour(accentCol.withAlpha(0.4f));
    g.drawEllipse(cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

    // 5. Crisp white TR-8S pointer line
    juce::Path pointer;
    pointer.addRoundedRectangle(-1.6f, -radius * 0.95f, 3.2f, radius * 0.62f, 1.2f);
    g.setColour(juce::Colours::white);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(cx, cy));
}

void TR8SLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                       const juce::Slider::SliderStyle /*style*/, juce::Slider& slider) {
    float cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    float topY = static_cast<float>(y) + 8.0f;
    float botY = static_cast<float>(y + height) - 8.0f;
    float trackH = std::max(10.0f, botY - topY);

    juce::Colour ledCol = slider.findColour(juce::Slider::trackColourId);
    if (ledCol.isTransparent())
        ledCol = juce::Colour(0xff00ff66);

    // 1. Scale calibration ticks along both sides of the fader slot
    g.setColour(juce::Colour(0xff6c7682));
    for (int i = 0; i <= 10; ++i) {
        float ty = topY + (static_cast<float>(i) / 10.0f) * trackH;
        float tickLen = (i == 0 || i == 5 || i == 10) ? 7.0f : 4.0f;
        g.drawHorizontalLine(static_cast<int>(ty), cx - 5.0f - tickLen, cx - 5.0f);
        g.drawHorizontalLine(static_cast<int>(ty), cx + 5.0f, cx + 5.0f + tickLen);
    }

    // 2. Recessed dark slot
    juce::Rectangle<float> slotRect(cx - 3.0f, topY, 6.0f, trackH);
    g.setColour(juce::Colour(0xff080a0d));
    g.fillRoundedRectangle(slotRect, 2.5f);
    g.setColour(juce::Colour(0xff252b33));
    g.drawRoundedRectangle(slotRect, 2.5f, 1.0f);

    // 3. Illuminated LED bar from bottom to fader cap
    float clampedPos = juce::jlimit(topY, botY, sliderPos);
    float fillH = std::max(0.0f, botY - clampedPos);
    if (fillH > 0.5f) {
        juce::Rectangle<float> fillRect(cx - 2.0f, clampedPos, 4.0f, fillH);
        g.setColour(ledCol.withAlpha(0.32f));
        g.fillRoundedRectangle(fillRect.expanded(2.5f, 1.0f), 3.0f);
        g.setColour(ledCol);
        g.fillRoundedRectangle(fillRect, 1.5f);
    }

    // 4. Translucent illuminated TR-8S fader cap
    float capW = std::min(static_cast<float>(width) - 6.0f, 26.0f);
    float capH = 14.0f;
    juce::Rectangle<float> capRect(cx - capW * 0.5f, clampedPos - capH * 0.5f, capW, capH);

    g.setColour(juce::Colours::black.withAlpha(0.65f));
    g.fillRoundedRectangle(capRect.translated(0.0f, 2.0f), 2.5f);

    juce::ColourGradient capGrad(juce::Colour(0xff2b3138), capRect.getX(), capRect.getY(),
                                 juce::Colour(0xff14181d), capRect.getX(), capRect.getBottom(), false);
    g.setGradientFill(capGrad);
    g.fillRoundedRectangle(capRect, 2.5f);

    g.setColour(ledCol.withAlpha(0.85f));
    g.drawRoundedRectangle(capRect, 2.5f, 1.2f);

    // Glowing center line on fader cap
    g.setColour(juce::Colours::white);
    g.fillRect(capRect.getX() + 2.5f, clampedPos - 1.0f, capRect.getWidth() - 5.0f, 2.0f);
}

void TR8SLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                   int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                                   juce::ComboBox& box) {
    juce::Rectangle<float> r(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(r, 3.0f, 1.2f);

    // Small down arrow
    juce::Path arrow;
    float ax = static_cast<float>(width) - 12.0f;
    float ay = static_cast<float>(height) * 0.5f;
    arrow.addTriangle(ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(box.findColour(juce::ComboBox::textColourId));
    g.fillPath(arrow);
}

juce::Font TR8SLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    float sz = juce::jlimit(9.5f, 13.5f, static_cast<float>(buttonHeight) * 0.44f);
    return juce::Font(juce::FontOptions(sz, juce::Font::bold));
}

static void drawDigit7Seg(juce::Graphics& g, float x, float y, float w, float h, int digit, juce::Colour onCol, juce::Colour offCol) {
    // Segments: 0=Top, 1=TopRight, 2=BotRight, 3=Bot, 4=BotLeft, 5=TopLeft, 6=Mid
    static const uint8_t segMask[10] = {
        0b0111111, // 0
        0b0000110, // 1
        0b1011011, // 2
        0b1001111, // 3
        0b1100110, // 4
        0b1101101, // 5
        0b1111101, // 6
        0b0000111, // 7
        0b1111111, // 8
        0b1101111  // 9
    };
    uint8_t m = (digit >= 0 && digit <= 9) ? segMask[digit] : 0;
    float t = std::max(2.0f, w * 0.16f);
    float halfH = h * 0.5f;

    auto drawSeg = [&](float sx, float sy, float sw, float sh, bool lit) {
        g.setColour(lit ? onCol : offCol);
        g.fillRoundedRectangle(sx, sy, sw, sh, 1.0f);
    };

    drawSeg(x + t, y, w - 2 * t, t, (m & (1 << 0)) != 0);                   // Top
    drawSeg(x + w - t, y + t, t, halfH - t * 1.2f, (m & (1 << 1)) != 0);    // TopRight
    drawSeg(x + w - t, y + halfH + t * 0.2f, t, halfH - t * 1.2f, (m & (1 << 2)) != 0); // BotRight
    drawSeg(x + t, y + h - t, w - 2 * t, t, (m & (1 << 3)) != 0);           // Bot
    drawSeg(x, y + halfH + t * 0.2f, t, halfH - t * 1.2f, (m & (1 << 4)) != 0); // BotLeft
    drawSeg(x, y + t, t, halfH - t * 1.2f, (m & (1 << 5)) != 0);            // TopLeft
    drawSeg(x + t, y + halfH - t * 0.5f, w - 2 * t, t, (m & (1 << 6)) != 0);// Mid
}

void TR8SLookAndFeel::drawSevenSegmentNumber(juce::Graphics& g, juce::Rectangle<float> bounds,
                                             double value, juce::Colour ledColour) {
    // Deep recessed crimson acrylic screen
    g.setColour(juce::Colour(0xff0c0406));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(juce::Colour(0xff3a141c));
    g.drawRoundedRectangle(bounds, 4.0f, 1.3f);

    int tenths = static_cast<int>(std::round(juce::jlimit(40.0, 300.0, value) * 10.0));
    int dHundreds = (tenths / 1000) % 10;
    int dTens     = (tenths / 100) % 10;
    int dOnes     = (tenths / 10) % 10;
    int dFrac     = tenths % 10;

    float padX = 7.0f;
    float padY = 5.0f;
    float availW = bounds.getWidth() - padX * 2.0f;
    float digitH = bounds.getHeight() - padY * 2.0f;
    float digitW = availW * 0.21f;
    float gap = availW * 0.045f;

    juce::Colour offCol = ledColour.withAlpha(0.11f);
    float curX = bounds.getX() + padX;
    float topY = bounds.getY() + padY;

    drawDigit7Seg(g, curX, topY, digitW, digitH, dHundreds > 0 ? dHundreds : -1, ledColour, offCol);
    curX += digitW + gap;
    drawDigit7Seg(g, curX, topY, digitW, digitH, dTens, ledColour, offCol);
    curX += digitW + gap;
    drawDigit7Seg(g, curX, topY, digitW, digitH, dOnes, ledColour, offCol);

    // Decimal dot
    g.setColour(ledColour);
    g.fillEllipse(curX + digitW + gap * 0.2f, topY + digitH - 3.5f, 3.2f, 3.2f);

    curX += digitW + gap * 1.4f;
    drawDigit7Seg(g, curX, topY + digitH * 0.12f, digitW * 0.85f, digitH * 0.88f, dFrac, ledColour, offCol);
}

} // namespace ReplicantTR8S
