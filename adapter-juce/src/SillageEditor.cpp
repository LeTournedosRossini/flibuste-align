// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SillageEditor.h"

namespace
{
juce::Colour kBack, kLane, kLaneAlt, kText, kDim, kGuide, kDub, kAligned, kCons, kCurve;
void applyTheme (const SillageTheme& t)
{
    kBack = t.back; kLane = t.lane; kLaneAlt = t.laneAlt; kText = t.text; kDim = t.dim; kGuide = t.guide; kDub = t.dub; kAligned = t.aligned; kCons = t.cons; kCurve = t.curve;
}
const juce::String kLaneNames[] = { "Guide", "Double (original)", "Double recalé"_fr, "Consonnes", "Décalage appliqué"_fr, "Force" };
constexpr int kLabelW = 120;
} // namespace

// ------------------------------------------------------------------------------------------------------------ les voies
SillageLanes::SillageLanes (SillageProcessor& p) : proc_ (p) { setWantsKeyboardFocus (false); }

juce::Rectangle<int> SillageLanes::laneBounds (Lane l) const
{
    const int n = (int) NumLanes;
    const int h = getHeight() / n;
    return { kLabelW, (int) l * h, getWidth() - kLabelW, h };
}

SillageLanes::Lane SillageLanes::laneAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) NumLanes; ++i)
        if (laneBounds ((Lane) i).contains (p)) return (Lane) i;
    return NumLanes;
}

double SillageLanes::timeAt (int x) const
{
    const auto b = laneBounds (Guide);
    return seconds_ * std::clamp ((double) (x - b.getX()) / std::max (1, b.getWidth()), 0.0, 1.0);
}

int SillageLanes::xAt (double t) const
{
    const auto b = laneBounds (Guide);
    return b.getX() + (seconds_ > 0 ? (int) std::lround (t / seconds_ * b.getWidth()) : 0);
}

void SillageLanes::drawPeaks (juce::Graphics& g, juce::Rectangle<int> r, const std::vector<float>& peaks, juce::Colour c)
{
    if (peaks.empty()) return;
    g.setColour (c);
    const int N = (int) peaks.size() / 2;
    const float mid = (float) r.getCentreY(), half = (float) r.getHeight() * 0.45f;
    for (int x = 0; x < r.getWidth(); ++x)
    {
        const int b = std::min (N - 1, x * N / std::max (1, r.getWidth()));
        const float lo = peaks[(std::size_t) b * 2], hi = peaks[(std::size_t) b * 2 + 1];
        g.drawVerticalLine (r.getX() + x, mid - hi * half, mid - lo * half + 1.f);
    }
}

void SillageLanes::paint (juce::Graphics& g)
{
    const auto d = proc_.display();
    seconds_ = d ? d->seconds : 0.0;
    if (dragPoint_ < 0) curve_ = proc_.strengthCurve();
    if (d.get() != lastDisplay_) { lastDisplay_ = d.get(); if (! creating_ && dragSegment_ < 0) consLocal_ = false; } // nouveau rendu : on revient aux consonnes du plugin
    g.fillAll (kBack);
    for (int i = 0; i < (int) NumLanes; ++i)
    {
        const auto b = laneBounds ((Lane) i);
        g.setColour ((i & 1) ? kLaneAlt : kLane);
        g.fillRect (b);
        g.setColour (kDim);
        g.setFont (13.f);
        g.drawText (kLaneNames[i], 8, b.getY(), kLabelW - 12, b.getHeight(), juce::Justification::centredLeft);
    }
    if (! d)
    {
        g.setColour (kDim);
        g.setFont (15.f);
        g.drawText ("Aucune capture.", getLocalBounds().withTrimmedLeft (kLabelW), juce::Justification::centred);
        return;
    }
    drawPeaks (g, laneBounds (Guide), d->guidePeaks, kGuide);
    drawPeaks (g, laneBounds (Dub), d->dubPeaks, kDub);
    drawPeaks (g, laneBounds (Aligned), d->alignedPeaks, kAligned);

    // Consonnes : sur leur voie, et en filigrane sur le double d'origine.
    const auto cons = consLocal_ ? cons_ : d->consonants;
    {
        const auto cb = laneBounds (Consonants), db = laneBounds (Dub);
        for (const auto& s : cons)
        {
            const int x0 = xAt (s.start), x1 = std::max (x0 + 2, xAt (s.end));
            g.setColour (kCons.withAlpha (0.55f));
            g.fillRect (x0, cb.getY() + 3, x1 - x0, cb.getHeight() - 6);
            g.setColour (kCons.withAlpha (0.18f));
            g.fillRect (x0, db.getY(), x1 - x0, db.getHeight());
        }
        if (creating_)
        {
            g.setColour (kCons.withAlpha (0.35f));
            g.fillRect (xAt (std::min (createStart_, createEnd_)), cb.getY() + 3, std::abs (xAt (createEnd_) - xAt (createStart_)), cb.getHeight() - 6);
        }
    }

    // Décalage appliqué.
    {
        const auto b = laneBounds (Shift).reduced (0, 4);
        float peak = 50.f;
        for (float v : d->shiftMs) peak = std::max (peak, std::fabs (v));
        peak = std::ceil (peak / 50.f) * 50.f;
        g.setColour (kDim.withAlpha (0.4f));
        g.drawHorizontalLine (b.getCentreY(), (float) b.getX(), (float) b.getRight());
        g.setFont (11.f);
        g.drawText (juce::String ((int) peak) + " ms", b.getX() + 4, b.getY(), 70, 14, juce::Justification::left);
        juce::Path p;
        for (std::size_t i = 0; i < d->shiftMs.size(); ++i)
        {
            const float x = (float) xAt ((double) i * 0.005), y = (float) b.getCentreY() - d->shiftMs[i] / peak * (float) b.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (kAligned);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    // Force : courbe (points modifiables), ou ligne du réglage constant.
    {
        const auto b = laneBounds (Strength).reduced (0, 6);
        auto yOf = [&] (double s) { return (float) b.getBottom() - (float) s * (float) b.getHeight(); };
        const double constant = *proc_.parameters().getRawParameterValue ("strength") / 100.0;
        g.setColour (kDim.withAlpha (0.3f));
        g.drawHorizontalLine ((int) yOf (1.0), (float) b.getX(), (float) b.getRight());
        g.drawHorizontalLine ((int) yOf (0.0), (float) b.getX(), (float) b.getRight());
        g.setFont (11.f);
        g.drawText ("100 %", b.getX() + 4, (int) yOf (1.0), 50, 12, juce::Justification::left);
        if (curve_.empty())
        {
            g.setColour (kCurve.withAlpha (0.6f));
            const float dash[] = { 5.f, 4.f };
            g.drawDashedLine (juce::Line<float> ((float) b.getX(), yOf (constant), (float) b.getRight(), yOf (constant)), dash, 2, 1.5f);
            g.setColour (kDim);
            g.drawText ("cliquer pour poser des points (force variable)", b.reduced (8, 0), juce::Justification::centredRight);
        }
        else
        {
            juce::Path p;
            p.startNewSubPath ((float) b.getX(), yOf (curve_.front().strength));
            for (const auto& pt : curve_) p.lineTo ((float) xAt (pt.time), yOf (pt.strength));
            p.lineTo ((float) b.getRight(), yOf (curve_.back().strength));
            g.setColour (kCurve);
            g.strokePath (p, juce::PathStrokeType (2.f));
            for (const auto& pt : curve_) g.fillEllipse ((float) xAt (pt.time) - 4.f, yOf (pt.strength) - 4.f, 8.f, 8.f);
        }
    }

    // Tête de lecture.
    bool valid = false;
    const double t = proc_.playheadSeconds (valid);
    if (valid && t <= d->seconds)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawVerticalLine (xAt (t), 0.f, (float) getHeight());
    }
}

void SillageLanes::mouseDown (const juce::MouseEvent& e)
{
    moved_ = false;
    dragPoint_ = dragSegment_ = -1;
    creating_ = false;
    const auto lane = laneAt (e.getPosition());
    const auto d = proc_.display();
    if (! d) return;
    if (lane == Strength)
    {
        if (curve_.empty()) curve_ = proc_.strengthCurve();
        const auto b = laneBounds (Strength).reduced (0, 6);
        auto yOf = [&] (double s) { return (float) b.getBottom() - (float) s * (float) b.getHeight(); };
        for (int i = 0; i < (int) curve_.size(); ++i)
            if (std::abs (xAt (curve_[(std::size_t) i].time) - e.x) < 8 && std::abs ((int) yOf (curve_[(std::size_t) i].strength) - e.y) < 8) dragPoint_ = i;
        if (e.mods.isRightButtonDown() && dragPoint_ >= 0)
        {
            curve_.erase (curve_.begin() + dragPoint_);
            dragPoint_ = -1;
            proc_.setStrengthCurve (curve_);
        }
        else if (dragPoint_ < 0)
        {
            const double s = std::clamp (1.0 - (double) (e.y - b.getY()) / std::max (1, b.getHeight()), 0.0, 1.0);
            if (curve_.empty()) // premier point : la courbe part du réglage constant
            {
                const double c = *proc_.parameters().getRawParameterValue ("strength") / 100.0;
                curve_.push_back ({ 0.0, c });
                curve_.push_back ({ d->seconds, c });
            }
            curve_.push_back ({ timeAt (e.x), s });
            std::stable_sort (curve_.begin(), curve_.end(), [] (const auto& a, const auto& b2) { return a.time < b2.time; });
            for (int i = 0; i < (int) curve_.size(); ++i) if (std::abs (curve_[(std::size_t) i].time - timeAt (e.x)) < 1e-9) dragPoint_ = i;
            proc_.setStrengthCurve (curve_);
        }
        repaint();
    }
    else if (lane == Consonants)
    {
        if (! consLocal_) { cons_ = proc_.consonants(); consLocal_ = true; }
        const double t = timeAt (e.x);
        for (int i = 0; i < (int) cons_.size(); ++i)
            if (t >= cons_[(std::size_t) i].start && t <= cons_[(std::size_t) i].end) dragSegment_ = i;
        if (dragSegment_ < 0) { creating_ = true; createStart_ = createEnd_ = t; }
        repaint();
    }
}

void SillageLanes::mouseDrag (const juce::MouseEvent& e)
{
    moved_ = true;
    if (dragPoint_ >= 0 && (std::size_t) dragPoint_ < curve_.size())
    {
        const auto b = laneBounds (Strength).reduced (0, 6);
        auto& pt = curve_[(std::size_t) dragPoint_];
        pt.time = timeAt (e.x);
        pt.strength = std::clamp (1.0 - (double) (e.y - b.getY()) / std::max (1, b.getHeight()), 0.0, 1.0);
        repaint();
    }
    else if (creating_)
    {
        createEnd_ = timeAt (e.x);
        repaint();
    }
}

void SillageLanes::mouseUp (const juce::MouseEvent&)
{
    if (dragPoint_ >= 0)
    {
        std::stable_sort (curve_.begin(), curve_.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });
        proc_.setStrengthCurve (curve_);
    }
    else if (creating_)
    {
        const double a = std::min (createStart_, createEnd_), b = std::max (createStart_, createEnd_);
        if (b - a >= 0.02) { cons_.push_back ({ a, b }); consLocal_ = true; proc_.setConsonants (cons_); }
        creating_ = false;
    }
    else if (dragSegment_ >= 0 && ! moved_) // un clic sur une zone existante la supprime
    {
        cons_.erase (cons_.begin() + dragSegment_);
        proc_.setConsonants (cons_);
    }
    dragPoint_ = dragSegment_ = -1;
    repaint();
}

void SillageLanes::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (laneAt (e.getPosition()) != Strength) return;
    for (int i = 0; i < (int) curve_.size(); ++i)
        if (std::abs (xAt (curve_[(std::size_t) i].time) - e.x) < 8)
        {
            curve_.erase (curve_.begin() + i);
            proc_.setStrengthCurve (curve_);
            break;
        }
}

// ---------------------------------------------------------------------------------------------------------- l'éditeur
namespace
{
void setupSlider (juce::Slider& s, juce::Label& l, const juce::String& text, juce::Component& parent)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    s.setTextValueSuffix (" %");
    parent.addAndMakeVisible (s);
    l.setText (text, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, kText);
    parent.addAndMakeVisible (l);
}
} // namespace

SillageEditor::SillageEditor (SillageProcessor& p) : juce::AudioProcessorEditor (p), proc_ (p), look_ (sillage_look::loadSuiteTheme()), lanes_ (p)
{
    applyTheme (look_.theme());
    setLookAndFeel (&look_);
    setSize (980, 720);
    setResizable (true, true);
    setResizeLimits (760, 600, 1800, 1100);
    auto& ap = p.parameters();

    engine_.addItemList ({ "Signalsmith", "Rubber Band R2", "Rubber Band R3" }, 1);
    maxShift_.addItemList ({ "0,5 s", "1 s", "1,5 s", "2 s", "3 s" }, 1);
    capLen_.addItemList ({ "30 s", "1 min", "2 min", "3 min", "5 min" }, 1);
    suggestSens_.addItemList ({ "Haute (30 ms)", "Moyenne (60 ms)", "Basse (100 ms)" }, 1);
    analysis_.addItemList ({ "Même hauteur", "Harmonies" }, 1);
    for (auto* c : { &engine_, &maxShift_, &capLen_, &suggestSens_, &analysis_ }) addAndMakeVisible (*c);
    voiceName_.setText (proc_.voiceName(), false);
    voiceName_.setTextToShowWhenEmpty ("ex. voix 2, tierce"_fr, kDim);
    voiceName_.onTextChange = [this] { proc_.setVoiceName (voiceName_.getText()); };
    addAndMakeVisible (voiceName_);
    const std::pair<juce::Label*, juce::String> labels[] = { { &engineL_, juce::String ("Moteur") }, { &maxShiftL_, "Écart maximal"_fr }, { &capLenL_, "Durée de capture"_fr }, { &suggestL_, "Sensibilité"_fr }, { &analysisL_, "Analyse"_fr }, { &voiceL_, "Nom de la voix"_fr } };
    for (auto& [l, t] : labels) { l->setText (t, juce::dontSendNotification); l->setColour (juce::Label::textColourId, kText); addAndMakeVisible (*l); }
    setupSlider (strength_, strengthL_, "Force", *this);
    setupSlider (protect_, protectL_, "Consonnes protégées"_fr, *this);
    setupSlider (tightness_, tightnessL_, "Serrage", *this);
    addAndMakeVisible (aligned_);
    for (auto* b : { &capture_, &finish_, &clear_, &export_, &suggest_, &clearCurve_ }) addAndMakeVisible (*b);
    status_.setColour (juce::Label::textColourId, kText);
    status_.setFont (juce::Font (juce::FontOptions (15.f)));
    addAndMakeVisible (status_);
    addAndMakeVisible (lanes_);

    engineA_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, "engine", engine_);
    maxShiftA_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, "maxshift", maxShift_);
    capLenA_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, "caplen", capLen_);
    strengthA_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, "strength", strength_);
    protectA_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, "protect", protect_);
    tightnessA_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, "tightness", tightness_);
    suggestA_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, "suggest", suggestSens_);
    analysisA_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, "analysis", analysis_);
    alignedA_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ap, "aligned", aligned_);

    capture_.onClick = [this] { proc_.armCapture(); };
    finish_.onClick = [this] { proc_.finishCapture(); };
    clear_.onClick = [this] { proc_.clearAll(); };
    export_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser> ("Exporter le double recalé"_fr, juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (proc_.suggestedExportName()), "*.wav");
        chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                               [this] (const juce::FileChooser& fc) {
                                   const auto f = fc.getResult();
                                   if (f == juce::File()) return;
                                   juce::String err;
                                   if (! proc_.exportAligned (f, err)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export", err);
                               });
    };
    suggest_.onClick = [this] { proc_.suggestStrength(); };
    clearCurve_.onClick = [this] { proc_.setStrengthCurve ({}); };
    startTimerHz (15);
}

SillageEditor::~SillageEditor() { stopTimer(); setLookAndFeel (nullptr); }

void SillageEditor::paint (juce::Graphics& g) { g.fillAll (kBack); }

void SillageEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (30);
    capture_.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (8);
    finish_.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (8);
    clear_.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (8);
    export_.setBounds (top.removeFromLeft (230));
    top.removeFromLeft (14);
    aligned_.setBounds (top);
    r.removeFromTop (6);
    auto forceRow = r.removeFromTop (28); // la force variable : suggestion, sensibilité, effacement
    suggest_.setBounds (forceRow.removeFromLeft (170));
    forceRow.removeFromLeft (14);
    suggestL_.setBounds (forceRow.removeFromLeft (86));
    suggestSens_.setBounds (forceRow.removeFromLeft (160));
    forceRow.removeFromLeft (14);
    clearCurve_.setBounds (forceRow.removeFromLeft (210));
    r.removeFromTop (6);
    status_.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    auto controls = r.removeFromBottom (124);
    lanes_.setBounds (r);
    controls.removeFromTop (8);
    auto row1 = controls.removeFromTop (30), row2 = controls.removeFromTop (30), row3 = controls.removeFromTop (30);
    auto place = [] (juce::Rectangle<int>& row, juce::Label& l, juce::Component& c, int labelW, int w) {
        l.setBounds (row.removeFromLeft (labelW));
        c.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (14);
    };
    const int third = (r.getWidth() - 28) / 3;
    place (row1, strengthL_, strength_, 56, third - 70);
    place (row1, protectL_, protect_, 130, third - 144);
    place (row1, tightnessL_, tightness_, 56, third - 70);
    place (row2, engineL_, engine_, 56, third - 70);
    place (row2, maxShiftL_, maxShift_, 100, third - 114);
    place (row2, capLenL_, capLen_, 120, third - 134);
    place (row3, analysisL_, analysis_, 56, third - 70);
    place (row3, voiceL_, voiceName_, 100, third - 114);
}

void SillageEditor::timerCallback()
{
    status_.setText (proc_.statusText(), juce::dontSendNotification);
    const auto ph = proc_.phase();
    capture_.setEnabled (ph != SillageProcessor::Phase::Capturing && ph != SillageProcessor::Phase::Computing);
    finish_.setEnabled (ph == SillageProcessor::Phase::Capturing || ph == SillageProcessor::Phase::Armed);
    clear_.setEnabled (ph != SillageProcessor::Phase::Idle);
    export_.setEnabled (proc_.canExport());
    suggest_.setEnabled (proc_.phase() == SillageProcessor::Phase::Ready);
    lanes_.repaint();
}
