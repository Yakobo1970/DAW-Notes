#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include "TrackAnalyser.h"

// Richtwerte eines Genres. Die Werte sind Erfahrungswerte aus kommerziellen Produktionen
// und als Orientierung gedacht, nicht als feste Regel.
struct GenreProfile
{
    juce::String name;
    juce::StringArray aliases;          // Kleingeschriebene Suchbegriffe für das Genre-Feld der Song Vision

    double bpmMin = 60.0, bpmMax = 200.0;
    double lufsMin = -14.0, lufsMax = -7.0;
    double lraMin = 3.0, lraMax = 10.0;
    double plrMin = 6.0, plrMax = 14.0; // Peak-to-Loudness-Ratio (Dynamik)
    double widthMin = -14.0, widthMax = -5.0; // Side zu Mid in dB
    double lowCorrelationMin = 0.8;     // Bass sollte für Clubs/Mono weitgehend mono sein
    std::array<double, AnalysisMetrics::numBands> bandTargets{};
    double bandTolerance = 3.5;
};

struct AnalysisFinding
{
    enum class Status { Good, Warning, Problem, Info };

    Status status = Status::Info;
    juce::String label;
    juce::String value;
    juce::String target;
    juce::String advice;
};

struct GenreEvaluation
{
    int fitPercent = 0;
    std::vector<AnalysisFinding> findings;
};

namespace GenreAnalysis
{
    const std::vector<GenreProfile>& getProfiles();

    // Index des passenden Profils zum freien Genre-Text (Fallback: "Allgemein")
    int findProfileIndex(const juce::String& genreText);

    GenreEvaluation evaluate(const AnalysisMetrics& metrics, const GenreProfile& profile,
                             double songVisionBpm, const juce::String& songVisionKey);

    juce::String getBandName(int band);

    // Passung zu allen Genre-Profilen (ohne "Allgemein"), absteigend sortiert: {Profil-Index, Prozent}
    std::vector<std::pair<int, int>> rankProfiles(const AnalysisMetrics& metrics);

    // Vorschläge aus den Standard-Moods der settings.ini (IDs 1-18), höchstens maxMoods
    std::vector<int> suggestMoodIds(const AnalysisMetrics& metrics, int maxMoods = 4);

    // Allgemeine Diagnose für die Song Vision: Referenztrack und/oder eigener Song (beide optional)
    juce::String describe(const AnalysisMetrics* reference, const juce::String& referenceName,
                          const AnalysisMetrics* ownSong, const juce::String& genreText);
}
