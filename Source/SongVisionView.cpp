#pragma execution_character_set("utf-8")

#include "SongVisionView.h"
#include "SettingsManager.h"

// Custom LookAndFeel für fette weiße Schrift + dunklen Body + bunten 3px-Rahmen bei aktiven Moods
class BoldButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return juce::Font(juce::FontOptions(buttonHeight * 0.45f, juce::Font::bold));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour& /*backgroundColour*/,
        bool isMouseOverButton, bool isButtonDown) override
    {
        // Inset, damit der 3px Rahmen nicht am Rand abgeschnitten wird
        auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
        auto cornerSize = 5.0f; // Abrundung der Ecken

        // 1. Immer dunkler Hintergrund
        g.setColour(juce::Colour(0xff181818));
        g.fillRoundedRectangle(bounds, cornerSize);

        // 2. Akzentfarbe aus buttonOnColourId holen
        juce::Colour borderColour = button.findColour(juce::TextButton::buttonOnColourId);

        // Bei Mouseover/Klick Rahmen leicht anpassen
        if (isButtonDown)
            borderColour = borderColour.darker(0.2f);
        else if (isMouseOverButton)
            borderColour = borderColour.brighter(0.2f);

        // 3. Knackiger 3px-Rahmen in der Mood-Farbe!
        g.setColour(borderColour);
        g.drawRoundedRectangle(bounds, cornerSize, 3.0f);
    }
};

static BoldButtonLookAndFeel boldLaf;

SongVisionView::SongVisionView(DatabaseManager& dbManager)
    : db(dbManager)
{
    auto& settings = SettingsManager::getInstance();

    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(bpmInput);
    bpmInput.onFocusLost = [this] { saveCurrentSongData(); };

    addAndMakeVisible(keyLabel);
    addAndMakeVisible(keyInput);
    keyInput.onFocusLost = [this] { saveCurrentSongData(); };

    addAndMakeVisible(genreLabel);
    addAndMakeVisible(genreInput);
    genreInput.onFocusLost = [this] { saveCurrentSongData(); };

    addAndMakeVisible(refLabel);
    addAndMakeVisible(refInput);
    refInput.onFocusLost = [this] { saveCurrentSongData(); };

    // NEU: Akkordfolgen TextEditor Konfiguration (Mehrzeilig & Auto-Save)
    chordLabel.setText(settings.getText("label_chords", "Akkorde:"), juce::dontSendNotification);
    addAndMakeVisible(chordLabel);

    chordInput.setMultiLine(true);
    chordInput.setReturnKeyStartsNewLine(true);
    chordInput.setTabKeyUsedAsCharacter(true);
    chordInput.setScrollbarsShown(true);
    chordInput.setTextToShowWhenEmpty(settings.getText("placeholder_chords", "z.B. Intro: Am | F | C | G\nRefrain: C | G | Am | F"), juce::Colours::grey);
    chordInput.setFont(juce::Font(juce::FontOptions(13.0f)));
    chordInput.onFocusLost = [this] { saveCurrentSongData(); };
    addAndMakeVisible(chordInput);

    addAndMakeVisible(moodTitleLabel);
    addAndMakeVisible(addMoodButton);

    addMoodButton.onClick = [this] {
        if (currentSongId < 0) return;

        auto& settings = SettingsManager::getInstance();

        auto dialogWindow = std::make_shared<juce::AlertWindow>(
            settings.getText("mood_add_title", "Eigenen Mood hinzufügen"),
            settings.getText("mood_add_prompt", "Gib einen Namen für den neuen Mood ein:"),
            juce::AlertWindow::QuestionIcon);

        dialogWindow->addTextEditor("moodInput", "", settings.getText("mood_add_placeholder", "z.B. Cyberpunk"));
        dialogWindow->addButton(settings.getText("mood_add_btn_add", "Hinzufügen"), 1);
        dialogWindow->addButton(settings.getText("mood_add_btn_cancel", "Abbrechen"), 0);

        dialogWindow->enterModalState(true, juce::ModalCallbackFunction::create([this, dialogWindow](int result) {
            if (result == 1)
            {
                juce::String newMood = dialogWindow->getTextEditorContents("moodInput").trim();
                if (newMood.isNotEmpty())
                {
                    db.toggleCustomMoodForSong(currentSongId, newMood, true);
                    refreshMoodButtons();
                }
            }
            }));
        };
}

SongVisionView::~SongVisionView()
{
    for (auto& info : moodButtons)
    {
        if (info.button)
            info.button->setLookAndFeel(nullptr);
    }
}

void SongVisionView::loadSong(int64_t songId)
{
    loadSongData(songId);
}

void SongVisionView::loadSongData(int64_t songId)
{
    currentSongId = songId;
    if (currentSongId < 0) return;

    auto songs = db.getAllSongs();
    for (const auto& s : songs)
    {
        if (s.id == currentSongId)
        {
            bpmInput.setText(juce::String(s.bpm), juce::dontSendNotification);
            keyInput.setText(s.key, juce::dontSendNotification);
            genreInput.setText(s.genre, juce::dontSendNotification);
            refInput.setText(s.referenceTrack, juce::dontSendNotification);
            chordInput.setText(s.chordProgression, juce::dontSendNotification); // NEU: Akkorde laden
            break;
        }
    }

    refreshMoodButtons();
}

void SongVisionView::saveCurrentSongData()
{
    if (currentSongId < 0) return;

    auto songs = db.getAllSongs();
    for (auto s : songs)
    {
        if (s.id == currentSongId)
        {
            s.bpm = bpmInput.getText().getDoubleValue();
            s.key = keyInput.getText();
            s.genre = genreInput.getText();
            s.referenceTrack = refInput.getText();
            s.chordProgression = chordInput.getText(); // NEU: Akkorde speichern

            db.updateSong(s);
            break;
        }
    }
}

void SongVisionView::applyMoodButtonStyle(juce::TextButton& btn, bool isActive, juce::Colour activeBgColour)
{
    if (isActive)
    {
        // AKTIV: Akzentfarbe für den 3px Rahmen speichern
        btn.setColour(juce::TextButton::buttonOnColourId, activeBgColour);

        // Text auf Weiß setzen
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);

        // Custom LookAndFeel aktivieren
        btn.setLookAndFeel(&boldLaf);
    }
    else
    {
        // INAKTIV: Standard-Farben & Standard-Look
        juce::Colour darkOff = juce::Colour(0xff1a1a1a);

        btn.setColour(juce::TextButton::buttonColourId, darkOff);
        btn.setColour(juce::TextButton::buttonOnColourId, darkOff);

        btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffcccccc));
        btn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffcccccc));

        btn.setLookAndFeel(nullptr);
    }

    btn.repaint();
}

void SongVisionView::refreshMoodButtons()
{
    for (auto& info : moodButtons)
    {
        if (info.button)
            info.button->setLookAndFeel(nullptr);
    }

    moodButtons.clear();
    if (currentSongId < 0) return;

    auto& settings = SettingsManager::getInstance();

    // 1. Standard Moods
    auto stdMoods = db.getStandardMoodsFromIni();
    auto activeStdIds = db.getStandardMoodsForSong(currentSongId);

    for (const auto& pair : stdMoods)
    {
        MoodButtonInfo info;
        info.moodId = pair.first;
        info.name = pair.second;
        info.isCustom = false;
        info.isSelected = std::find(activeStdIds.begin(), activeStdIds.end(), pair.first) != activeStdIds.end();

        info.button = std::make_unique<juce::TextButton>(info.name);
        info.button->setToggleState(info.isSelected, juce::dontSendNotification);

        juce::Colour moodColour = settings.getMoodColour(info.moodId);
        applyMoodButtonStyle(*info.button, info.isSelected, moodColour);

        int mId = info.moodId;
        info.button->onClick = [this, mId, btn = info.button.get(), moodColour] {
            bool newState = !btn->getToggleState();
            btn->setToggleState(newState, juce::dontSendNotification);
            db.toggleStandardMoodForSong(currentSongId, mId, newState);
            applyMoodButtonStyle(*btn, newState, moodColour);
            };

        addAndMakeVisible(*info.button);
        moodButtons.push_back(std::move(info));
    }

    // 2. Custom Moods
    auto globalCustomMoods = db.getAllGlobalCustomMoods();
    auto activeCustomMoods = db.getActiveCustomMoodsForSong(currentSongId);

    for (const auto& moodName : globalCustomMoods)
    {
        MoodButtonInfo info;
        info.moodId = -1;
        info.name = moodName + "  ✕";
        info.isCustom = true;
        info.isSelected = std::find(activeCustomMoods.begin(), activeCustomMoods.end(), moodName) != activeCustomMoods.end();

        info.button = std::make_unique<juce::TextButton>(info.name);
        info.button->setToggleState(info.isSelected, juce::dontSendNotification);

        applyMoodButtonStyle(*info.button, info.isSelected, juce::Colours::white);

        juce::String mName = moodName;
        info.button->onClick = [this, mName, btn = info.button.get()] {
            auto mousePos = btn->getMouseXYRelative();

            if (mousePos.x > btn->getWidth() - 25)
            {
                auto& set = SettingsManager::getInstance();
                juce::String promptText = set.getText("mood_delete_prompt", "Möchtest du den Mood \"%s\" wirklich löschen?").replace("%s", mName);

                auto deleteDlg = std::make_shared<juce::AlertWindow>(
                    set.getText("mood_delete_title", "Mood löschen"),
                    promptText,
                    juce::AlertWindow::WarningIcon);

                deleteDlg->addButton(set.getText("mood_delete_btn_yes", "Löschen"), 1);
                deleteDlg->addButton(set.getText("mood_delete_btn_no", "Abbrechen"), 0);

                deleteDlg->enterModalState(true, juce::ModalCallbackFunction::create([this, mName, deleteDlg](int res) {
                    if (res == 1)
                    {
                        db.removeCustomMoodGlobally(mName);
                        refreshMoodButtons();
                    }
                    }));
            }
            else
            {
                bool newState = !btn->getToggleState();
                btn->setToggleState(newState, juce::dontSendNotification);
                db.toggleCustomMoodForSong(currentSongId, mName, newState);
                applyMoodButtonStyle(*btn, newState, juce::Colours::white);
            }
            };

        addAndMakeVisible(*info.button);
        moodButtons.push_back(std::move(info));
    }

    resized();
}

void SongVisionView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff222222));
}

void SongVisionView::resized()
{
    auto area = getLocalBounds().reduced(15);

    // Zeile 1: BPM | Key | Genre
    auto row1 = area.removeFromTop(40);
    int width = row1.getWidth() / 3;

    auto bpmArea = row1.removeFromLeft(width);
    bpmLabel.setBounds(bpmArea.removeFromLeft(50));
    bpmInput.setBounds(bpmArea.reduced(5, 5));

    auto keyArea = row1.removeFromLeft(width);
    keyLabel.setBounds(keyArea.removeFromLeft(80));
    keyInput.setBounds(keyArea.reduced(5, 5));

    auto genreArea = row1;
    genreLabel.setBounds(genreArea.removeFromLeft(60));
    genreInput.setBounds(genreArea.reduced(5, 5));

    area.removeFromTop(10);

    // Zeile 2: Reference Track
    auto row2 = area.removeFromTop(40);
    refLabel.setBounds(row2.removeFromLeft(140));
    refInput.setBounds(row2.reduced(5, 5));

    area.removeFromTop(10);

    // Zeile 3: Akkordfolge / Chords (NEU: Mehrzeiliger Block mit 80px Höhe)
    auto chordArea = area.removeFromTop(80);
    chordLabel.setBounds(chordArea.removeFromLeft(140).removeFromTop(25));
    chordInput.setBounds(chordArea.reduced(5, 2));

    area.removeFromTop(15);

    // Zeile 4: Mood-Grid Header
    auto moodHeader = area.removeFromTop(30);
    moodTitleLabel.setBounds(moodHeader.removeFromLeft(150));
    addMoodButton.setBounds(moodHeader.removeFromLeft(120));

    area.removeFromTop(10);

    // Dynamic Tag-Grid
    int x = area.getX();
    int y = area.getY();
    int buttonHeight = 30;
    int spacing = 8;

    for (auto& info : moodButtons)
    {
        juce::Font btnFont = info.isSelected ? juce::Font(juce::FontOptions(14.0f, juce::Font::bold))
            : juce::Font(juce::FontOptions(14.0f, juce::Font::plain));

        int btnWidth = juce::GlyphArrangement::getStringWidthInt(btnFont, info.name) + 28;

        if (x + btnWidth > area.getRight())
        {
            x = area.getX();
            y += buttonHeight + spacing;
        }

        info.button->setBounds(x, y, btnWidth, buttonHeight);
        x += btnWidth + spacing;
    }
}