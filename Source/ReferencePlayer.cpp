#pragma execution_character_set("utf-8")

#include "ReferencePlayer.h"

ReferencePlayer::ReferencePlayer()
{
    formats.registerBasicFormats();
    readAheadThread.startThread(juce::Thread::Priority::normal);
}

ReferencePlayer::~ReferencePlayer()
{
    transport.setSource(nullptr);
    readerSource.reset();
    readAheadThread.stopThread(2000);
}

void ReferencePlayer::prepare(double sampleRate, int samplesPerBlock)
{
    transport.prepareToPlay(samplesPerBlock, sampleRate);
    scratch.setSize(2, juce::jmax(samplesPerBlock, 512), false, false, true);
    prepared = true;
}

void ReferencePlayer::release()
{
    prepared = false;
    transport.releaseResources();
}

void ReferencePlayer::addTo(juce::AudioBuffer<float>& buffer)
{
    if (!prepared.load() || !transport.isPlaying())
        return;

    const int numSamples = buffer.getNumSamples();
    if (scratch.getNumSamples() < numSamples) // nur falls der Host größere Blöcke schickt als angekündigt
        scratch.setSize(2, numSamples, false, false, true);

    juce::AudioSourceChannelInfo info(&scratch, 0, numSamples);
    transport.getNextAudioBlock(info);

    if (buffer.getNumChannels() >= 2)
    {
        buffer.addFrom(0, 0, scratch, 0, 0, numSamples);
        buffer.addFrom(1, 0, scratch, 1, 0, numSamples);
    }
    else if (buffer.getNumChannels() == 1)
    {
        buffer.addFrom(0, 0, scratch, 0, 0, numSamples, 0.5f);
        buffer.addFrom(0, 0, scratch, 1, 0, numSamples, 0.5f);
    }
}

bool ReferencePlayer::load(const juce::File& file)
{
    if (file == currentFile && readerSource != nullptr)
        return true;

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return false;

    const double fileRate = reader->sampleRate;
    const int channels = (int)juce::jlimit(1u, 2u, reader->numChannels);
    auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);

    // setSource stoppt die Wiedergabe und tauscht die Quelle threadsicher aus
    transport.setSource(newSource.get(), 32768, &readAheadThread, fileRate, channels);
    readerSource = std::move(newSource);
    currentFile = file;
    return true;
}

void ReferencePlayer::unload()
{
    transport.setSource(nullptr);
    readerSource.reset();
    currentFile = juce::File();
}

void ReferencePlayer::play()
{
    if (readerSource == nullptr) return;

    // Am Ende angekommen: von vorn beginnen
    if (transport.getCurrentPosition() >= transport.getLengthInSeconds() - 0.05)
        transport.setPosition(0.0);
    transport.start();
}

void ReferencePlayer::pause()
{
    transport.stop();
}

void ReferencePlayer::stop()
{
    transport.stop();
    transport.setPosition(0.0);
}
