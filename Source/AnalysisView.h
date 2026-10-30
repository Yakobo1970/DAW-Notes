#pragma once

#include <JuceHeader.h>
#include "DatabaseManager.h"
#include "TrackAnalyser.h"
#include "GenreAnalysis.h"

class AnalysisView : public juce::Component,
                     private juce::ListBoxModel,
                     private juce::Timer
{
public:
    AnalysisView(DatabaseManager& dbManager, TrackAnalyser& trackAnalyser, std::function<void()> onToDosAdded);
    ~AnalysisView() override;

    // Lädt Song-Daten (Genre, BPM, Tonart) und die letzte gespeicherte Analyse
    void loadSong(int64_t songId);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    void timerCallback() override;

    void updateControls();
    void reevaluate();
    void addFindingsAsToDos();

    void paintStatusPanel(juce::Graphics& g, juce::Rectangle<int> area);
    void paintInstructions(juce::Graphics& g, juce::Rectangle<int> area);

    DatabaseManager& db;
    TrackAnalyser& analyser;
    std::function<void()> toDosAddedCallback;

    int64_t currentSongId = -1;
    juce::String songGenre, songKey;
    double songBpm = 0.0;

    bool hasResult = false;
    AnalysisMetrics metrics;
    juce::Time analysedAt;
    GenreEvaluation evaluation;

    TrackAnalyser::State lastState = TrackAnalyser::State::Idle;
    juce::String statusHint;

    juce::Label profileLabel{ {}, "Genre-Profil:" };
    juce::ComboBox profileBox;
    juce::Label profileSourceLabel;
    juce::TextButton analyseButton;
    juce::TextButton cancelButton;
    juce::TextButton toDoButton;
    juce::ListBox findingsList{ {}, this };

    juce::Rectangle<int> statusArea, instructionArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisView)
};
