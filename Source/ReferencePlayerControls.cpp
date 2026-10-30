#pragma execution_character_set("utf-8")

#include "ReferencePlayerControls.h"
#include "SettingsManager.h"

namespace
{
    juce::String formatTime(double seconds)
    {
        const int total = juce::jmax(0, (int)seconds);
        return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
    }
}

void ReferencePlayerControls::TransportButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.5f);
    const juce::Colour accent(0xffd9822b);

    g.setColour(isButtonDown ? juce::Colour(0xff3a3a3a) : isMouseOver ? juce::Colour(0xff333333) : juce::Colour(0xff263238));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(active ? accent : juce::Colour(0xff555555));
    g.drawRoundedRectangle(bounds, 4.0f, active ? 2.0f : 1.0f);

    g.setColour(!isEnabled() ? juce::Colour(0xff666666) : active ? accent : juce::Colours::white);
    auto icon = bounds.withSizeKeepingCentre(10.0f, 11.0f);

    switch (symbol)
    {
    case Symbol::Play:
    {
        juce::Path tri;
        tri.addTriangle(icon.getX() + 1.0f, icon.getY(), icon.getX() + 1.0f, icon.getBottom(), icon.getRight() + 1.0f, icon.getCentreY());
        g.fillPath(tri);
        break;
    }
    case Symbol::Pause:
        g.fillRect(icon.withWidth(3.5f));
        g.fillRect(icon.withTrimmedLeft(icon.getWidth() - 3.5f));
        break;
    case Symbol::Stop:
        g.fillRect(icon.reduced(0.5f));
        break;
    }
}

ReferencePlayerControls::ReferencePlayerControls(ReferencePlayer& p) : player(p)
{
    playButton.setTooltip(tr("player_play", "Referenztrack abspielen"));
    pauseButton.setTooltip(tr("player_pause", "Pause"));
    stopButton.setTooltip(tr("player_stop", "Stopp (zurück zum Anfang)"));

    playButton.onClick = [this] {
        if (!player.isReady())
        {
            if (onNeedFile) onNeedFile();
            return;
        }
        player.play();
        refresh();
    };
    pauseButton.onClick = [this] { player.pause(); refresh(); };
    stopButton.onClick = [this] { player.stop(); refresh(); };

    timeLabel.setJustificationType(juce::Justification::centred);
    timeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    timeLabel.setFont(juce::Font(juce::FontOptions(12.0f)));

    addAndMakeVisible(playButton);
    addAndMakeVisible(pauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(timeLabel);

    refresh();
    startTimerHz(5);
}

ReferencePlayerControls::~ReferencePlayerControls()
{
    stopTimer();
}

void ReferencePlayerControls::refresh()
{
    const bool ready = player.isReady();
    const bool playing = player.isPlaying();
    const bool paused = ready && !playing && player.getPositionSeconds() > 0.05;

    if (playButton.active != playing || pauseButton.active != paused)
    {
        playButton.active = playing;
        pauseButton.active = paused;
        playButton.repaint();
        pauseButton.repaint();
    }

    pauseButton.setEnabled(ready);
    stopButton.setEnabled(ready);

    if (ready && !player.isPrepared())
        playButton.setTooltip(tr("player_no_audio", "Keine Audioausgabe aktiv – im Plugin muss die DAW-Spur laufen bzw. in der Standalone ein Ausgabegerät gewählt sein."));
    else
        playButton.setTooltip(ready ? tr("player_play", "Referenztrack abspielen") + ": " + player.getFile().getFileName()
                                    : tr("player_choose", "Referenztrack zum Anhören wählen …"));

    timeLabel.setText(ready ? formatTime(player.getPositionSeconds()) + " / " + formatTime(player.getLengthSeconds()) : juce::String("--:--"),
                      juce::dontSendNotification);
}

void ReferencePlayerControls::timerCallback()
{
    refresh();
}

void ReferencePlayerControls::resized()
{
    auto area = getLocalBounds();
    const int size = juce::jmin(area.getHeight(), 28);

    for (auto* b : { (juce::Button*)&playButton, (juce::Button*)&pauseButton, (juce::Button*)&stopButton })
    {
        b->setBounds(area.removeFromLeft(size).withSizeKeepingCentre(size, size));
        area.removeFromLeft(3);
    }
    timeLabel.setBounds(area);
}
