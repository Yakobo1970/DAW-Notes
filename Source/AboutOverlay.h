#pragma once

#include <JuceHeader.h>

// "Über DAW Notes": Overlay mit langsam scrollender Entstehungsgeschichte und Links
class AboutOverlay : public juce::Component, private juce::Timer
{
public:
    AboutOverlay();
    ~AboutOverlay() override;

    std::function<void()> onClose;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    static constexpr const char* websiteUrl = "https://www.undabtanzbar.de";
    static constexpr const char* donateUrl = "https://paypal.me/yakobo";

private:
    void timerCallback() override;
    void buildStoryLayout();

    juce::Image logo;
    juce::TextLayout storyLayout;
    float storyHeight = 0.0f;
    float scrollPos = 0.0f;
    bool paused = false;

    juce::Rectangle<int> panel, storyArea;

    juce::TextButton closeButton;
    juce::TextButton donateButton;
    juce::TextButton websiteButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutOverlay)
};
