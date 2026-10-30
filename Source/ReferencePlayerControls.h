#pragma once

#include <JuceHeader.h>
#include "ReferencePlayer.h"

// Play / Pause / Stop + Zeitanzeige für den Referenztrack (Song Vision, Zeile "Ref.-Track")
class ReferencePlayerControls : public juce::Component, private juce::Timer
{
public:
    explicit ReferencePlayerControls(ReferencePlayer& player);
    ~ReferencePlayerControls() override;

    // Wird aufgerufen, wenn "Play" gedrückt wird, aber noch keine Datei geladen ist
    std::function<void()> onNeedFile;

    void refresh();
    void resized() override;

private:
    enum class Symbol { Play, Pause, Stop };

    struct TransportButton : public juce::Button
    {
        TransportButton(Symbol s) : juce::Button("transport"), symbol(s) {}
        void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;

        Symbol symbol;
        bool active = false;
    };

    void timerCallback() override;

    ReferencePlayer& player;
    TransportButton playButton{ Symbol::Play }, pauseButton{ Symbol::Pause }, stopButton{ Symbol::Stop };
    juce::Label timeLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReferencePlayerControls)
};
