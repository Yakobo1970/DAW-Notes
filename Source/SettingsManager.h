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
        if (uiTexts.containsKey(key))
            return uiTexts.getValue(key);

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
        juce::File file = getIniFile();
        if (!file.existsAsFile())
        {
            const char* rawIniData =
                "[Moods]\n"
                "1=Energetisch\n"
                "2=Melancholisch\n"
                "3=Düster\n"
                "4=Fröhlich\n"
                "5=Entspannt\n"
                "6=Aggressiv\n"
                "7=Treibend\n"
                "8=Atmosphärisch\n"
                "9=Gefühlvoll\n"
                "10=Hypnotisch\n"
                "11=Nostalgisch\n"
                "12=Euphorisierend\n"
                "13=Dramatisch\n"
                "14=Mystisch\n"
                "15=Groovig\n"
                "16=Ruhig\n"
                "17=Monoton\n"
                "18=Verspielt\n\n"
                "[MoodColours]\n"
                "1=#FF007F\n"
                "2=#00FF66\n"
                "3=#00E5FF\n"
                "4=#FFD700\n"
                "5=#FF4500\n"
                "6=#A020F0\n"
                "7=#FF1493\n"
                "8=#00FF00\n"
                "9=#00FFFF\n"
                "10=#FF00FF\n"
                "11=#39FF14\n"
                "12=#FF3131\n"
                "13=#F5F5F5\n"
                "14=#00F5FF\n"
                "15=#FF69B4\n"
                "16=#FFF01F\n"
                "17=#7B68EE\n"
                "18=#FF8C00\n\n"
                "[UI]\n"
                "btn_add_song=+ Neuer Song\n"
                "btn_delete_song=Song löschen\n"
                "btn_rename_song=Umbenennen\n"
                "tab_library=Library\n"
                "tab_vision=Song Vision\n"
                "tab_session_todos=Session ToDos\n"
                "tab_release_todos=Release ToDos\n"
                "col_id=ID\n"
                "col_title=Titel\n"
                "col_bpm=BPM\n"
                "col_key=Tonart\n"
                "col_genre=Genre\n"
                "col_created=Erstellt am\n"
                "dialog_rename_title=Song umbenennen\n"
                "dialog_rename_prompt=Gib einen neuen Titel ein:\n"
                "dialog_rename_placeholder=Neuer Titel...\n"
                "dialog_delete_title=Song löschen\n"
                "dialog_delete_confirm=Möchtest du '%s' wirklich unwiderruflich löschen?\n"
                "dialog_delete_notice_title=Hinweis\n"
                "dialog_delete_notice_msg=Bitte wähle zuerst einen Song aus der Liste aus, den du löschen möchtest.\n"
                "ctx_load_open=Song laden & öffnen\n"
                "btn_save=Speichern\n"
                "btn_cancel=Abbrechen\n"
                "btn_yes_delete=Ja, löschen\n"
                "header_no_song=Kein Song geladen – Bitte erst einen Song in der Library wählen\n"
                "header_active_song=Aktiver Song: %s\n"
                "mood_add_title=Eigenen Mood hinzufügen\n"
                "mood_add_prompt=Gib einen Namen für den neuen Mood ein:\n"
                "mood_add_placeholder=z.B. Cyberpunk\n"
                "mood_add_btn_add=Hinzufügen\n"
                "mood_add_btn_cancel=Abbrechen\n"
                "mood_delete_title=Mood löschen\n"
                "mood_delete_prompt=Möchtest du den Mood \"%s\" wirklich unwiderruflich löschen?\n"
                "mood_delete_btn_yes=Löschen\n"
                "mood_delete_btn_no=Abbrechen\n"
                "col_done=Done\n"
                "col_pin=Pin\n"
                "col_task=Aufgabe\n"
                "col_date=Erstellt am\n"
                "todo_delete_title=Aufgabe löschen\n"
                "todo_delete_prompt=Möchtest du die Aufgabe \"%s\" wirklich löschen?\n"
                "todo_unpin_title=Globalen Pin entfernen\n"
                "todo_unpin_prompt=Möchtest du den Pin entfernen? Die Aufgabe wird dadurch aus anderen Songs entfernt.\n"
                "help_library=LIBRARY:\\n• Wähle einen Song aus der Liste, um ihn zu bearbeiten.\\n• Nutze 'Neuer Song', um ein neues Projekt hinzuzufügen.\n"
                "help_vision=SONG VISION:\\n• Halte deine Ideen, Stimmung und Referenztracks fest.\\n• Nutze Keywords/Moods zur Kategorisierung.\n"
                "help_session_todos=SESSION TO-DOS:\\n• Aufgaben für die aktuelle Recording-/Mixing-Session.\\n• Mit 'Pin' bleiben ToDos für ALLE Songs sichtbar.\n"
                "help_release_todos=RELEASE TO-DOS:\\n• Checkliste für Master, Artwork, Distribution & Promotion.\\n• Klicke auf die Spaltenköpfe, um zu sortieren.\n";

            juce::String defaultIniContent = juce::String::fromUTF8(rawIniData);
            file.replaceWithText(defaultIniContent, false, false, "UTF-8");
        }
    }

    void loadSettings()
    {
        juce::File file = getIniFile();
        if (!file.existsAsFile()) return;

        moods.clear();
        moodColours.clear();

        juce::String fileContent = file.loadFileAsString();
        juce::StringArray lines;
        lines.addLines(fileContent);

        juce::String currentSection = "";

        for (auto line : lines)
        {
            line = line.trim();
            if (line.isEmpty() || line.startsWith(";")) continue;

            if (line.startsWith("[") && line.endsWith("]"))
            {
                currentSection = line.substring(1, line.length() - 1);
                continue;
            }

            int equalsIdx = line.indexOf("=");
            if (equalsIdx > 0)
            {
                juce::String key = line.substring(0, equalsIdx).trim();
                juce::String value = line.substring(equalsIdx + 1).trim();

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