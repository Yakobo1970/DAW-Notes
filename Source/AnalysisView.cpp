#pragma execution_character_set("utf-8")

#include "AnalysisView.h"
#include "SettingsManager.h"

namespace
{
    using Status = AnalysisFinding::Status;

    juce::Colour statusColour(Status s)
    {
        switch (s)
        {
        case Status::Good:    return juce::Colour(0xff4caf50);
        case Status::Warning: return juce::Colour(0xffffb300);
        case Status::Problem: return juce::Colour(0xffe53935);
        default:              return juce::Colour(0xff64b5f6);
        }
    }

    juce::Colour scoreColour(int percent)
    {
        return percent >= 80 ? juce::Colour(0xff4caf50) : percent >= 60 ? juce::Colour(0xffffb300) : juce::Colour(0xffe53935);
    }

    juce::String formatTime(double seconds)
    {
        const int total = (int)seconds;
        return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
    }
}

AnalysisView::AnalysisView(DatabaseManager& dbManager, TrackAnalyser& trackAnalyser, std::function<void()> onToDosAdded)
    : db(dbManager), analyser(trackAnalyser), toDosAddedCallback(std::move(onToDosAdded))
{
    auto& settings = SettingsManager::getInstance();

    profileLabel.setText(settings.getText("analysis_profile_label", "Genre-Profil:"), juce::dontSendNotification);
    addAndMakeVisible(profileLabel);

    const auto& profiles = GenreAnalysis::getProfiles();
    for (int i = 0; i < (int)profiles.size(); ++i)
        profileBox.addItem(profiles[(size_t)i].name, i + 1);
    profileBox.setSelectedId(1, juce::dontSendNotification);
    profileBox.onChange = [this] { reevaluate(); };
    addAndMakeVisible(profileBox);

    profileSourceLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
    profileSourceLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(profileSourceLabel);

    analyseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffd9822b));
    analyseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    analyseButton.onClick = [this] {
        auto st = analyser.getState();
        if (st == TrackAnalyser::State::WaitingForSignal || st == TrackAnalyser::State::Listening)
        {
            analyser.finishAnalysis();
        }
        else if (currentSongId >= 0 && (st == TrackAnalyser::State::Idle || st == TrackAnalyser::State::Done))
        {
            statusHint.clear();
            analyser.startAnalysis(currentSongId);
        }
        updateControls();
    };
    addAndMakeVisible(analyseButton);

    cancelButton.setButtonText(settings.getText("analysis_btn_cancel", "Abbrechen"));
    cancelButton.onClick = [this] {
        analyser.cancelAnalysis();
        lastState = TrackAnalyser::State::Idle;
        statusHint = SettingsManager::getInstance().getText("analysis_cancelled", "Analyse abgebrochen.");
        updateControls();
    };
    addChildComponent(cancelButton);

    toDoButton.setButtonText(settings.getText("analysis_btn_todos", "Tipps als Session ToDos"));
    toDoButton.setTooltip(tr("analysis_uebernimmt_alle_gelben_und", "Übernimmt alle gelben und roten Punkte als Session ToDos"));
    toDoButton.onClick = [this] { addFindingsAsToDos(); };
    addChildComponent(toDoButton);

    findingsList.setRowHeight(44);
    findingsList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1e1e1e));
    addChildComponent(findingsList);

    lastState = analyser.getState();
    updateControls();
    startTimerHz(15);
}

AnalysisView::~AnalysisView()
{
    stopTimer();
}

void AnalysisView::loadSong(int64_t songId)
{
    currentSongId = songId;
    hasResult = false;
    songGenre.clear();
    songKey.clear();
    songBpm = 0.0;

    if (currentSongId >= 0)
    {
        for (const auto& s : db.getAllSongs())
        {
            if (s.id == currentSongId)
            {
                songGenre = s.genre;
                songKey = s.key;
                songBpm = s.bpm;
                break;
            }
        }

        juce::String json = db.loadAnalysis(currentSongId, analysedAt);
        if (json.isNotEmpty())
        {
            metrics = AnalysisMetrics::fromVar(juce::JSON::parse(json));
            hasResult = true;
        }
    }

    auto& settings = SettingsManager::getInstance();
    profileBox.setSelectedId(GenreAnalysis::findProfileIndex(songGenre) + 1, juce::dontSendNotification);
    profileSourceLabel.setText(songGenre.isNotEmpty()
        ? settings.getText("analysis_profile_source", "Genre laut Song Vision: %s").replace("%s", songGenre)
        : settings.getText("analysis_profile_none", "Kein Genre in der Song Vision eingetragen"),
        juce::dontSendNotification);

    reevaluate();
}

void AnalysisView::reevaluate()
{
    if (hasResult)
    {
        const auto& profiles = GenreAnalysis::getProfiles();
        const int idx = juce::jlimit(0, (int)profiles.size() - 1, profileBox.getSelectedId() - 1);
        evaluation = GenreAnalysis::evaluate(metrics, profiles[(size_t)idx], songBpm, songKey);

        // Welche anderen Genres hat die Analyse "herausgehört"?
        juce::StringArray alternatives;
        for (const auto& [profileIndex, percent] : GenreAnalysis::rankProfiles(metrics))
            if (profileIndex != idx && alternatives.size() < 3)
                alternatives.add(profiles[(size_t)profileIndex].name + " (" + juce::String(percent) + " %)");

        if (alternatives.size() > 0)
            evaluation.findings.insert(evaluation.findings.begin(),
                { AnalysisFinding::Status::Info, SettingsManager::getInstance().getText("analysis_alt_genres", "Klingt auch nach"),
                  "", "", alternatives.joinIntoString("   |   ") });
    }
    else
    {
        evaluation = GenreEvaluation();
    }

    findingsList.updateContent();
    findingsList.repaint();
    updateControls();
}

void AnalysisView::addFindingsAsToDos()
{
    if (currentSongId < 0 || !hasResult) return;

    int added = 0;
    for (const auto& f : evaluation.findings)
    {
        if (f.status != Status::Warning && f.status != Status::Problem) continue;

        ToDoItem item;
        item.songId = currentSongId;
        item.text = tr("analysis_todo_prefix", "[Analyse] ") + f.label + ": " + f.advice;
        item.createdAt = juce::Time::getCurrentTime();
        item.type = ToDoType::Session;
        if (db.addToDo(item) > 0) ++added;
    }

    auto& settings = SettingsManager::getInstance();
    statusHint = added > 0
        ? settings.getText("analysis_todos_added", juce::String::fromUTF8("%d Tipps wurden als Session ToDos übernommen.")).replace("%d", juce::String(added))
        : settings.getText("analysis_todos_none", juce::String::fromUTF8("Keine gelben oder roten Punkte - nichts zu übernehmen."));

    if (added > 0 && toDosAddedCallback)
        toDosAddedCallback();

    repaint();
}

void AnalysisView::timerCallback()
{
    const auto st = analyser.getState();

    if (st == TrackAnalyser::State::Done)
    {
        const auto songId = analyser.getSongId();
        if (songId >= 0)
            db.saveAnalysis(songId, juce::JSON::toString(analyser.getResult().toVar(), true));

        analyser.markResultConsumed();
        statusHint.clear();

        if (songId == currentSongId)
            loadSong(currentSongId);
    }
    else if (st == TrackAnalyser::State::Idle
             && (lastState == TrackAnalyser::State::WaitingForSignal || lastState == TrackAnalyser::State::Listening))
    {
        statusHint = SettingsManager::getInstance().getText("analysis_no_signal",
            tr("analysis_es_wurde_fast_kein", "Es wurde (fast) kein Audiosignal gehört. Liegt DAW Notes auf dem Master-Bus und läuft die Wiedergabe?"));
    }

    if (st != lastState)
    {
        lastState = analyser.getState();
        updateControls();
    }

    if (st == TrackAnalyser::State::WaitingForSignal || st == TrackAnalyser::State::Listening || st == TrackAnalyser::State::Finishing)
        repaint(statusArea);
}

void AnalysisView::updateControls()
{
    auto& settings = SettingsManager::getInstance();
    const auto st = analyser.getState();
    const bool running = st == TrackAnalyser::State::WaitingForSignal || st == TrackAnalyser::State::Listening;
    const bool busy = running || st == TrackAnalyser::State::Finishing;

    analyseButton.setButtonText(running ? settings.getText("analysis_btn_finish", "Fertig")
                                        : settings.getText("analysis_btn_start", "Analyse"));
    analyseButton.setEnabled(st != TrackAnalyser::State::Finishing && currentSongId >= 0);
    cancelButton.setVisible(running);
    profileBox.setEnabled(!busy);

    const bool showResults = hasResult && !busy;
    findingsList.setVisible(showResults);
    toDoButton.setVisible(showResults);

    resized();
    repaint();
}

int AnalysisView::getNumRows()
{
    return (int)evaluation.findings.size();
}

void AnalysisView::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    juce::ignoreUnused(rowIsSelected);
    if (rowNumber < 0 || rowNumber >= (int)evaluation.findings.size()) return;

    const auto& f = evaluation.findings[(size_t)rowNumber];

    g.fillAll(rowNumber % 2 == 0 ? juce::Colour(0xff2a2a2a) : juce::Colour(0xff222222));

    g.setColour(statusColour(f.status));
    g.fillEllipse(8.0f, 8.0f, 10.0f, 10.0f);

    auto line1 = juce::Rectangle<int>(26, 2, width - 32, 20);
    g.setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::bold)));
    g.setColour(juce::Colours::white);
    g.drawText(f.label, line1.removeFromLeft(230), juce::Justification::centredLeft, true);

    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.setColour(statusColour(f.status).brighter(0.3f));
    g.drawText(f.value, line1.removeFromLeft(220), juce::Justification::centredLeft, true);

    if (f.target.isNotEmpty())
    {
        g.setColour(juce::Colour(0xff8a8a8a));
        g.drawText(SettingsManager::getInstance().getText("analysis_target", "Ziel: ") + f.target, line1, juce::Justification::centredLeft, true);
    }

    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.setColour(juce::Colour(0xffb0b0b0));
    g.drawFittedText(f.advice, juce::Rectangle<int>(26, 21, width - 32, height - 22), juce::Justification::topLeft, 2, 1.0f);
}

void AnalysisView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff222222));

    paintStatusPanel(g, statusArea);

    if (!findingsList.isVisible())
        paintInstructions(g, instructionArea);
}

void AnalysisView::paintStatusPanel(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto& settings = SettingsManager::getInstance();
    const auto st = analyser.getState();

    g.setColour(juce::Colour(0xff181818));
    g.fillRoundedRectangle(area.toFloat(), 5.0f);
    area = area.reduced(12, 6);

    if (st == TrackAnalyser::State::WaitingForSignal || st == TrackAnalyser::State::Listening)
    {
        const bool waiting = st == TrackAnalyser::State::WaitingForSignal;

        // Pulsierender "Aufnahme"-Punkt
        const float pulse = 0.5f + 0.5f * std::sin((float)juce::Time::getMillisecondCounter() * 0.006f);
        g.setColour(juce::Colour(0xffe53935).withAlpha(waiting ? 0.4f : 0.5f + 0.5f * pulse));
        g.fillEllipse((float)area.getX(), (float)area.getCentreY() - 7.0f, 14.0f, 14.0f);

        auto text = area.withTrimmedLeft(24);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(waiting ? settings.getText("analysis_waiting", juce::String::fromUTF8("Warte auf Signal – starte jetzt die Wiedergabe in der DAW (Song von Anfang an)"))
                           : settings.getText("analysis_listening", juce::String::fromUTF8("DAW Notes hört zu … spiele den kompletten Song bis zum Ende ab")),
                   text.removeFromTop(text.getHeight() / 2), juce::Justification::centredLeft, true);

        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.setColour(juce::Colour(0xffaaaaaa));
        auto info = text.removeFromLeft(330);
        g.drawText(formatTime(analyser.getElapsedSeconds()) + "   |   " + juce::String(analyser.getMomentaryLufs(), 1) + tr("analysis_momentary", " LUFS (momentan)"),
                   info, juce::Justification::centredLeft, true);

        // Pegelanzeige
        auto meter = text.reduced(0, 6).toFloat();
        g.setColour(juce::Colour(0xff2c2c2c));
        g.fillRoundedRectangle(meter, 3.0f);
        const float norm = juce::jlimit(0.0f, 1.0f, (analyser.getCurrentLevelDb() + 60.0f) / 60.0f);
        g.setColour(norm > 0.95f ? juce::Colour(0xffe53935) : juce::Colour(0xff4caf50));
        g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * norm), 3.0f);
        return;
    }

    if (st == TrackAnalyser::State::Finishing)
    {
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(settings.getText("analysis_finishing", juce::String::fromUTF8("Werte aus …")), area, juce::Justification::centredLeft, true);
        return;
    }

    if (hasResult)
    {
        const auto& profiles = GenreAnalysis::getProfiles();
        const int idx = juce::jlimit(0, (int)profiles.size() - 1, profileBox.getSelectedId() - 1);

        auto top = area.removeFromTop(area.getHeight() / 2);
        g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        g.setColour(scoreColour(evaluation.fitPercent));
        g.drawText(settings.getText("analysis_fit", "Genre-Passung %s: ").replace("%s", profiles[(size_t)idx].name)
                       + juce::String(evaluation.fitPercent) + " %",
                   top.removeFromLeft(430), juce::Justification::centredLeft, true);

        auto bar = top.reduced(0, 8).toFloat();
        g.setColour(juce::Colour(0xff2c2c2c));
        g.fillRoundedRectangle(bar, 3.0f);
        g.setColour(scoreColour(evaluation.fitPercent));
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * (float)evaluation.fitPercent / 100.0f), 3.0f);

        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.setColour(juce::Colour(0xff999999));
        juce::String sub = statusHint.isNotEmpty() ? statusHint
            : settings.getText("analysis_from", "Analyse vom %s").replace("%s", analysedAt.formatted("%d.%m.%Y %H:%M"))
              + tr("analysis_richtwerte_geschmack_und_absicht", "  ·  Richtwerte – Geschmack und Absicht gehen vor.");
        g.drawText(sub, area, juce::Justification::centredLeft, true);
        return;
    }

    g.setColour(juce::Colour(0xffcccccc));
    g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    g.drawText(statusHint.isNotEmpty() ? statusHint
                                       : settings.getText("analysis_no_result", juce::String::fromUTF8("Für diesen Song gibt es noch keine Analyse.")),
               area, juce::Justification::centredLeft, true);
}

void AnalysisView::paintInstructions(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (analyser.getState() == TrackAnalyser::State::Finishing) return;

    auto& settings = SettingsManager::getInstance();
    juce::String text = settings.getText("analysis_howto", juce::String::fromUTF8(
        "So funktioniert die Analyse:\n\n"
        "1.  DAW Notes als letztes Plugin auf den Master-Bus legen (nach Limiter/Mastering).\n"
        "2.  Oben das passende Genre-Profil prüfen (kommt aus dem Genre-Feld der Song Vision).\n"
        "3.  \"Analyse\" drücken und den kompletten Song einmal von Anfang bis Ende abspielen.\n"
        "4.  DAW Notes hört zu und beendet die Analyse automatisch, wenn die Wiedergabe stoppt\n"
        "     (oder du klickst auf \"Fertig\").\n\n"
        "Gemessen werden Lautheit (LUFS), Dynamik, Spitzenpegel, Stereobild, Bass-Mono,\n"
        "Frequenzbalance, Tempo und Tonart – und mit typischen Werten des Genres verglichen."));

    g.setColour(juce::Colour(0xffaaaaaa));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawFittedText(text.replace("\\n", "\n"), area.reduced(8), juce::Justification::topLeft, 14, 1.0f);
}

void AnalysisView::resized()
{
    auto area = getLocalBounds().reduced(10, 8);

    auto row1 = area.removeFromTop(30);
    profileLabel.setBounds(row1.removeFromLeft(95));
    profileBox.setBounds(row1.removeFromLeft(250).reduced(0, 2));
    row1.removeFromLeft(10);

    analyseButton.setBounds(row1.removeFromRight(130).reduced(0, 1));
    if (cancelButton.isVisible())
    {
        row1.removeFromRight(8);
        cancelButton.setBounds(row1.removeFromRight(100).reduced(0, 1));
    }
    if (toDoButton.isVisible())
    {
        row1.removeFromRight(8);
        toDoButton.setBounds(row1.removeFromRight(170).reduced(0, 1));
    }
    profileSourceLabel.setBounds(row1);

    area.removeFromTop(8);
    statusArea = area.removeFromTop(52);
    area.removeFromTop(8);

    instructionArea = area;
    findingsList.setBounds(area);
}
