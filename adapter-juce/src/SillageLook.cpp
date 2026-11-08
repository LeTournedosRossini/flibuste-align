#include "SillageLook.h"

namespace sillage_look
{
using namespace neptune::core::theme;

juce::Colour colourOf (const Palette& p, Token t, float alpha)
{
    const auto& c = p.color (t);
    return juce::Colour ((juce::uint8) c.r, (juce::uint8) c.g, (juce::uint8) c.b, (float) (c.a * (double) alpha));
}

SillageTheme loadSuiteTheme()
{
    ThemeId id = ThemeId::Dark;
    int contrast = 1;
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    const auto file = dir.getChildFile ("Neptune").getChildFile ("settings.json");
    if (file.existsAsFile())
    {
        const auto doc = juce::JSON::parse (file.loadFileAsString());
        if (doc.isObject())
        {
            if (auto t = themeFromString (doc.getProperty ("theme", "dark").toString().toStdString())) id = *t;
            contrast = juce::jlimit (0, 4, (int) doc.getProperty ("contrast", 1));
        }
    }
    const auto& def = definition (id);
    const auto pal = withContrast (def, contrast);
    SillageTheme t;
    t.back = colourOf (pal, Token::BgApp);
    t.panel = colourOf (pal, Token::PanelBg);
    t.lane = colourOf (pal, Token::BgCard);
    t.laneAlt = t.lane.interpolatedWith (t.back, 0.45f);
    t.border = colourOf (pal, Token::Border);
    t.text = colourOf (pal, Token::Text);
    t.dim = colourOf (pal, Token::TextDim);
    t.accent = colourOf (pal, Token::Accent);
    t.accent2 = colourOf (pal, Token::Accent2);
    t.pink = colourOf (pal, Token::Pink);
    t.guide = colourOf (pal, Token::Band1);
    t.dub = colourOf (pal, Token::Band2);
    t.aligned = t.accent;
    t.cons = t.pink;
    t.curve = colourOf (pal, Token::Band3);
    t.cornerRadius = std::max (4, def.metrics.cornerRadius);
    t.borderWidth = def.metrics.borderWidth;
    t.highContrast = def.control == ControlStyle::HighContrast;
    return t;
}
} // namespace sillage_look

SillageLook::SillageLook (const SillageTheme& t) : t_ (t)
{
    using C = juce::Colour;
    auto set = [this] (int id, C c) { setColour (id, c); };
    set (juce::ResizableWindow::backgroundColourId, t.back);
    set (juce::Label::textColourId, t.text);
    set (juce::Label::outlineColourId, C());
    set (juce::TextButton::textColourOffId, t.text);
    set (juce::TextButton::textColourOnId, t.text);
    set (juce::ToggleButton::textColourId, t.text);
    set (juce::ToggleButton::tickColourId, t.accent);
    set (juce::ComboBox::backgroundColourId, t.lane);
    set (juce::ComboBox::textColourId, t.text);
    set (juce::ComboBox::outlineColourId, t.border);
    set (juce::ComboBox::arrowColourId, t.dim);
    set (juce::PopupMenu::backgroundColourId, t.panel);
    set (juce::PopupMenu::textColourId, t.text);
    set (juce::PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.35f));
    set (juce::PopupMenu::highlightedTextColourId, t.text);
    set (juce::Slider::textBoxTextColourId, t.text);
    set (juce::Slider::textBoxBackgroundColourId, t.lane);
    set (juce::Slider::textBoxOutlineColourId, t.border);
    set (juce::Slider::trackColourId, t.accent);
    set (juce::Slider::backgroundColourId, t.border);
    set (juce::Slider::thumbColourId, t.accent);
    set (juce::AlertWindow::backgroundColourId, t.panel);
    set (juce::AlertWindow::textColourId, t.text);
    set (juce::AlertWindow::outlineColourId, t.border);
}

juce::Font SillageLook::getTextButtonFont (juce::TextButton&, int h) { return juce::Font (juce::FontOptions (juce::jmin (15.f, (float) h * 0.55f))); }
juce::Font SillageLook::getLabelFont (juce::Label& l) { return l.getFont(); }
juce::Font SillageLook::getComboBoxFont (juce::ComboBox&) { return juce::Font (juce::FontOptions (14.f)); }
juce::Font SillageLook::getPopupMenuFont() { return juce::Font (juce::FontOptions (14.f)); }

void SillageLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f; // pastille
    const bool on = b.getToggleState() || down;
    juce::ColourGradient grad (t_.accent.withAlpha (on ? 0.35f : (highlighted ? 0.22f : 0.0f)), r.getX(), r.getY(),
                               t_.pink.withAlpha (on ? 0.22f : (highlighted ? 0.12f : 0.0f)), r.getRight(), r.getBottom(), false);
    g.setColour (t_.lane);
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, radius);
    g.setColour (b.isEnabled() ? (highlighted || on ? t_.accent : t_.border) : t_.border.withAlpha (0.5f));
    g.drawRoundedRectangle (r, radius, (float) t_.borderWidth);
    if (! b.isEnabled()) { g.setColour (t_.back.withAlpha (0.45f)); g.fillRoundedRectangle (r, radius); }
}

void SillageLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const float h = (float) b.getHeight();
    const float box = std::min (h - 8.f, 20.f);
    const juce::Rectangle<float> r (2.f, (h - box) * 0.5f, box * 1.7f, box); // petit interrupteur
    g.setColour (b.getToggleState() ? t_.accent.withAlpha (0.45f) : t_.lane);
    g.fillRoundedRectangle (r, box * 0.5f);
    g.setColour (highlighted || b.getToggleState() ? t_.accent : t_.border);
    g.drawRoundedRectangle (r, box * 0.5f, (float) t_.borderWidth);
    const float cx = b.getToggleState() ? r.getRight() - box * 0.5f : r.getX() + box * 0.5f;
    g.setColour (b.getToggleState() ? t_.text : t_.dim);
    g.fillEllipse (cx - box * 0.32f, r.getCentreY() - box * 0.32f, box * 0.64f, box * 0.64f);
    g.setColour (b.isEnabled() ? t_.text : t_.dim);
    g.setFont (juce::Font (juce::FontOptions (14.f)));
    g.drawText (b.getButtonText(), (int) (r.getRight() + 8.f), 0, b.getWidth() - (int) (r.getRight() + 8.f), b.getHeight(), juce::Justification::centredLeft);
}

void SillageLook::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f, x0 = (float) x + 6.f, x1 = (float) (x + w) - 6.f;
    g.setColour (t_.border);
    g.fillRoundedRectangle (x0, cy - 2.f, x1 - x0, 4.f, 2.f);
    const float px = juce::jlimit (x0, x1, pos);
    juce::ColourGradient grad (t_.accent, x0, cy, t_.pink, x1, cy, false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (x0, cy - 2.f, std::max (0.f, px - x0), 4.f, 2.f);
    g.setColour (t_.lane);
    g.fillEllipse (px - 8.f, cy - 8.f, 16.f, 16.f);
    g.setColour (s.isMouseOverOrDragging() ? t_.pink : t_.accent);
    g.drawEllipse (px - 8.f, cy - 8.f, 16.f, 16.f, 2.f);
}

void SillageLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.5f, 0.5f, (float) w - 1.f, (float) h - 1.f);
    g.setColour (t_.lane);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (box.hasKeyboardFocus (true) || box.isMouseOver() ? t_.accent : t_.border);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, (float) t_.borderWidth);
    juce::Path arrow;
    const float cx = (float) w - 18.f, cy = (float) h * 0.5f;
    arrow.addTriangle (cx - 4.5f, cy - 2.f, cx + 4.5f, cy - 2.f, cx, cy + 3.5f);
    g.setColour (t_.dim);
    g.fillPath (arrow);
}
