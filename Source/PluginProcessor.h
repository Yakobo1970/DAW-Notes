#pragma once

#include <JuceHeader.h>
#include "DatabaseManager.h"

class DawNotesAudioProcessor : public juce::AudioProcessor
{
public:
    DatabaseManager& getDbManager() { return dbManager; }

    DawNotesAudioProcessor();
    ~DawNotesAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // === EIGENE GETTER & SETTER FÜR DIE DATENBANK ===
    DatabaseManager& getDatabaseManager() { return dbManager; }
    int64_t getActiveSongId() const { return activeSongId; }
    void setActiveSongId(int64_t newId) { activeSongId = newId; }

private:
    DatabaseManager dbManager;
    int64_t activeSongId = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DawNotesAudioProcessor)
};