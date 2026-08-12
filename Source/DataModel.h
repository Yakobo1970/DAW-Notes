#pragma once
#include <JuceHeader.h>

enum class ToDoType
{
    Session,
    Release
};

struct ToDoItem
{
    int64_t id = -1;
    int64_t songId = -1;
    juce::String text;
    bool isPermanent = false;
    bool isDone = false;
    juce::Time createdAt;
    juce::Time completedAt;
    ToDoType type = ToDoType::Session;
};

struct Song
{
    int64_t id = -1;
    juce::String title = "Unbenannter Song";
    double bpm = 120.0;
    juce::String key = "C Major";
    juce::String genre = "Pop";
    juce::String referenceTrack = "";
    juce::String chordProgression = ""; // <-- NEU: Akkordfolge / Chords
    juce::Time createdAt = juce::Time::getCurrentTime();

    std::vector<int> standardMoodIds;
    juce::StringArray customMoods;
};