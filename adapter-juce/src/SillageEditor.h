// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SillageProcessor.h"
#include "SillageLook.h"

class SillageEditor;

// Les voies : guide, double d'origine, double recalé, consonnes, décalage appliqué, force. La voie « consonnes » et la voie « force » se modifient à la souris.
class SillageLanes : public juce::Component
{
public:
    explicit SillageLanes (SillageProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum Lane { Guide, Dub, Aligned, Consonants, Shift, Strength, NumLanes };
    juce::Rectangle<int> laneBounds (Lane) const;
    Lane laneAt (juce::Point<int>) const;
    double timeAt (int x) const;
    int xAt (double t) const;
    void drawPeaks (juce::Graphics&, juce::Rectangle<int>, const std::vector<float>&, juce::Colour);

    SillageProcessor& proc_;
    std::vector<align::StrengthPoint> curve_;
    std::vector<align::Segment> cons_;
    int dragPoint_ = -1, dragSegment_ = -1;
    bool creating_ = false, moved_ = false, consLocal_ = false;
    const void* lastDisplay_ = nullptr;
    double createStart_ = 0, createEnd_ = 0;
    double seconds_ = 0;
};

class SillageEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SillageEditor (SillageProcessor&);
    ~SillageEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    SillageProcessor& proc_;
    SillageLook look_;
    juce::TextButton capture_ { "Capturer" }, finish_ { "Terminer" }, clear_ { "Tout effacer" }, export_ { "Exporter le double recalé (WAV)..."_fr }, clearCurve_ { "Effacer la courbe de force" }, suggest_ { "Suggérer la force"_fr };
    juce::ToggleButton aligned_ { "Recalé"_fr };
    juce::ComboBox engine_, maxShift_, capLen_, suggestSens_, analysis_;
    juce::TextEditor voiceName_;
    juce::Slider strength_, protect_, tightness_;
    juce::Label analysisL_, voiceL_, suggestL_, engineL_, maxShiftL_, capLenL_, strengthL_, protectL_, tightnessL_, status_;
    SillageLanes lanes_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineA_, maxShiftA_, capLenA_, suggestA_, analysisA_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> strengthA_, protectA_, tightnessA_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> alignedA_;
};
