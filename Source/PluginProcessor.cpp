#pragma execution_character_set("utf-8")
#include "PluginProcessor.h"
#include "PluginEditor.h"

DawNotesAudioProcessor::DawNotesAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
{
    // dbManager oeffnet die Datenbank bereits in seinem eigenen Konstruktor
    // (Standardpfad %APPDATA%/DAW Notes/daw_notes.db, siehe DatabaseManager::openDatabase()).
}

DawNotesAudioProcessor::~DawNotesAudioProcessor()
{
}

const juce::String DawNotesAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DawNotesAudioProcessor::acceptsMidi() const { return false; }
bool DawNotesAudioProcessor::producesMidi() const { return false; }
bool DawNotesAudioProcessor::isMidiEffect() const { return false; }
double DawNotesAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int DawNotesAudioProcessor::getNumPrograms() { return 1; }
int DawNotesAudioProcessor::getCurrentProgram() { return 0; }
void DawNotesAudioProcessor::setCurrentProgram(int index) {}
const juce::String DawNotesAudioProcessor::getProgramName(int index) { return {}; }
void DawNotesAudioProcessor::changeProgramName(int index, const juce::String& newName) {}
void DawNotesAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {}
void DawNotesAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool DawNotesAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}
#endif

void DawNotesAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());
}

bool DawNotesAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* DawNotesAudioProcessor::createEditor()
{
    return new DawNotesAudioProcessorEditor(*this);
}

// === VST3 / STANDALONE SESSION STATE SPEICHERUNG ===
void DawNotesAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml("DAWNOTES_SETTINGS");
    // Speichert die aktive Song-ID im DAW-Preset
    xml.setAttribute("activeSongId", static_cast<double>(activeSongId));
    copyXmlToBinary(xml, destData);
}

void DawNotesAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName("DAWNOTES_SETTINGS"))
    {
        // 1. Geladene Song-ID aus den DAW-Projekt-Daten wiederherstellen
        activeSongId = static_cast<int64_t>(xmlState->getDoubleAttribute("activeSongId", -1.0));

        // 2. Falls das GUI geöffnet ist, benachrichtigen wir es direkt
        if (auto* editor = dynamic_cast<DawNotesAudioProcessorEditor*>(getActiveEditor()))
        {
            editor->onSongSelected(activeSongId);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DawNotesAudioProcessor();
}