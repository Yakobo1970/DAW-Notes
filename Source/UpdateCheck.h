#pragma once

#include <JuceHeader.h>

// Sucht höchstens einmal am Tag im Hintergrund nach dem neuesten GitHub-Release.
// Sendet nur die Anfrage selbst, lässt sich abschalten. Von allen Editoren des Prozesses geteilt
// (juce::SharedResourcePointer<UpdateCheck>). Muster aus Spurwerk / Midi-Fundus.
class UpdateCheck : private juce::Thread
{
public:
    static constexpr const char* kApiUrl = "https://api.github.com/repos/Yakobo1970/DAW-Notes/releases/latest";
    static constexpr const char* kReleasesUrl = "https://github.com/Yakobo1970/DAW-Notes/releases/latest";

    UpdateCheck();
    ~UpdateCheck() override;

    bool enabled() const;
    void setEnabled(bool on);             // Einschalten sucht sofort
    juce::String newerVersion() const;    // neueres Release ("2.0.1"), leer wenn keins / abgeschaltet
    void dismiss();                       // Hinweis bis zum nächsten Start ausblenden

    // "v2.0.10" > "2.0.9"; nicht-numerische Teile zählen als 0
    static bool isNewer(const juce::String& candidate, const juce::String& current);
    // blockierende Abfrage; liefert den Tag ohne "v", leer bei Fehler
    static juce::String fetchLatest(int timeoutMs, std::function<bool()> keepGoing = {});

private:
    void run() override;
    void startIfDue(bool force);
    std::unique_ptr<juce::PropertiesFile> props;
    juce::String latest, testVersion;
    bool dismissed = false;
    mutable juce::CriticalSection lock;
};
