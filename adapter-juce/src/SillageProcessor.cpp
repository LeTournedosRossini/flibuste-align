#include "SillageProcessor.h"
#include "SillageEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
const double kMaxShifts[] = { 0.5, 1.0, 1.5, 2.0, 3.0 };
const double kSuggestMs[] = { 30.0, 60.0, 100.0 };
const double kCaptureSeconds[] = { 30.0, 60.0, 120.0, 180.0, 300.0 };
constexpr std::uint32_t kMagic = 0x31474C53; // « SLG1 »
constexpr int kFade = 220;                  // fondu aux bords de la zone rejouée (5 ms)

void computePeaks (const float* x, std::size_t n, std::vector<float>& out)
{
    const int N = SillageDisplay::kPeaks;
    out.assign ((std::size_t) N * 2, 0.f);
    if (n == 0) return;
    for (int b = 0; b < N; ++b)
    {
        const std::size_t a = (std::size_t) ((double) b * (double) n / N), e = std::max (a + 1, (std::size_t) ((double) (b + 1) * (double) n / N));
        float lo = 0.f, hi = 0.f;
        for (std::size_t i = a; i < e && i < n; ++i) { lo = std::min (lo, x[i]); hi = std::max (hi, x[i]); }
        out[(std::size_t) b * 2] = lo;
        out[(std::size_t) b * 2 + 1] = hi;
    }
}

std::vector<float> monoOf (const std::vector<std::vector<float>>& ch, std::size_t n)
{
    std::vector<float> m (n, 0.f);
    for (const auto& c : ch)
        for (std::size_t i = 0; i < n && i < c.size(); ++i) m[i] += c[i] / (float) ch.size();
    return m;
}

juce::MemoryBlock encodeFlac (const std::vector<const float*>& ch, std::size_t n, double sr, int bits)
{
    juce::MemoryBlock mb;
    if (ch.empty() || n == 0) return mb;
    auto* os = new juce::MemoryOutputStream (mb, false);
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (os, sr, (unsigned) ch.size(), bits, {}, 5));
    if (! w) { delete os; return {}; }
    w->writeFromFloatArrays (ch.data(), (int) ch.size(), (int) n);
    w.reset(); // vide et ferme (le flux appartient à l'écrivain)
    return mb;
}

bool decodeFlac (const void* data, std::size_t size, std::vector<std::vector<float>>& out)
{
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (data, size, false), true));
    if (! r || r->lengthInSamples <= 0 || r->numChannels == 0) return false;
    const int n = (int) r->lengthInSamples;
    out.assign (r->numChannels, std::vector<float> ((std::size_t) n));
    std::vector<float*> p;
    for (auto& c : out) p.push_back (c.data());
    return r->read (p.data(), (int) p.size(), 0, n);
}
} // namespace

juce::AudioProcessor::BusesProperties SillageProcessor::makeBuses()
{
    return BusesProperties().withInput ("Double", juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)
                            .withInput ("Guide", juce::AudioChannelSet::stereo(), true);
}

juce::AudioProcessorValueTreeState::ParameterLayout SillageProcessor::makeLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "engine", 1 }, "Moteur", StringArray { "Signalsmith", "Rubber Band R2", "Rubber Band R3" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "strength", 1 }, "Force", NormalisableRange<float> (0.f, 100.f, 1.f), 100.f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "protect", 1 }, "Consonnes protégées"_fr, NormalisableRange<float> (0.f, 100.f, 1.f), 100.f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tightness", 1 }, "Serrage", NormalisableRange<float> (0.f, 100.f, 1.f), 95.f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "maxshift", 1 }, "Écart maximal"_fr, StringArray { "0,5 s", "1 s", "1,5 s", "2 s", "3 s" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "caplen", 1 }, "Durée de capture"_fr, StringArray { "30 s", "1 min", "2 min", "3 min", "5 min" }, 2));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "aligned", 1 }, "Recalé"_fr, true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "suggest", 1 }, "Sensibilité de la suggestion"_fr, StringArray { "Haute (30 ms)", "Moyenne (60 ms)", "Basse (100 ms)" }, 1));
    return l;
}

SillageProcessor::SillageProcessor()
    : juce::AudioProcessor (makeBuses()), apvts_ (*this, nullptr, "SILLAGE", makeLayout())
{
    for (const char* id : { "engine", "strength", "protect", "tightness", "maxshift" }) apvts_.addParameterListener (id, this);
    startTimerHz (20);
}

SillageProcessor::~SillageProcessor()
{
    stopTimer();
    if (worker_.joinable()) worker_.join();
}

bool SillageProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto m = l.getMainInputChannelSet();
    if (m != l.getMainOutputChannelSet() || (m != juce::AudioChannelSet::mono() && m != juce::AudioChannelSet::stereo())) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto g = l.inputBuses[1];
        if (! g.isDisabled() && g != juce::AudioChannelSet::mono() && g != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void SillageProcessor::prepareToPlay (double sr, int)
{
    if (sr != sampleRate_ && phase() != Phase::Idle) sampleRateStale_ = 1; // la capture n'est plus valable à une autre fréquence
    sampleRate_ = sr;
}

align::AlignSettings SillageProcessor::alignSettings() const
{
    align::AlignSettings a;
    a.tightness = *apvts_.getRawParameterValue ("tightness") / 100.0;
    a.maxShiftSeconds = kMaxShifts[std::clamp ((int) *apvts_.getRawParameterValue ("maxshift"), 0, 4)];
    return a;
}

align::RenderSettings SillageProcessor::renderSettings() const
{
    align::RenderSettings r;
    const int e = (int) *apvts_.getRawParameterValue ("engine");
    r.engine = e == 1 ? align::Engine::RubberBandR2 : e == 2 ? align::Engine::RubberBandR3 : align::Engine::Signalsmith;
    r.strength = *apvts_.getRawParameterValue ("strength") / 100.0;
    r.protect = *apvts_.getRawParameterValue ("protect") / 100.0;
    std::lock_guard<std::mutex> lk (dataMutex_);
    r.strengthCurve = curve_;
    return r;
}

void SillageProcessor::parameterChanged (const juce::String& id, float)
{
    const int level = (id == "tightness" || id == "maxshift") ? 2 : 1;
    int cur = dirty_.load();
    while (cur < level && ! dirty_.compare_exchange_weak (cur, level)) {}
    dirtySince_ = (std::int64_t) juce::Time::getMillisecondCounter();
}

// ---------------------------------------------------------------------------------------------------------------- audio
void SillageProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto main = getBusBuffer (buffer, true, 0);
    const int nch = main.getNumChannels();
    if (n <= 0 || nch <= 0) return;

    bool playing = false, havePos = false;
    std::int64_t pos = 0;
    if (auto* ph = getPlayHead())
        if (auto info = ph->getPosition())
        {
            playing = info->getIsPlaying();
            if (auto t = info->getTimeInSamples()) { pos = *t; havePos = true; }
        }
    playheadKnown_ = havePos;
    lastPlayhead_ = pos;

    const bool haveSide = getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled();

    // --- capture
    int ph = phase_.load();
    if (ph == (int) Phase::Armed && playing && havePos)
    {
        if (auto* cap = captureRaw_.load())
        {
            cap->startSong = pos;
            cap->written = 0;
            phase_ = ph = (int) Phase::Capturing;
        }
    }
    if (ph == (int) Phase::Capturing)
    {
        auto* cap = captureRaw_.load();
        const std::size_t w = cap ? cap->written.load() : 0;
        const bool contiguous = havePos && pos == cap->startSong + (std::int64_t) w;
        if (! cap || ! playing || ! contiguous || stopRequested_.load())
        {
            phase_ = (int) Phase::Computing; // arrêt, saut dans le morceau ou demande de l'interface
            stopRequested_ = false;
        }
        else
        {
            const std::size_t room = cap->capacity - w, take = std::min<std::size_t> ((std::size_t) n, room);
            for (int c = 0; c < cap->channels; ++c)
            {
                const float* src = main.getReadPointer (std::min (c, nch - 1));
                std::copy (src, src + take, cap->dub[(std::size_t) c].begin() + (std::ptrdiff_t) w);
            }
            if (haveSide)
            {
                auto side = getBusBuffer (buffer, true, 1);
                const int sc = side.getNumChannels();
                for (std::size_t i = 0; i < take; ++i)
                {
                    float s = 0.f;
                    for (int c = 0; c < sc; ++c) s += side.getReadPointer (c)[i];
                    cap->guide[w + i] = sc > 0 ? s / (float) sc : 0.f;
                }
            }
            cap->written = w + take;
            if (take < (std::size_t) n || cap->written >= cap->capacity) phase_ = (int) Phase::Computing;
        }
        return; // pendant la capture le double passe tel quel
    }

    // --- lecture du résultat
    if (ph != (int) Phase::Ready || ! playing || ! havePos || sampleRateStale_.load() || *apvts_.getRawParameterValue ("aligned") < 0.5f) return;
    const juce::SpinLock::ScopedTryLockType lock (resultLock_);
    if (! lock.isLocked() || ! result_) return;
    const auto& r = *result_;
    if (std::fabs (r.sampleRate - sampleRate_) > 0.5) return;
    const int ach = (int) r.aligned.size();
    for (int i = 0; i < n; ++i)
    {
        const std::int64_t idx = pos + i - r.startSong;
        if (idx < 0 || idx >= (std::int64_t) r.length) continue;
        const float wIn = std::min (1.f, (float) idx / (float) kFade), wOut = std::min (1.f, (float) ((std::int64_t) r.length - 1 - idx) / (float) kFade);
        const float w = std::min (wIn, wOut);
        for (int c = 0; c < nch; ++c)
        {
            float* d = main.getWritePointer (c);
            d[i] = d[i] * (1.f - w) + r.aligned[(std::size_t) std::min (c, ach - 1)][(std::size_t) idx] * w;
        }
    }
}

// ----------------------------------------------------------------------------------------------------------- commandes
void SillageProcessor::armCapture()
{
    if (phase() == Phase::Computing || phase() == Phase::Capturing) return;
    const double sec = kCaptureSeconds[std::clamp ((int) *apvts_.getRawParameterValue ("caplen"), 0, 4)];
    auto cap = std::make_shared<SillageCapture>();
    cap->sampleRate = sampleRate_;
    cap->channels = std::max (1, getMainBusNumInputChannels());
    cap->capacity = (std::size_t) (sec * sampleRate_);
    cap->dub.assign ((std::size_t) cap->channels, std::vector<float> (cap->capacity, 0.f));
    cap->guide.assign (cap->capacity, 0.f);
    {
        std::lock_guard<std::mutex> lk (dataMutex_);
        message_ = {};
    }
    capture_ = cap;
    sampleRateStale_ = 0;
    stopRequested_ = false;
    captureRaw_ = cap.get();
    phase_ = (int) Phase::Armed;
}

void SillageProcessor::finishCapture()
{
    if (phase() == Phase::Armed) { phase_ = (int) Phase::Idle; captureRaw_ = nullptr; return; }
    if (phase() != Phase::Capturing) return;
    stopRequested_ = true;
    stopRequestedAt_ = (std::int64_t) juce::Time::getMillisecondCounter();
}

void SillageProcessor::clearAll()
{
    phase_ = (int) Phase::Idle;
    captureRaw_ = nullptr;
    if (worker_.joinable()) worker_.join();
    jobDone_ = false;
    jobCaptureId_ = nullptr;
    {
        const juce::SpinLock::ScopedLockType lk (resultLock_);
        result_.reset();
    }
    std::lock_guard<std::mutex> lk (dataMutex_);
    capture_.reset();
    analysis_.reset();
    display_.reset();
    message_ = {};
}

// ------------------------------------------------------------------------------------------------------------ export
bool SillageProcessor::canExport() const
{
    const juce::SpinLock::ScopedLockType lk (resultLock_);
    return phase() == Phase::Ready && result_ != nullptr;
}

juce::String SillageProcessor::suggestedExportName() const
{
    std::shared_ptr<const SillageResult> r;
    {
        const juce::SpinLock::ScopedLockType lk (resultLock_);
        r = result_;
    }
    if (! r) return "double-recale.wav";
    const double s = (double) r->startSong / r->sampleRate;
    const int minutes = (int) (s / 60.0);
    return "double-recale_a_" + juce::String (minutes) + "min" + juce::String (s - minutes * 60.0, 2) + "s.wav";
}

bool SillageProcessor::exportAligned (const juce::File& file, juce::String& error)
{
    std::shared_ptr<const SillageResult> r;
    {
        const juce::SpinLock::ScopedLockType lk (resultLock_);
        r = result_;
    }
    if (! r || r->aligned.empty()) { error = "Rien à exporter : faites d'abord une capture."_fr; return false; }
    auto target = file.hasFileExtension ("wav") ? file : file.withFileExtension ("wav");
    target.deleteFile();
    auto os = std::make_unique<juce::FileOutputStream> (target);
    if (! os->openedOk()) { error = "Impossible d'écrire le fichier : "_fr + target.getFullPathName(); return false; }
    juce::StringPairArray meta;
    meta.set (juce::WavAudioFormat::bwavDescription, "Sillage : double recalé sur le guide");
    meta.set (juce::WavAudioFormat::bwavOriginator, "Sillage");
    meta.set (juce::WavAudioFormat::bwavTimeReference, juce::String ((juce::int64) r->startSong)); // position de la capture dans le morceau, en échantillons
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (os.get(), r->sampleRate, (unsigned) r->aligned.size(), 24, meta, 0));
    if (! writer) { error = "Le format du fichier n'a pas pu être créé."_fr; return false; }
    os.release(); // le flux appartient à l'écrivain
    std::vector<const float*> p;
    for (const auto& c : r->aligned) p.push_back (c.data());
    const bool ok = writer->writeFromFloatArrays (p.data(), (int) p.size(), (int) r->length);
    writer.reset();
    if (! ok) { error = "L'écriture du fichier a échoué."_fr; return false; }
    {
        std::lock_guard<std::mutex> lk (dataMutex_);
        message_ = "Exporté : "_fr + target.getFileName() + " (commence à "_fr + juce::String ((double) r->startSong / r->sampleRate, 2) + " s dans le morceau)"_fr;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ calcul en arrière-plan
void SillageProcessor::requestRecompute (bool fullAlign)
{
    int cur = dirty_.load();
    const int level = fullAlign ? 2 : 1;
    while (cur < level && ! dirty_.compare_exchange_weak (cur, level)) {}
    dirtySince_ = (std::int64_t) juce::Time::getMillisecondCounter() - 1000; // tout de suite
}

void SillageProcessor::startJob (bool fullAlign)
{
    auto cap = capture_;
    if (! cap || jobRunning_.load()) return;
    if (worker_.joinable()) worker_.join();
    const std::size_t n = std::min (cap->written.load(), cap->capacity);
    if (n < (std::size_t) (cap->sampleRate * 0.5))
    {
        { std::lock_guard<std::mutex> lk (dataMutex_); message_ = "Capture trop courte (moins d'une demi-seconde)."; }
        phase_ = (int) Phase::Idle;
        return;
    }
    std::shared_ptr<const align::Analysis> prev;
    {
        std::lock_guard<std::mutex> lk (dataMutex_);
        prev = analysis_;
    }
    if (! fullAlign && ! prev) fullAlign = true;
    const auto as = alignSettings();
    const auto rs = renderSettings();
    jobCaptureId_ = cap.get();
    jobRunning_ = true;
    jobDone_ = false;
    jobGeneration_ = ++generation_;
    worker_ = std::thread ([this, cap, n, fullAlign, prev, as, rs]() {
        auto analysis = prev;
        juce::String message;
        std::shared_ptr<SillageResult> result;
        std::shared_ptr<SillageDisplay> disp;
        try
        {
            std::vector<std::vector<float>> dub = cap->dub;
            for (auto& c : dub) c.resize (n);
            const auto dubMono = monoOf (dub, n);
            double rms = 0;
            for (std::size_t i = 0; i < n; ++i) rms += (double) cap->guide[i] * cap->guide[i];
            rms = std::sqrt (rms / (double) n);
            if (fullAlign && rms < 1e-5)
                message = "L'entrée latérale est silencieuse : branchez le guide sur l'entrée latérale de ce plugin."_fr;
            else
            {
                if (fullAlign)
                    analysis = std::make_shared<align::Analysis> (align::analysePair (cap->guide.data(), n, dubMono.data(), n, cap->sampleRate, as));
                auto rr = align::renderAnalysis (dub, *analysis, rs);
                if (rr.fellBack) message = "Le moteur choisi a échoué : Signalsmith a été utilisé."_fr;
                result = std::make_shared<SillageResult>();
                result->sampleRate = cap->sampleRate;
                result->startSong = cap->startSong;
                result->length = n;
                result->aligned = std::move (rr.channels);
                disp = std::make_shared<SillageDisplay>();
                disp->seconds = (double) n / cap->sampleRate;
                computePeaks (cap->guide.data(), n, disp->guidePeaks);
                computePeaks (dubMono.data(), n, disp->dubPeaks);
                const auto alignedMono = monoOf (result->aligned, n);
                computePeaks (alignedMono.data(), n, disp->alignedPeaks);
                const auto sh = align::appliedShift (*analysis, rs);
                disp->shiftMs.resize (sh.size());
                for (std::size_t i = 0; i < sh.size(); ++i) disp->shiftMs[i] = (float) (sh[i] * 1000.0);
                disp->consonants = analysis->consonants;
            }
        }
        catch (const std::exception& e) { message = juce::String ("Erreur de calcul : ") + e.what(); result.reset(); }
        {
            std::lock_guard<std::mutex> lk (jobMutex_);
            jobResult_ = result;
            jobAnalysis_ = analysis;
            jobDisplay_ = disp;
            jobMessage_ = message;
        }
        jobDone_ = true;
        jobRunning_ = false;
    });
}

void SillageProcessor::adoptJobResult()
{
    if (worker_.joinable()) worker_.join();
    std::shared_ptr<const SillageResult> res;
    std::shared_ptr<const align::Analysis> an;
    std::shared_ptr<const SillageDisplay> disp;
    juce::String msg;
    {
        std::lock_guard<std::mutex> lk (jobMutex_);
        res = jobResult_; an = jobAnalysis_; disp = jobDisplay_; msg = jobMessage_;
        jobResult_.reset(); jobAnalysis_.reset(); jobDisplay_.reset();
    }
    jobDone_ = false;
    if (capture_.get() != jobCaptureId_) return; // une autre capture a pris la place entre-temps (ou « Tout effacer »)
    {
        std::lock_guard<std::mutex> lk (dataMutex_);
        message_ = msg;
        if (res) { analysis_ = an; display_ = disp; }
    }
    if (res)
    {
        { const juce::SpinLock::ScopedLockType lk (resultLock_); result_ = res; }
        phase_ = (int) Phase::Ready;
    }
    else
        phase_ = result_ ? (int) Phase::Ready : (int) Phase::Idle;
}

void SillageProcessor::timerCallback()
{
    if (jobDone_.load()) adoptJobResult();
    const auto now = (std::int64_t) juce::Time::getMillisecondCounter();
    const Phase ph = phase();
    if (ph == Phase::Capturing && stopRequested_.load() && now - stopRequestedAt_.load() > 1000)
    {
        phase_ = (int) Phase::Computing; // l'hôte n'appelle plus l'audio : on conclut nous-mêmes
        stopRequested_ = false;
    }
    if (phase() == Phase::Computing && ! jobRunning_.load() && ! jobDone_.load())
    {
        const bool renderOnly = restoredNeedsRender_;
        restoredNeedsRender_ = false;
        dirty_ = 0;
        startJob (! renderOnly);
        return;
    }
    if (phase() == Phase::Ready && ! jobRunning_.load() && ! jobDone_.load())
    {
        const int d = dirty_.load();
        if (d != 0 && now - dirtySince_.load() > 350)
        {
            dirty_ = 0;
            startJob (d == 2);
        }
    }
}

// ---------------------------------------------------------------------------------------------------- affichage / états
juce::String SillageProcessor::statusText() const
{
    juce::String msg;
    {
        std::lock_guard<std::mutex> lk (dataMutex_);
        msg = message_;
    }
    if (sampleRateStale_.load() && phase() != Phase::Idle) return "La fréquence d'échantillonnage a changé : refaites une capture."_fr;
    switch (phase())
    {
        case Phase::Idle:       return msg.isNotEmpty() ? msg : "Prêt. « Capturer », puis lancez la lecture sur la phrase à caler."_fr;
        case Phase::Armed:      return "En attente : lancez la lecture (la capture démarre avec elle)."_fr;
        case Phase::Capturing:  return "Capture en cours : " + juce::String (capturedSeconds(), 1) + " s";
        case Phase::Computing:  return "Calcul en cours...";
        case Phase::Ready:      return msg.isNotEmpty() ? msg : "Recalé. Réglez la force, le moteur, les consonnes : le rendu se refait tout seul."_fr;
    }
    return {};
}

double SillageProcessor::capturedSeconds() const
{
    auto* c = captureRaw_.load();
    return c ? (double) c->written.load() / c->sampleRate : 0.0;
}

double SillageProcessor::capacitySeconds() const
{
    auto* c = captureRaw_.load();
    return c ? (double) c->capacity / c->sampleRate : 0.0;
}

std::shared_ptr<const SillageDisplay> SillageProcessor::display() const
{
    std::lock_guard<std::mutex> lk (dataMutex_);
    return display_;
}

double SillageProcessor::playheadSeconds (bool& valid) const
{
    valid = false;
    std::shared_ptr<SillageCapture> cap = capture_;
    if (! cap || ! playheadKnown_.load()) return 0.0;
    const double t = (double) (lastPlayhead_.load() - cap->startSong) / cap->sampleRate;
    valid = t >= 0.0;
    return t;
}

std::vector<align::StrengthPoint> SillageProcessor::strengthCurve() const
{
    std::lock_guard<std::mutex> lk (dataMutex_);
    return curve_;
}

void SillageProcessor::setStrengthCurve (std::vector<align::StrengthPoint> c)
{
    { std::lock_guard<std::mutex> lk (dataMutex_); curve_ = std::move (c); }
    requestRecompute (false);
}

int SillageProcessor::suggestStrength()
{
    std::shared_ptr<const align::Analysis> an;
    {
        std::lock_guard<std::mutex> lk (dataMutex_);
        an = analysis_;
    }
    if (! an || phase() != Phase::Ready) return 0;
    align::SuggestSettings ss;
    ss.thresholdMs = kSuggestMs[std::clamp ((int) *apvts_.getRawParameterValue ("suggest"), 0, 2)];
    auto curve = align::suggestStrengthCurve (*an, ss);
    const int n = (int) curve.size();
    {
        std::lock_guard<std::mutex> lk (dataMutex_);
        message_ = n > 0 ? "Force proposée : "_fr + juce::String (n) + " points. À ajuster à la souris."_fr
                         : "Aucun passage ne dépasse le seuil : la force reste constante."_fr;
    }
    setStrengthCurve (std::move (curve));
    return n;
}

std::vector<align::Segment> SillageProcessor::consonants() const
{
    std::lock_guard<std::mutex> lk (dataMutex_);
    return analysis_ ? analysis_->consonants : std::vector<align::Segment> {};
}

void SillageProcessor::setConsonants (std::vector<align::Segment> v)
{
    {
        std::lock_guard<std::mutex> lk (dataMutex_);
        if (! analysis_) return;
        auto copy = std::make_shared<align::Analysis> (*analysis_);
        copy->consonants = std::move (v);
        analysis_ = copy;
    }
    requestRecompute (false);
}

juce::AudioProcessorEditor* SillageProcessor::createEditor() { return new SillageEditor (*this); }

// ------------------------------------------------------------------------------------------------------------------ état
void SillageProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto* obj = new juce::DynamicObject();
    juce::var root (obj);
    if (auto xml = apvts_.copyState().createXml()) obj->setProperty ("params", xml->toString());
    std::shared_ptr<SillageCapture> cap = capture_;
    std::shared_ptr<const align::Analysis> an;
    {
        std::lock_guard<std::mutex> lk (dataMutex_);
        an = analysis_;
        juce::Array<juce::var> pts;
        for (const auto& p : curve_) { juce::Array<juce::var> q; q.add (p.time); q.add (p.strength); pts.add (q); }
        obj->setProperty ("curve", pts);
    }
    const bool hasData = phase() == Phase::Ready && cap && an;
    obj->setProperty ("hasData", hasData);
    juce::MemoryBlock dubFlac, guideFlac;
    if (hasData)
    {
        const std::size_t n = std::min (cap->written.load(), cap->capacity);
        obj->setProperty ("startSong", (juce::int64) cap->startSong);
        obj->setProperty ("sampleRate", cap->sampleRate);
        obj->setProperty ("length", (juce::int64) n);
        obj->setProperty ("hop", an->hopSeconds);
        juce::Array<juce::var> cons;
        for (const auto& s : an->consonants) { juce::Array<juce::var> q; q.add (s.start); q.add (s.end); cons.add (q); }
        obj->setProperty ("consonants", cons);
        std::vector<const float*> dubPtrs;
        for (const auto& c : cap->dub) dubPtrs.push_back (c.data());
        dubFlac = encodeFlac (dubPtrs, n, cap->sampleRate, 24);
        guideFlac = encodeFlac ({ cap->guide.data() }, n, cap->sampleRate, 16);
    }
    juce::MemoryOutputStream os (dest, false);
    const auto json = juce::JSON::toString (root, true);
    os.writeInt ((int) kMagic);
    os.writeInt ((int) json.getNumBytesAsUTF8());
    os.write (json.toRawUTF8(), json.getNumBytesAsUTF8());
    if (hasData)
    {
        os.writeInt ((int) an->path.size());
        for (double v : an->path) os.writeFloat ((float) v);
        os.writeInt ((int) dubFlac.getSize());
        os.write (dubFlac.getData(), dubFlac.getSize());
        os.writeInt ((int) guideFlac.getSize());
        os.write (guideFlac.getData(), guideFlac.getSize());
    }
}

void SillageProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size < 8) return;
    juce::MemoryInputStream in (data, (std::size_t) size, false);
    if ((std::uint32_t) in.readInt() != kMagic) return;
    const int jsonLen = in.readInt();
    if (jsonLen <= 0 || jsonLen > size) return;
    juce::MemoryBlock jb;
    in.readIntoMemoryBlock (jb, jsonLen);
    const auto root = juce::JSON::parse (jb.toString());
    if (! root.isObject()) return;
    if (root.hasProperty ("params"))
        if (auto xml = juce::XmlDocument::parse (root["params"].toString()))
            apvts_.replaceState (juce::ValueTree::fromXml (*xml));
    dirty_ = 0;
    {
        std::vector<align::StrengthPoint> pts;
        if (auto* a = root["curve"].getArray())
            for (const auto& p : *a)
                if (auto* q = p.getArray(); q && q->size() == 2) pts.push_back ({ (double) (*q)[0], (double) (*q)[1] });
        std::lock_guard<std::mutex> lk (dataMutex_);
        curve_ = std::move (pts);
    }
    if (! (bool) root["hasData"]) return;

    const std::size_t pathCount = (std::size_t) std::max (0, in.readInt());
    std::vector<double> path (pathCount);
    for (auto& v : path) v = in.readFloat();
    const int dubSize = in.readInt();
    juce::MemoryBlock dubBlock;
    in.readIntoMemoryBlock (dubBlock, dubSize);
    const int guideSize = in.readInt();
    juce::MemoryBlock guideBlock;
    in.readIntoMemoryBlock (guideBlock, guideSize);

    auto cap = std::make_shared<SillageCapture>();
    std::vector<std::vector<float>> dub, guide;
    if (! decodeFlac (dubBlock.getData(), dubBlock.getSize(), dub) || ! decodeFlac (guideBlock.getData(), guideBlock.getSize(), guide) || guide.empty()) return;
    cap->sampleRate = (double) root["sampleRate"];
    cap->startSong = (juce::int64) root["startSong"];
    cap->channels = (int) dub.size();
    cap->dub = std::move (dub);
    cap->guide = std::move (guide[0]);
    cap->capacity = cap->guide.size();
    cap->written = cap->capacity;
    auto an = std::make_shared<align::Analysis>();
    an->sampleRate = cap->sampleRate;
    an->length = cap->capacity;
    an->hopSeconds = (double) root["hop"];
    an->path = std::move (path);
    if (auto* a = root["consonants"].getArray())
        for (const auto& p : *a)
            if (auto* q = p.getArray(); q && q->size() == 2) an->consonants.push_back ({ (double) (*q)[0], (double) (*q)[1] });
    if (worker_.joinable()) worker_.join();
    jobDone_ = false;
    {
        std::lock_guard<std::mutex> lk (dataMutex_);
        analysis_ = an;
    }
    capture_ = cap;
    captureRaw_ = nullptr;
    restoredNeedsRender_ = true;
    phase_ = (int) Phase::Computing;
}

// Point d'entrée demandé par JUCE (CLAP et Standalone).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SillageProcessor(); }
