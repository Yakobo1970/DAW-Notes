#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

// Messwerte eines kompletten Song-Durchlaufs (Grundlage für die Genre-Bewertung)
struct AnalysisMetrics
{
    static constexpr int numBands = 6; // Sub, Bass, Tiefe Mitten, Mitten, Präsenz, Air

    double durationSeconds = 0.0;
    double integratedLufs = -70.0;
    double loudnessRange = 0.0;      // LRA in LU
    double maxShortTermLufs = -70.0;
    double samplePeakDb = -100.0;
    int clippedSamples = 0;

    double correlation = 1.0;        // Stereo-Korrelation gesamt (-1 .. +1)
    double lowCorrelation = 1.0;     // Korrelation unterhalb 120 Hz
    double sideToMidDb = -100.0;     // Stereobreite: Side- zu Mid-Energie

    std::array<double, numBands> bandDb{}; // Energieanteil je Band relativ zur Gesamtenergie (dB)

    double tempoBpm = 0.0;
    bool tempoFromHost = false;
    juce::String detectedKey;
    double keyConfidence = 0.0;
    double rhythmStrength = 0.0;     // Ausprägung des Beats (Autokorrelation Peak/Mittel)
    juce::String chordProgression;   // Häufigste Akkordfolge, z.B. "Am | F | C | G"

    juce::var toVar() const;
    static AnalysisMetrics fromVar(const juce::var& v);
};

// Hört dem Audiosignal zu (Audio-Thread schreibt in eine lock-freie FIFO) und wertet
// im eigenen Hintergrund-Thread aus. Gehört dem AudioProcessor, damit eine laufende
// Analyse auch bei geschlossenem Plugin-Fenster weiterläuft.
class TrackAnalyser : private juce::Thread
{
public:
    enum class State { Idle, WaitingForSignal, Listening, Finishing, Done };

    TrackAnalyser();
    ~TrackAnalyser() override;

    void prepare(double newSampleRate);

    // Audio-Thread
    void pushAudio(const juce::AudioBuffer<float>& buffer);
    void setHostTransport(bool isPlaying, double bpm);

    // Message-Thread
    void startAnalysis(int64_t songId);
    void finishAnalysis();
    void cancelAnalysis();

    State getState() const { return state.load(); }
    int64_t getSongId() const { return analysedSongId.load(); }
    double getElapsedSeconds() const { return elapsedSeconds.load(); }
    float getCurrentLevelDb() const { return currentLevelDb.load(); }
    float getMomentaryLufs() const { return momentaryLufs.load(); }

    // Nur gültig, wenn getState() == Done
    const AnalysisMetrics& getResult() const { return result; }
    void markResultConsumed() { if (state.load() == State::Done) state = State::Idle; }

    // Analysiert eine Audiodatei (WAV, MP3, FLAC, AIFF, OGG ...) synchron im aufrufenden Thread.
    // progress erhält 0..1 und liefert false, um abzubrechen.
    static bool analyseFile(const juce::File& file, AnalysisMetrics& out, juce::String& error,
                            const std::function<bool(double)>& progress);

private:
    void run() override;
    void processChunk(const float* left, const float* right, int numSamples);
    void resetAccumulators();
    void computeResult();
    bool shouldAutoFinish() const;

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        float process(float x)
        {
            double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float)y;
        }
        void reset() { z1 = z2 = 0; }
    };

    void setupFilters();
    juce::String estimateChords(double tempoBpm) const;

    // --- Übergabe Audio-Thread -> Analyse-Thread
    static constexpr int fifoSize = 1 << 18;
    juce::AbstractFifo fifo{ fifoSize };
    juce::AudioBuffer<float> fifoBuffer{ 2, fifoSize };

    std::atomic<State> state{ State::Idle };
    std::atomic<bool> capturing{ false };
    std::atomic<bool> finishRequested{ false };
    std::atomic<int64_t> analysedSongId{ -1 };
    std::atomic<double> sampleRate{ 44100.0 };
    std::atomic<bool> hostPlaying{ false };
    std::atomic<double> hostBpm{ 0.0 };

    std::atomic<double> elapsedSeconds{ 0.0 };
    std::atomic<float> currentLevelDb{ -100.0f };
    std::atomic<float> momentaryLufs{ -70.0f };

    // --- Zustand des Analyse-Threads
    double fs = 44100.0;
    bool hostWasPlaying = false;
    double hostStoppedSince = -1.0;  // Zeitpunkt (Sekunden Laufzeit) des Transport-Stopps
    juce::uint32 lastActivityMs = 0;
    double hostBpmSeen = 0.0;

    juce::int64 samplesListened = 0;
    juce::int64 trailingSilence = 0;

    Biquad kShelf[2], kHighPass[2], lowPass[2];
    double blockSum = 0.0;
    int blockSamples = 0, blockLength = 4410;
    std::vector<float> blockPowers;   // mittlere K-gewichtete Leistung je 100-ms-Block
    double meterSum = 0.0;
    int meterSamples = 0;

    double peak = 0.0;
    int clipCount = 0;
    double sumLL = 0, sumRR = 0, sumLR = 0, lowLL = 0, lowRR = 0, lowLR = 0, sumMid = 0, sumSide = 0;

    // Spektrum + Tonart (FFT 8192, Hop 4096)
    static constexpr int specOrder = 13, specSize = 1 << specOrder;
    juce::dsp::FFT specFft{ specOrder };
    std::vector<float> specRing, specFrame, specWindow;
    int specWritePos = 0, specHopCounter = 0;
    std::vector<double> powerSum;
    std::array<double, 12> chroma{};
    std::vector<int> binPitchClass;
    std::vector<std::array<float, 12>> chromaFrames; // Chroma je Spektrum-Frame (für die Akkorderkennung)

    // Onset-Hüllkurve für die Tempo-Erkennung (FFT 1024, Hop 512)
    static constexpr int onsetOrder = 10, onsetSize = 1 << onsetOrder, onsetHop = 512;
    juce::dsp::FFT onsetFft{ onsetOrder };
    std::vector<float> onsetFrame, onsetWindow, prevOnsetMag;
    std::vector<float> onsetEnvelope;
    int onsetHopCounter = 0;

    AnalysisMetrics result;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackAnalyser)
};
