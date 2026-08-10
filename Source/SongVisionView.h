#pragma once

#include <JuceHeader.h>
#include "DatabaseManager.h"

class SongVisionView : public juce::Component
{
public:
    SongVisionView(DatabaseManager& dbManager);
    ~SongVisionView() override;

    void loadSong(int64_t songId);
    void saveCurrentSongData();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    DatabaseManager& db;
    int64_t currentSongId = -1;

    // UI Elemente Zeile 1
    juce::Label bpmLabel{ {}, "BPM:" };
    juce::TextEditor bpmInput;

    juce::Label keyLabel{ {}, "Tonart:" };
    juce::TextEditor keyInput;

    juce::Label genreLabel{ {}, "Genre:" };
    juce::TextEditor genreInput;

    // UI Elemente Zeile 2
    juce::Label refLabel{ {}, "Ref.-Track:" };
    juce::TextEditor refInput;

    // UI Elemente Zeile 3 (NEU: Akkordfolge / Chords)
    juce::Label chordLabel{ {}, "Akkorde:" };
    juce::TextEditor chordInput;

    // UI Elemente Zeile 4
    juce::Label moodTitleLabel{ {}, "Song-Stimmung:" };
    juce::TextButton addMoodButton{ "+ Eigenes Mood" };

    struct MoodButtonInfo
    {
        int moodId = -1;
        juce::String name;
        bool isCustom = false;
        bool isSelected = false;
        std::unique_ptr<juce::TextButton> button;
    };

    std::vector<MoodButtonInfo> moodButtons;

    void loadSongData(int64_t songId);
    void refreshMoodButtons();

    void applyMoodButtonStyle(juce::TextButton& btn, bool isActive, juce::Colour activeBgColour);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongVisionView)
};