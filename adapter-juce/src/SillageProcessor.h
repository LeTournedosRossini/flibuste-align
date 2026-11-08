#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wunused-parameter", "-Wextra-semi", "-Wnon-virtual-dtor")
#include <clap-juce-extensions/clap-juce-extensions.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#include "align/Pipeline.h"

// Les textes de l'interface sont en UTF-8 : "texte"_fr donne une juce::String correcte (accents compris).
inline juce::String operator""_fr (const char* s, std::size_t n) { return juce::String::fromUTF8 (s, (int) n); }

// « Sillage » (nom provisoire) : cale une doublure sur un guide. Le plugin s'insère sur la piste du double ; le guide arrive par l'entrée latérale.
// Mode d'emploi : « Capturer », lancer la lecture sur la phrase, arrêter (ou « Terminer ») ; le calcul se fait en arrière-plan, puis le plugin rejoue le
// double recalé à la même place dans le morceau. Hors de la zone capturée, le double passe tel quel. Aucune latence ajoutée.

// Ce que l'audio a enregistré : le double (une ou deux voies) et le guide (somme mono), sur la même ligne de temps du morceau.
struct SillageCapture
{
    double sampleRate = 44100.0;
    int channels = 2;
    std::int64_t startSong = 0;                 // position du morceau (échantillons) du premier échantillon capturé
    std::size_t capacity = 0;
    std::atomic<std::size_t> written { 0 };
    std::vector<std::vector<float>> dub;
    std::vector<float> guide;
};

// Ce que l'audio rejoue.
struct SillageResult
{
    double sampleRate = 44100.0;
    std::int64_t startSong = 0;
    std::size_t length = 0;
    std::vector<std::vector<float>> aligned;
};

// Ce que l'interface dessine (rempli sur le fil de messages).
struct SillageDisplay
{
    double seconds = 0;
    std::vector<float> guidePeaks, dubPeaks, alignedPeaks; // paires (min, max), `kPeaks` chacune
    std::vector<float> shiftMs;                            // décalage appliqué, un point tous les 5 ms
    std::vector<align::Segment> consonants;
    static constexpr int kPeaks = 2048;
};

class SillageProcessor : public juce::AudioProcessor, private juce::Timer, private juce::AudioProcessorValueTreeState::Listener,
                         public clap_juce_extensions::clap_juce_audio_processor_capabilities
{
public:
    enum class Phase { Idle = 0, Armed, Capturing, Computing, Ready };

    SillageProcessor();
    ~SillageProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Sillage"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- commandes de l'interface (fil de messages)
    void armCapture();            // prépare l'enregistrement ; il démarre à la prochaine lecture
    void finishCapture();         // arrête la capture en cours et lance le calcul
    void clearAll();              // oublie capture et résultat
    // « Acter » : écrit le double recalé (tel qu'on l'entend, réglages actuels) dans un WAV 24 bits à placer sur la timeline comme nouvelle prise.
    // Le fichier commence à la position de la capture ; cette position est aussi écrite dans le WAV (horodatage BWF).
    bool exportAligned (const juce::File&, juce::String& error);
    juce::String suggestedExportName() const;
    bool canExport() const;
    juce::AudioProcessorValueTreeState& parameters() { return apvts_; }

    // --- état affiché
    Phase phase() const { return (Phase) phase_.load(); }
    bool busy() const { return jobRunning_.load() || phase() == Phase::Computing; }
    juce::String statusText() const;
    double capturedSeconds() const;
    double capacitySeconds() const;
    std::shared_ptr<const SillageDisplay> display() const;
    double playheadSeconds (bool& valid) const; // position dans la zone capturée, en secondes
    std::vector<align::StrengthPoint> strengthCurve() const;
    void setStrengthCurve (std::vector<align::StrengthPoint>);
    // Pose une courbe de force proposée d'après les passages très décalés (sensibilité = réglage « suggest »). Rend le nombre de points posés (0 : rien à proposer).
    int suggestStrength();
    std::vector<align::Segment> consonants() const;
    void setConsonants (std::vector<align::Segment>);

private:
    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void timerCallback() override;
    void parameterChanged (const juce::String&, float) override;
    void startJob (bool fullAlign);
    void adoptJobResult();
    align::AlignSettings alignSettings() const;
    align::RenderSettings renderSettings() const;
    void requestRecompute (bool fullAlign);

    juce::AudioProcessorValueTreeState apvts_;
    std::atomic<int> phase_ { 0 };
    std::atomic<int> sampleRateStale_ { 0 };
    std::atomic<bool> stopRequested_ { false };
    std::atomic<std::int64_t> stopRequestedAt_ { 0 };

    // capture (audio -> message)
    std::shared_ptr<SillageCapture> capture_;
    std::atomic<SillageCapture*> captureRaw_ { nullptr };
    std::atomic<std::int64_t> lastPlayhead_ { 0 };
    std::atomic<bool> playheadKnown_ { false };

    // résultat rejoué par l'audio
    mutable juce::SpinLock resultLock_;
    std::shared_ptr<const SillageResult> result_;

    // analyse conservée (fil de messages)
    mutable std::mutex dataMutex_;
    std::shared_ptr<const align::Analysis> analysis_;
    std::vector<align::StrengthPoint> curve_;
    std::shared_ptr<const SillageDisplay> display_;
    juce::String message_;

    // travail en arrière-plan
    std::thread worker_;
    std::atomic<bool> jobRunning_ { false }, jobDone_ { false };
    std::mutex jobMutex_;
    std::shared_ptr<const SillageResult> jobResult_;
    std::shared_ptr<const align::Analysis> jobAnalysis_;
    std::shared_ptr<const SillageDisplay> jobDisplay_;
    juce::String jobMessage_;
    std::atomic<std::uint64_t> generation_ { 0 };
    std::uint64_t jobGeneration_ = 0;
    std::atomic<int> dirty_ { 0 };          // 0 rien, 1 rendre, 2 recalculer
    std::atomic<std::int64_t> dirtySince_ { 0 };
    bool restoredNeedsRender_ = false;
    const void* jobCaptureId_ = nullptr;

    double sampleRate_ = 44100.0;
    friend class SillageEditor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SillageProcessor)
};
