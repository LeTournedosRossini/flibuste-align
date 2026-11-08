#pragma once
// Habillage NOVA de Sillage : couleurs des jetons du thème de la suite (dark, bois, clair, haut contraste) et réglage de contraste, lus dans
// settings.json de Neptune. Seule la couleur et la forme des composants viennent de NOVA ; les composants sont ceux de JUCE.
#include <juce_gui_basics/juce_gui_basics.h>
#include "neptune/core/Theme.h"

struct SillageTheme
{
    juce::Colour back, panel, lane, laneAlt, border, text, dim, accent, accent2, pink, guide, dub, aligned, cons, curve;
    int cornerRadius = 8, borderWidth = 1;
    bool highContrast = false;
};

namespace sillage_look
{
juce::Colour colourOf (const neptune::core::theme::Palette&, neptune::core::theme::Token, float alpha = 1.f);
SillageTheme loadSuiteTheme();   // lit settings.json de Neptune ; thème sombre, contraste 1 par défaut
} // namespace sillage_look

class SillageLook : public juce::LookAndFeel_V4
{
public:
    explicit SillageLook (const SillageTheme&);
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    const SillageTheme& theme() const { return t_; }

private:
    SillageTheme t_;
};
