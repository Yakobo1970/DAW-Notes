#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "DatabaseManager.h"
#include "TrackAnalyser.h"
#include "GhostArrangement.h"
#include "ReferencePlayerControls.h"

class SongVisionView : public juce::Component, private juce::Timer
{
public:
    SongVisionView(DatabaseManager& dbManager, ReferencePlayer& referencePlayer);
    ~SongVisionView() override;

    void loadSong(int64_t songId);
    void saveCurrentSongData();

    // Diagnose neu aufbauen (z.B. nach einer neuen Analyse im Reiter "Analyse")
    void refreshDiagnosis();

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
    juce::TextButton refAnalyseButton;

    // Referenztrack anhören (Play/Pause/Stop)
    ReferencePlayer& player;
    ReferencePlayerControls playerControls{ player };
    void chooseFileForListening();
    void syncPlayerWithSong(const juce::File& referenceFile);

    // Diagnose unter den Moods
    juce::TextEditor diagnosisBox;

    // Analyse eines Referenztracks im Hintergrund
    struct ReferenceJob : public juce::Thread
    {
        ReferenceJob(const juce::File& f, int64_t song) : juce::Thread("DAW Notes Referenz"), file(f), songId(song) {}
        void run() override
        {
            ok = TrackAnalyser::analyseFile(file, metrics, error, [this](double p) { progress = p; return !threadShouldExit(); });
            finished = true;
        }

        juce::File file;
        int64_t songId;
        AnalysisMetrics metrics;
        juce::String error;
        bool ok = false;
        std::atomic<double> progress{ 0.0 };
        std::atomic<bool> finished{ false };
    };
    std::unique_ptr<ReferenceJob> referenceJob;
    std::unique_ptr<juce::FileChooser> fileChooser;

    void chooseReferenceFile();
    void timerCallback() override;
    void onReferenceAnalysed(ReferenceJob& job);
    void applyReferenceResult(const AnalysisMetrics& m, const juce::String& fileName);

    // Ghost-Arrangement aus dem Referenztrack (MIDI-Datei mit Markern und benannten Spuren)
    struct DragFileButton : public juce::TextButton
    {
        juce::File fileToDrag;
        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (fileToDrag.existsAsFile() && e.getDistanceFromDragStart() > 8)
                juce::DragAndDropContainer::performExternalDragDropOfFiles({ fileToDrag.getFullPathName() }, false, this);
            else
                juce::TextButton::mouseDrag(e);
        }
    };

    struct GhostJob : public juce::Thread
    {
        GhostJob(const juce::File& f, double tempo) : juce::Thread("DAW Notes Ghost"), file(f), bpm(tempo) {}
        void run() override
        {
            ok = GhostArrangement::build(file, bpm, arrangement, error, [this](double p) { progress = p; return !threadShouldExit(); });
            finished = true;
        }

        juce::File file;
        double bpm;
        GhostArrangement arrangement;
        juce::String error;
        bool ok = false;
        std::atomic<double> progress{ 0.0 };
        std::atomic<bool> finished{ false };
    };

    DragFileButton ghostButton;
    std::unique_ptr<GhostJob> ghostJob;
    juce::File lastReferenceFile;
    double lastReferenceBpm = 0.0;

    void onGhostClicked();
    void startGhostJob(const juce::File& referenceFile);
    void onGhostBuilt(GhostJob& job);
    void resetGhostButton();

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