#pragma execution_character_set("utf-8")

#include "SongVisionView.h"
#include "SettingsManager.h"
#include "GenreAnalysis.h"

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

SongVisionView::SongVisionView(DatabaseManager& dbManager, ReferencePlayer& referencePlayer)
    : db(dbManager), player(referencePlayer)
{
    auto& settings = SettingsManager::getInstance();

    bpmLabel.setText(tr("label_bpm", "BPM:"), juce::dontSendNotification);
    keyLabel.setText(tr("label_key", "Tonart:"), juce::dontSendNotification);
    genreLabel.setText(tr("label_genre", "Genre:"), juce::dontSendNotification);
    refLabel.setText(tr("label_reference", "Ref.-Track:"), juce::dontSendNotification);
    moodTitleLabel.setText(tr("label_moods", "Song-Stimmung:"), juce::dontSendNotification);
    addMoodButton.setButtonText(tr("btn_add_mood", "+ Eigenes Mood"));

    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(bpmInput);
    bpmInput.onFocusLost = [this] { saveCurrentSongData(); };

    addAndMakeVisible(keyLabel);
    addAndMakeVisible(keyInput);
    keyInput.onFocusLost = [this] { saveCurrentSongData(); };

    addAndMakeVisible(genreLabel);
    addAndMakeVisible(genreInput);
    genreInput.onFocusLost = [this] { saveCurrentSongData(); refreshDiagnosis(); };

    addAndMakeVisible(refLabel);
    addAndMakeVisible(refInput);
    refInput.onFocusLost = [this] { saveCurrentSongData(); };

    refAnalyseButton.setButtonText(settings.getText("ref_analyse_btn", juce::String::fromUTF8("Analysieren …")));
    refAnalyseButton.setTooltip(tr("vision_referenztrack_laden_wav_mp3", "Referenztrack laden (WAV, MP3, FLAC, AIFF, OGG) und BPM, Tonart, Akkorde und Moods erkennen"));
    refAnalyseButton.onClick = [this] {
        if (referenceJob != nullptr) { referenceJob->signalThreadShouldExit(); return; } // zweiter Klick = abbrechen
        chooseReferenceFile();
    };
    addAndMakeVisible(refAnalyseButton);

    playerControls.onNeedFile = [this] { chooseFileForListening(); };
    addAndMakeVisible(playerControls);

    resetGhostButton();
    ghostButton.setTooltip(tr("ghost_tooltip", "Grobes Ghost-Arrangement aus dem Referenztrack: Abschnitte als Marker, "
                                               "Kick/Bass/Snare/Hats/Pads/Lead als benannte MIDI-Spuren. Fertige Datei hierher ziehen."));
    ghostButton.onClick = [this] { onGhostClicked(); };
    addAndMakeVisible(ghostButton);

    diagnosisBox.setMultiLine(true);
    diagnosisBox.setReadOnly(true);
    diagnosisBox.setCaretVisible(false);
    diagnosisBox.setScrollbarsShown(true);
    diagnosisBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff181818));
    diagnosisBox.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff333333));
    diagnosisBox.setColour(juce::TextEditor::textColourId, juce::Colour(0xffbbbbbb));
    diagnosisBox.setFont(juce::Font(juce::FontOptions(12.5f)));
    addAndMakeVisible(diagnosisBox);

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
    stopTimer();
    if (referenceJob != nullptr)
        referenceJob->stopThread(5000);
    if (ghostJob != nullptr)
        ghostJob->stopThread(5000);

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

    // Gespeicherten Referenztrack des Songs für Player und Ghost-Arrangement bereitstellen
    juce::File referenceFile(db.loadReferencePath(currentSongId));
    if (referenceFile.existsAsFile())
    {
        juce::String refName;
        juce::Time when;
        lastReferenceFile = referenceFile;
        lastReferenceBpm = AnalysisMetrics::fromVar(juce::JSON::parse(db.loadReference(currentSongId, refName, when))).tempoBpm;
    }
    syncPlayerWithSong(referenceFile.existsAsFile() ? referenceFile : juce::File());

    refreshMoodButtons();
    refreshDiagnosis();
}

void SongVisionView::syncPlayerWithSong(const juce::File& referenceFile)
{
    // Beim Songwechsel nicht den Referenztrack des vorherigen Songs weiterspielen
    if (player.getFile() == referenceFile)
        return;

    player.stop();
    if (referenceFile.existsAsFile())
        player.load(referenceFile);
    else
        player.unload();
    playerControls.refresh();
}

void SongVisionView::chooseFileForListening()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        tr("player_choose", "Referenztrack zum Anhören wählen …"),
        lastReferenceFile.existsAsFile() ? lastReferenceFile.getParentDirectory() : juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg;*.m4a;*.wma");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            const auto file = fc.getResult();
            if (!file.existsAsFile()) return;

            if (!player.load(file))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    tr("ref_error_title", "Referenztrack"), tr("ta_die_datei_konnte_nicht", "Die Datei konnte nicht gelesen werden (Format nicht unterstützt?)."));
                return;
            }
            player.play();
            playerControls.refresh();
        });
}

void SongVisionView::refreshDiagnosis()
{
    if (currentSongId < 0) { diagnosisBox.clear(); return; }

    juce::Time when;
    juce::String refName;
    std::unique_ptr<AnalysisMetrics> reference, own;

    const auto refJson = db.loadReference(currentSongId, refName, when);
    if (refJson.isNotEmpty())
        reference = std::make_unique<AnalysisMetrics>(AnalysisMetrics::fromVar(juce::JSON::parse(refJson)));

    const auto ownJson = db.loadAnalysis(currentSongId, when);
    if (ownJson.isNotEmpty())
        own = std::make_unique<AnalysisMetrics>(AnalysisMetrics::fromVar(juce::JSON::parse(ownJson)));

    diagnosisBox.setText(GenreAnalysis::describe(reference.get(), refName, own.get(), genreInput.getText()), juce::dontSendNotification);
}

void SongVisionView::resetGhostButton()
{
    ghostButton.setButtonText(ghostButton.fileToDrag.existsAsFile()
        ? tr("vision_ghost_midi_ziehen", "⇩ Ghost-MIDI ziehen")
        : SettingsManager::getInstance().getText("ghost_btn", juce::String::fromUTF8("Ghost-Arrangement")));
}

void SongVisionView::onGhostClicked()
{
    if (currentSongId < 0) return;
    if (ghostJob != nullptr) { ghostJob->signalThreadShouldExit(); return; } // zweiter Klick = abbrechen

    juce::PopupMenu menu;
    if (lastReferenceFile.existsAsFile())
        menu.addItem(1, tr("vision_aus", "Aus \"") + lastReferenceFile.getFileName() + tr("vision_erzeugen", "\" erzeugen"));
    menu.addItem(2, tr("vision_referenztrack_waehlen_und_erzeugen", "Referenztrack wählen und erzeugen …"));
    if (ghostButton.fileToDrag.existsAsFile())
    {
        menu.addSeparator();
        menu.addItem(3, tr("vision_letzte_ghost_midi_im", "Letzte Ghost-MIDI im Explorer zeigen"));
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&ghostButton), [this](int choice) {
        if (choice == 1) startGhostJob(lastReferenceFile);
        else if (choice == 3) ghostButton.fileToDrag.revealToUser();
        else if (choice == 2)
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                SettingsManager::getInstance().getText("ref_choose_title", "Referenztrack laden"),
                juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg;*.m4a;*.wma");
            fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc) {
                    if (fc.getResult().existsAsFile()) startGhostJob(fc.getResult());
                });
        }
    });
}

void SongVisionView::startGhostJob(const juce::File& referenceFile)
{
    if (ghostJob != nullptr) return;

    // Tempo der letzten Referenzanalyse wiederverwenden, sonst schätzt der Job selbst
    const double bpm = (referenceFile == lastReferenceFile) ? lastReferenceBpm : 0.0;
    ghostJob = std::make_unique<GhostJob>(referenceFile, bpm);
    ghostJob->startThread(juce::Thread::Priority::low);
    startTimerHz(10);
}

void SongVisionView::onGhostBuilt(GhostJob& job)
{
    if (!job.ok)
    {
        if (job.error != "Abgebrochen.")
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, tr("ghost_title", "Ghost-Arrangement"), job.error);
        return;
    }

    if (job.file != lastReferenceFile)
    {
        lastReferenceFile = job.file;
        lastReferenceBpm = job.arrangement.bpm;
    }

    auto dlg = std::make_shared<juce::AlertWindow>(tr("ghost_title", "Ghost-Arrangement"),
        job.arrangement.describe() + tr("ghost_save_question",
            "\n\nAls MIDI-Datei speichern? Danach den Button in die DAW ziehen.\n"
            "Platzhalter = stumme Noten (C-2, Velocity 1) markieren, wo ein Element spielt."),
        juce::AlertWindow::InfoIcon);
    dlg->addButton(tr("vision_mit_platzhaltern", "Mit Platzhaltern"), 1);
    dlg->addButton(tr("vision_nur_marker_spuren", "Nur Marker + Spuren"), 2);
    dlg->addButton(tr("vision_abbrechen", "Abbrechen"), 0);

    const auto arrangement = job.arrangement;
    const auto refName = job.file.getFileNameWithoutExtension();
    dlg->enterModalState(true, juce::ModalCallbackFunction::create([this, dlg, arrangement, refName](int result) {
        if (result == 0) return;

        const auto target = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW Notes").getChildFile("Ghost Arrangements")
            .getChildFile(juce::File::createLegalFileName(refName + " - Ghost.mid"));

        if (!arrangement.writeMidi(target, result == 1))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, tr("ghost_title", "Ghost-Arrangement"),
                tr("vision_die_midi_datei_konnte", "Die MIDI-Datei konnte nicht geschrieben werden:\n") + target.getFullPathName());
            return;
        }

        ghostButton.fileToDrag = target;
        resetGhostButton();
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, tr("ghost_title", "Ghost-Arrangement"),
            tr("vision_gespeichert", "Gespeichert:\n") + target.getFullPathName()
            + tr("ghost_saved_hint", "\n\nZieh jetzt den Button \"⇩ Ghost-MIDI ziehen\" ins Arrangement deiner DAW "
                                     "(bei Takt 1). Den Referenztrack so anlegen, dass sein Takt 1 dort liegt."));
    }));
}

void SongVisionView::chooseReferenceFile()
{
    if (currentSongId < 0) return;

    fileChooser = std::make_unique<juce::FileChooser>(
        SettingsManager::getInstance().getText("ref_choose_title", "Referenztrack laden"),
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg;*.m4a;*.wma");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            const auto file = fc.getResult();
            if (!file.existsAsFile() || referenceJob != nullptr) return;

            referenceJob = std::make_unique<ReferenceJob>(file, currentSongId);
            referenceJob->startThread(juce::Thread::Priority::low);
            startTimerHz(10);
        });
}

void SongVisionView::timerCallback()
{
    if (ghostJob != nullptr)
    {
        if (!ghostJob->finished.load())
        {
            ghostButton.setButtonText(tr("vision_ghost", "Ghost … ") + juce::String((int)(ghostJob->progress.load() * 100.0)) + " %");
            return;
        }

        ghostJob->stopThread(1000);
        auto job = std::move(ghostJob);
        resetGhostButton();
        onGhostBuilt(*job);
    }

    if (referenceJob == nullptr) { if (ghostJob == nullptr) stopTimer(); return; }

    if (!referenceJob->finished.load())
    {
        refAnalyseButton.setButtonText(tr("vision_analysiere", "Analysiere … ")
            + juce::String((int)(referenceJob->progress.load() * 100.0)) + juce::String::fromUTF8(" %"));
        return;
    }

    if (ghostJob == nullptr) stopTimer();
    referenceJob->stopThread(1000);
    auto job = std::move(referenceJob);
    refAnalyseButton.setButtonText(SettingsManager::getInstance().getText("ref_analyse_btn", juce::String::fromUTF8("Analysieren …")));
    onReferenceAnalysed(*job);
}

void SongVisionView::onReferenceAnalysed(ReferenceJob& job)
{
    if (!job.ok)
    {
        if (job.error != "Abgebrochen.")
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                SettingsManager::getInstance().getText("ref_error_title", "Referenztrack"), job.error);
        return;
    }

    const auto fileName = job.file.getFileNameWithoutExtension();
    db.saveReference(job.songId, fileName, juce::JSON::toString(job.metrics.toVar(), true), job.file.getFullPathName());

    // Für das Ghost-Arrangement merken (spart die erneute Dateiauswahl und Tempo-Schätzung)
    lastReferenceFile = job.file;
    lastReferenceBpm = job.metrics.tempoBpm;

    if (job.songId != currentSongId) return; // Inzwischen anderer Song aktiv - nur speichern

    // Analysierten Track direkt zum Anhören bereitstellen (laufende Wiedergabe nicht unterbrechen)
    if (!player.isPlaying())
    {
        player.load(job.file);
        playerControls.refresh();
    }

    refreshDiagnosis();

    // Gefundene Werte zur Übernahme anbieten
    juce::StringArray moodNames;
    const auto stdMoods = db.getStandardMoodsFromIni();
    for (auto id : GenreAnalysis::suggestMoodIds(job.metrics))
        for (const auto& pair : stdMoods)
            if (pair.first == id) moodNames.add(pair.second);

    juce::String summary;
    summary << tr("vision_erkannt_in", "Erkannt in \"") << fileName << "\":\n\n"
            << tr("ref_result_tempo", "Tempo:  ") << (job.metrics.tempoBpm > 0.0 ? juce::String(job.metrics.tempoBpm, 1) + " BPM" : juce::String::fromUTF8("–")) << "\n"
            << tr("ref_result_key", "Tonart:  ") << (job.metrics.detectedKey.isNotEmpty() ? job.metrics.detectedKey : juce::String::fromUTF8("–")) << "\n"
            << tr("ref_result_chords", "Akkorde:  ") << (job.metrics.chordProgression.isNotEmpty() ? job.metrics.chordProgression : juce::String::fromUTF8("–")) << "\n"
            << tr("ref_result_moods", "Moods:  ") << (moodNames.isEmpty() ? juce::String::fromUTF8("–") : moodNames.joinIntoString(", ")) << "\n\n"
            << tr("vision_in_die_song_vision", "In die Song Vision übernehmen? Vorhandene Akkord-Notizen bleiben erhalten, Moods werden nur ergänzt.");

    auto dlg = std::make_shared<juce::AlertWindow>(
        SettingsManager::getInstance().getText("ref_result_title", "Referenztrack analysiert"), summary, juce::AlertWindow::InfoIcon);
    dlg->addButton(tr("vision_uebernehmen", "Übernehmen"), 1);
    dlg->addButton(tr("vision_nur_diagnose", "Nur Diagnose"), 0);

    const auto metrics = job.metrics;
    dlg->enterModalState(true, juce::ModalCallbackFunction::create([this, dlg, metrics, fileName](int result) {
        if (result == 1)
            applyReferenceResult(metrics, fileName);
    }));
}

void SongVisionView::applyReferenceResult(const AnalysisMetrics& m, const juce::String& fileName)
{
    if (currentSongId < 0) return;

    if (m.tempoBpm > 0.0)
        bpmInput.setText(juce::String(m.tempoBpm, 1), juce::dontSendNotification);
    if (m.detectedKey.isNotEmpty())
        keyInput.setText(m.detectedKey, juce::dontSendNotification);
    if (refInput.isEmpty())
        refInput.setText(fileName, juce::dontSendNotification);

    if (m.chordProgression.isNotEmpty())
    {
        const juce::String line = tr("ref_chords_prefix", "Referenz: ") + m.chordProgression;
        if (chordInput.isEmpty())
            chordInput.setText(line, juce::dontSendNotification);
        else if (!chordInput.getText().contains(line))
            chordInput.setText(chordInput.getText().trimEnd() + "\n" + line, juce::dontSendNotification);
    }

    saveCurrentSongData();

    for (auto id : GenreAnalysis::suggestMoodIds(m))
        db.toggleStandardMoodForSong(currentSongId, id, true);

    refreshMoodButtons();
    refreshDiagnosis();
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
    ghostButton.setBounds(row2.removeFromRight(160).reduced(5, 6));
    refAnalyseButton.setBounds(row2.removeFromRight(150).reduced(5, 6));
    playerControls.setBounds(row2.removeFromRight(170).reduced(5, 6));
    refInput.setBounds(row2.reduced(5, 5));

    area.removeFromTop(6);

    // Zeile 3: Akkordfolge / Chords (Mehrzeiliger Block)
    auto chordArea = area.removeFromTop(66);
    chordLabel.setBounds(chordArea.removeFromLeft(140).removeFromTop(25));
    chordInput.setBounds(chordArea.reduced(5, 2));

    area.removeFromTop(10);

    // Zeile 4: Mood-Grid Header
    auto moodHeader = area.removeFromTop(30);
    moodTitleLabel.setBounds(moodHeader.removeFromLeft(150));
    addMoodButton.setBounds(moodHeader.removeFromLeft(120));

    area.removeFromTop(8);

    // Dynamic Tag-Grid
    int x = area.getX();
    int y = area.getY();
    int buttonHeight = 28;
    int spacing = 6;

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

    // Diagnose füllt den Rest unter dem Mood-Grid
    const int diagTop = (moodButtons.empty() ? area.getY() : y + buttonHeight) + 10;
    diagnosisBox.setBounds(area.getX(), diagTop, area.getWidth(), juce::jmax(50, area.getBottom() - diagTop));
}