#pragma once

#include <JuceHeader.h>
#include <vector>
#include <utility>
#include <map>

class SettingsManager
{
public:
    SettingsManager()
    {
        ensureIniFileExists();
        loadSettings();
    }

    static SettingsManager& getInstance()
    {
        static SettingsManager instance;
        return instance;
    }

    std::vector<std::pair<int, juce::String>> getStandardMoods() const
    {
        return moods;
    }

    juce::String getText(const juce::String& key, const juce::String& fallbackText) const
    {
        // In der .ini stehen Zeilenumbrüche als "\n"
        if (uiTexts.containsKey(key))
            return uiTexts.getValue(key).replace("\\n", "\n");

        return fallbackText;
    }

    juce::Colour getMoodColour(int moodId)
    {
        juce::String hexString = "";

        // 1. Suche nach der Farbe aus der geladenen moodColours Map
        if (moodColours.find(moodId) != moodColours.end())
        {
            hexString = moodColours[moodId].trim();
        }

        // Schneide ein eventuelles '#' am Anfang ab
        if (hexString.startsWith("#"))
            hexString = hexString.substring(1);

        // Nimmt die letzten 6 Zeichen (z. B. "A020F0")
        hexString = hexString.getLastCharacters(6).trim();

        // Fallback-Farben, falls die .ini mal leer ist
        if (hexString.isEmpty())
        {
            switch (moodId)
            {
            case 1: return juce::Colour(0xffff0055); // Neon Pink
            case 2: return juce::Colour(0xff00d2ff); // Neon Blau
            case 3: return juce::Colour(0xffffa500); // Orange
            default: return juce::Colours::cyan;
            }
        }

        // Erzwinge immer 100% Deckkraft (0xFF...) am Anfang!
        return juce::Colour::fromString("FF" + hexString);
    }

private:
    std::vector<std::pair<int, juce::String>> moods;
    std::map<int, juce::String> moodColours;
    juce::PropertySet uiTexts;

    juce::File getIniFile() const
    {
        // Auf "DAW Notes" angepasst
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("DAW Notes");
        if (!dir.exists()) dir.createDirectory();
        return dir.getChildFile("settings.ini");
    }

    void ensureIniFileExists()
    {
        // Ohne settings.ini (z.B. Plugin ohne Setup kopiert) die mitgelieferte deutsche Sprachdatei anlegen.
        // Das Setup legt je nach gewählter Sprache set_german/english/france.ini als settings.ini ab.
        juce::File file = getIniFile();
        if (!file.existsAsFile())
            file.replaceWithData(BinaryData::set_german_ini, (size_t)BinaryData::set_german_iniSize);
    }

public:
    // Lädt eine andere Sprachdatei (z.B. Resources/Languages/set_english.ini), ohne settings.ini zu ändern
    void loadLanguageFile(const juce::File& file)
    {
        loadSettings(file);
    }

private:
    void loadSettings(juce::File file = {})
    {
        if (file == juce::File())
            file = getIniFile();
        if (!file.existsAsFile()) return;

        moods.clear();
        moodColours.clear();
        uiTexts.clear();

        juce::String fileContent = file.loadFileAsString();
        juce::StringArray lines;
        lines.addLines(fileContent);

        juce::String currentSection = "";

        for (auto rawLine : lines)
        {
            // Werte nicht trimmen: führende/abschließende Leerzeichen gehören teils zum Text (z.B. " bis ")
            juce::String line = rawLine.trim();
            if (line.isEmpty() || line.startsWith(";")) continue;

            if (line.startsWith("[") && line.endsWith("]"))
            {
                currentSection = line.substring(1, line.length() - 1);
                continue;
            }

            int equalsIdx = rawLine.indexOf("=");
            if (equalsIdx > 0)
            {
                juce::String key = rawLine.substring(0, equalsIdx).trim();
                juce::String value = rawLine.substring(equalsIdx + 1).trimCharactersAtEnd("\r\n");
                if (currentSection != "UI") value = value.trim();

                if (currentSection == "Moods")
                    moods.push_back({ key.getIntValue(), value });
                else if (currentSection == "MoodColours")
                    moodColours[key.getIntValue()] = value;
                else if (currentSection == "UI")
                    uiTexts.setValue(key, value);
            }
        }
    }
};

// Kurzform für übersetzbare Texte: Schlüssel aus [UI] der settings.ini, sonst deutscher Standardtext (UTF-8).
// Tools/extract_texts.py sammelt alle tr()-Aufrufe für die Sprachdateien unter Resources/Languages.
inline juce::String tr(const char* key, const char* germanFallbackUtf8)
{
    return SettingsManager::getInstance().getText(key, juce::String::fromUTF8(germanFallbackUtf8));
}