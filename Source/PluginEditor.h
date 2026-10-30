#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SongLibraryView.h"
#include "SongVisionView.h"
#include "AnalysisView.h"
#include "AboutOverlay.h"
#include "ToDoView.h"

class DawNotesAudioProcessorEditor;

class MainContentComponent : public juce::Component
{
public:
    MainContentComponent(DawNotesAudioProcessorEditor& ed) : editor(ed) {}
    void paint(juce::Graphics& g) override;

private:
    DawNotesAudioProcessorEditor& editor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};

class CustomTabbedComponent : public juce::TabbedComponent
{
public:
    CustomTabbedComponent(juce::TabbedButtonBar::Orientation orientation)
        : juce::TabbedComponent(orientation) {}

    std::function<void(int)> onTabChanged;

    void currentTabChanged(int newIndex, const juce::String& /*tabName*/) override
    {
        if (onTabChanged)
            onTabChanged(newIndex);
    }
};

class DawNotesAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    DawNotesAudioProcessorEditor(DawNotesAudioProcessor&);
    ~DawNotesAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Öffentlicher Zugriff für MainContentComponent (zum Zeichnen der GIFs)
    juce::Image headerGif;

    // Damit PluginProcessor::setStateInformation() darauf zugreifen kann:
    void onSongSelected(int64_t songId);

private:
    DawNotesAudioProcessor& audioProcessor;

    MainContentComponent contentComponent{ *this };

    CustomTabbedComponent tabs{ juce::TabbedButtonBar::TabsAtTop };

    std::unique_ptr<SongLibraryView> songLibraryView;
    std::unique_ptr<SongVisionView> songVisionView;
    std::unique_ptr<AnalysisView> analysisView;
    std::unique_ptr<ToDoView> sessionToDoView;
    std::unique_ptr<ToDoView> releaseToDoView;

    juce::Label activeSongHeaderLabel;
    juce::TextEditor instructionBox;
    juce::HyperlinkButton openManualButton;
    juce::TooltipWindow tooltipWindow{ this, 600 };

    juce::TextButton aboutButton;
    std::unique_ptr<AboutOverlay> aboutOverlay;
    void showAbout();

    int64_t activeSongId = -1;

    void updateTabEnableState();
    void updateInstructionText(int tabIndex);
    void openManualPdf();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DawNotesAudioProcessorEditor)
};