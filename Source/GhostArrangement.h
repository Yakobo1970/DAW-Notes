#pragma once

#include <JuceHeader.h>
#include <vector>

// Grobes Ghost-Arrangement aus einem Referenztrack: Abschnitte (als Marker) und
// je Klanggruppe eine benannte MIDI-Spur mit stummen Platzhalter-Blöcken.
struct GhostArrangement
{
    struct Section
    {
        juce::String name;
        int startBar = 0, endBar = 0; // endBar exklusiv, 0-basiert
    };

    struct Element
    {
        juce::String name;
        std::vector<std::pair<int, int>> regions; // aktive Bereiche in Takten [start, end)
    };

    double bpm = 120.0;
    int totalBars = 0;
    double gridOffsetSeconds = 0.0; // Zeitpunkt von Takt 1 im Referenztrack
    std::vector<Section> sections;
    std::vector<Element> elements;

    // Analysiert die Datei. bpm <= 0: Tempo wird vorher geschätzt.
    static bool build(const juce::File& file, double bpm, GhostArrangement& out, juce::String& error,
                      const std::function<bool(double)>& progress);

    // Schreibt eine Typ-1-MIDI-Datei. placeholders = false: nur Marker und Spurnamen.
    bool writeMidi(const juce::File& target, bool placeholders) const;

    // Lesbare Zusammenfassung für den Dialog
    juce::String describe() const;
};
