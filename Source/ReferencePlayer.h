#pragma once

#include <JuceHeader.h>
#include <atomic>

// Spielt den Referenztrack ab, indem er dem Ausgang des Plugins beigemischt wird.
// Gehört dem AudioProcessor; Laden/Steuern im Message-Thread, addTo() im Audio-Thread.
class ReferencePlayer
{
public:
    ReferencePlayer();
    ~ReferencePlayer();

    void prepare(double sampleRate, int samplesPerBlock);
    void release();

    // Audio-Thread: mischt den Referenztrack (falls er läuft) in den Puffer
    void addTo(juce::AudioBuffer<float>& buffer);

    // Message-Thread
    bool load(const juce::File& file);
    void unload();
    void play();
    void pause();
    void stop();

    bool isPlaying() const { return transport.isPlaying(); }
    bool isReady() const { return readerSource != nullptr; }
    bool isPrepared() const { return prepared.load(); }
    juce::File getFile() const { return currentFile; }
    double getPositionSeconds() const { return transport.getCurrentPosition(); }
    double getLengthSeconds() const { return transport.getLengthInSeconds(); }

private:
    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAheadThread{ "DAW Notes Referenz-Player" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::AudioBuffer<float> scratch;
    juce::File currentFile;
    std::atomic<bool> prepared{ false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReferencePlayer)
};
