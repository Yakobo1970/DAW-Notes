#pragma execution_character_set("utf-8")

#include "SongLibraryView.h"
#include "SettingsManager.h"
#include <algorithm>

SongLibraryView::SongLibraryView(DatabaseManager& dbManager,
    std::function<void(int64_t)> onSongSelectedCallback,
    std::function<void()> onRequestSwitchToVisionCallback,
    std::function<void(int64_t, const juce::String&)> onSongRenamedCallback)
    : db(dbManager),
    onSongSelected(onSongSelectedCallback),
    onRequestSwitchToVision(onRequestSwitchToVisionCallback),
    onSongRenamed(onSongRenamedCallback)
{
    auto& settings = SettingsManager::getInstance();

    // Alle Texte über SettingsManager + UTF8-Fallbacks laden
    addSongButton.setButtonText(settings.getText("btn_add_song", juce::String::fromUTF8("+ Neuer Song")));
    renameSongButton.setButtonText(settings.getText("btn_rename_song", juce::String::fromUTF8("Umbenennen")));
    pdfExportButton.setButtonText(settings.getText("btn_html_export", juce::String::fromUTF8("HTML Export")));
    deleteSongButton.setButtonText(settings.getText("btn_delete_song", juce::String::fromUTF8("Song löschen")));

    table.getHeader().addColumn(settings.getText("col_id", "ID"), 1, 40, 30, 60, juce::TableHeaderComponent::defaultFlags | juce::TableHeaderComponent::sortable);
    table.getHeader().addColumn(settings.getText("col_title", "Titel"), 2, 240, 120, 500, juce::TableHeaderComponent::defaultFlags | juce::TableHeaderComponent::sortable);
    table.getHeader().addColumn(settings.getText("col_bpm", "BPM"), 3, 60, 50, 90, juce::TableHeaderComponent::defaultFlags | juce::TableHeaderComponent::sortable);
    table.getHeader().addColumn(settings.getText("col_key", "Tonart"), 4, 100, 70, 150, juce::TableHeaderComponent::defaultFlags | juce::TableHeaderComponent::sortable);
    table.getHeader().addColumn(settings.getText("col_genre", "Genre"), 5, 120, 80, 200, juce::TableHeaderComponent::defaultFlags | juce::TableHeaderComponent::sortable);
    table.getHeader().addColumn(settings.getText("col_created", juce::String::fromUTF8("Erstellt am")), 6, 140, 110, 180, juce::TableHeaderComponent::defaultFlags | juce::TableHeaderComponent::sortable);

    addAndMakeVisible(table);
    addAndMakeVisible(addSongButton);
    addAndMakeVisible(renameSongButton);
    addAndMakeVisible(pdfExportButton);
    addAndMakeVisible(deleteSongButton);

    deleteSongButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff8b0000));
    pdfExportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d5a27));

    addSongButton.onClick = [this] {
        Song newSong;
        newSong.title = tr("library_neuer_song", "Neuer Song");
        newSong.bpm = 120.0;
        newSong.key = "C Major";
        newSong.genre = "Pop";
        newSong.createdAt = juce::Time::getCurrentTime();

        int64_t newId = db.addSong(newSong);
        refreshSongList();

        if (newId >= 0)
        {
            selectedSongId = newId;
            table.selectRow(0);
            if (onSongSelected) onSongSelected(selectedSongId);
        }
        };

    renameSongButton.onClick = [this] {
        int selectedRow = table.getSelectedRow();
        if (selectedRow >= 0 && selectedRow < (int)songs.size())
        {
            const auto& song = songs[(size_t)selectedRow];
            renameSong(song.id, song.title);
        }
        };

    pdfExportButton.onClick = [this] {
        generatePdfExport();
        };

    deleteSongButton.onClick = [this] {
        deleteSelectedSongWithConfirmation();
        };

    refreshSongList();
}

SongLibraryView::~SongLibraryView() {}

void SongLibraryView::refreshSongList()
{
    songs = db.getAllSongs();
    applySorting();
    table.updateContent();
    table.repaint();
}

void SongLibraryView::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    if (newSortColumnId != 0)
    {
        table.getHeader().setSortColumnId(newSortColumnId, isForwards);
        applySorting();
        table.updateContent();
        table.repaint();
    }
}

void SongLibraryView::applySorting()
{
    const int columnId = table.getHeader().getSortColumnId();
    const bool isForwards = table.getHeader().isSortedForwards();

    if (columnId == 0) return;

    std::sort(songs.begin(), songs.end(), [columnId, isForwards](const Song& a, const Song& b) {
        int result = 0;

        switch (columnId)
        {
        case 1: result = (a.id < b.id) ? -1 : (a.id > b.id ? 1 : 0); break;
        case 2: result = a.title.compareIgnoreCase(b.title); break;
        case 3: result = (a.bpm < b.bpm) ? -1 : (a.bpm > b.bpm ? 1 : 0); break;
        case 4: result = a.key.compareIgnoreCase(b.key); break;
        case 5: result = a.genre.compareIgnoreCase(b.genre); break;
        case 6: result = (a.createdAt.toMilliseconds() < b.createdAt.toMilliseconds()) ? -1 : (a.createdAt.toMilliseconds() > b.createdAt.toMilliseconds() ? 1 : 0); break;
        default: break;
        }

        return isForwards ? (result < 0) : (result > 0);
        });
}

int SongLibraryView::getNumRows()
{
    return static_cast<int>(songs.size());
}

void SongLibraryView::paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
{
    juce::ignoreUnused(width, height);

    if (rowIsSelected)
        g.fillAll(juce::Colour(0xff3b5998));
    else if (rowNumber % 2 == 0)
        g.fillAll(juce::Colour(0xff2a2a2a));
    else
        g.fillAll(juce::Colour(0xff222222));
}

void SongLibraryView::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected)
{
    juce::ignoreUnused(rowIsSelected);

    if (rowNumber < 0 || rowNumber >= (int)songs.size()) return;

    const auto& song = songs[(size_t)rowNumber];
    g.setColour(juce::Colours::white);
    g.setFont(13.0f);

    auto rect = juce::Rectangle<int>(width, height).reduced(8, 0);

    if (columnId == 1) g.drawText(juce::String(song.id), rect, juce::Justification::centredLeft);
    else if (columnId == 2) g.drawText(song.title, rect, juce::Justification::centredLeft);
    else if (columnId == 3) g.drawText(juce::String(song.bpm, 1), rect, juce::Justification::centredLeft);
    else if (columnId == 4) g.drawText(song.key, rect, juce::Justification::centredLeft);
    else if (columnId == 5) g.drawText(song.genre, rect, juce::Justification::centredLeft);
    else if (columnId == 6)
    {
        g.setColour(juce::Colours::lightgrey);
        g.drawText(song.createdAt.formatted("%d.%m.%Y %H:%M"), rect, juce::Justification::centredLeft);
    }
}

void SongLibraryView::cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e)
{
    juce::ignoreUnused(columnId);

    if (rowNumber < 0 || rowNumber >= (int)songs.size()) return;

    selectedSongId = songs[(size_t)rowNumber].id;

    if (onSongSelected)
        onSongSelected(selectedSongId);

    if (e.mods.isPopupMenu())
    {
        showContextMenu(rowNumber);
    }
}

void SongLibraryView::cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&)
{
    if (rowNumber < 0 || rowNumber >= (int)songs.size()) return;

    const auto& song = songs[(size_t)rowNumber];
    selectedSongId = song.id;

    if (onSongSelected) onSongSelected(selectedSongId);

    if (columnId == 2)
    {
        renameSong(song.id, song.title);
    }
    else if (onRequestSwitchToVision)
    {
        onRequestSwitchToVision();
    }
}

void SongLibraryView::showContextMenu(int rowNumber)
{
    if (rowNumber < 0 || rowNumber >= (int)songs.size()) return;
    const auto song = songs[(size_t)rowNumber];
    auto& settings = SettingsManager::getInstance();

    juce::PopupMenu menu;
    menu.addItem(1, settings.getText("ctx_load_open", juce::String::fromUTF8("Song laden & öffnen")));
    menu.addItem(2, settings.getText("btn_rename_song", juce::String::fromUTF8("Umbenennen")));
    menu.addItem(3, settings.getText("ctx_html_export", juce::String::fromUTF8("HTML / Sheet Export...")));
    menu.addSeparator();
    menu.addItem(4, settings.getText("ctx_delete_song", juce::String::fromUTF8("Löschen...")));

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, song](int result) {
        if (result == 1)
        {
            if (onSongSelected) onSongSelected(song.id);
            if (onRequestSwitchToVision) onRequestSwitchToVision();
        }
        else if (result == 2)
        {
            renameSong(song.id, song.title);
        }
        else if (result == 3)
        {
            generatePdfExport();
        }
        else if (result == 4)
        {
            deleteSelectedSongWithConfirmation();
        }
        });
}

void SongLibraryView::renameSong(int64_t songId, const juce::String& oldTitle)
{
    auto& settings = SettingsManager::getInstance();

    auto dialogWindow = std::make_shared<juce::AlertWindow>(
        settings.getText("dialog_rename_title", juce::String::fromUTF8("Song umbenennen")),
        settings.getText("dialog_rename_prompt", juce::String::fromUTF8("Gib einen neuen Titel ein:")),
        juce::MessageBoxIconType::NoIcon
    );

    dialogWindow->addTextEditor("titleInput", oldTitle, settings.getText("dialog_rename_placeholder", juce::String::fromUTF8("Neuer Titel...")));
    dialogWindow->addButton(settings.getText("btn_save", juce::String::fromUTF8("Speichern")), 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialogWindow->addButton(settings.getText("btn_cancel", juce::String::fromUTF8("Abbrechen")), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    dialogWindow->enterModalState(true, juce::ModalCallbackFunction::create([this, dialogWindow, songId](int result) {
        if (result == 1)
        {
            juce::String newTitle = dialogWindow->getTextEditorContents("titleInput").trim();
            if (newTitle.isNotEmpty())
            {
                auto all = db.getAllSongs();
                for (auto s : all)
                {
                    if (s.id == songId)
                    {
                        s.title = newTitle;
                        db.updateSong(s);

                        if (onSongRenamed)
                            onSongRenamed(songId, newTitle);

                        break;
                    }
                }
                refreshSongList();
            }
        }
        }));
}

void SongLibraryView::deleteSelectedSongWithConfirmation()
{
    auto& settings = SettingsManager::getInstance();

    int selectedRow = table.getSelectedRow();
    if (selectedRow < 0 || selectedRow >= (int)songs.size())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            settings.getText("dialog_delete_notice_title", juce::String::fromUTF8("Hinweis")),
            settings.getText("dialog_delete_notice_msg", juce::String::fromUTF8("Bitte wähle zuerst einen Song aus der Liste aus, den du löschen möchtest."))
        );
        return;
    }

    const auto song = songs[(size_t)selectedRow];
    juce::String confirmMsg = settings.getText("dialog_delete_confirm", juce::String::fromUTF8("Möchtest du '%s' wirklich unwiderruflich löschen?")).replace("%s", song.title);

    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        settings.getText("dialog_delete_title", juce::String::fromUTF8("Song löschen")),
        confirmMsg,
        settings.getText("btn_yes_delete", juce::String::fromUTF8("Ja, löschen")),
        settings.getText("btn_cancel", juce::String::fromUTF8("Abbrechen")),
        nullptr,
        juce::ModalCallbackFunction::create([this, song](int result) {
            if (result == 1)
            {
                db.deleteSong(song.id);
                selectedSongId = -1;
                refreshSongList();
            }
            })
    );
}

void SongLibraryView::generatePdfExport()
{
    auto& settings = SettingsManager::getInstance();

    int selectedRow = table.getSelectedRow();
    if (selectedRow < 0 || selectedRow >= (int)songs.size())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            settings.getText("dialog_export_notice_title", juce::String::fromUTF8("Export Hinweis")),
            settings.getText("dialog_export_notice_msg", juce::String::fromUTF8("Bitte wähle zuerst einen Song aus der Liste aus!"))
        );
        return;
    }

    const auto song = songs[(size_t)selectedRow];

    // 1. ToDos KORREKT abrufen: 
    // Erste Abfrage = Session ToDos (ToDoType::Session / false)
    // Zweite Abfrage = Release ToDos (ToDoType::Release / true)
    auto sessionTodos = db.getToDosForSong(song.id, ToDoType::Session);
    auto releaseTodos = db.getToDosForSong(song.id, ToDoType::Release);

    // 2. Alle Moods sammeln (Standard + Custom)
    juce::StringArray allMoods;
    auto activeStdMoodIds = db.getStandardMoodsForSong(song.id);
    auto stdMoodList = db.getStandardMoodsFromIni();
    for (int id : activeStdMoodIds)
    {
        for (const auto& stdMood : stdMoodList)
        {
            if (stdMood.first == id)
            {
                allMoods.add(stdMood.second);
                break;
            }
        }
    }

    auto activeCustomMoods = db.getActiveCustomMoodsForSong(song.id);
    for (const auto& customMood : activeCustomMoods)
    {
        allMoods.add(customMood);
    }

    // 3. HTML-Dokument aufbauen
    juce::String html;
    html << "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        << "<title>" << song.title << " - Studio Sheet</title>"
        << "<style>"
        << "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 40px; color: #111; background: #fff; line-height: 1.5; }"
        << "h1 { color: #d97706; border-bottom: 3px solid #d97706; padding-bottom: 8px; margin-bottom: 5px; font-size: 28px; }"
        << "h2 { color: #222; margin-top: 30px; border-bottom: 1px solid #ccc; padding-bottom: 4px; font-size: 16px; text-transform: uppercase; letter-spacing: 1px; }"
        << ".meta-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; background: #f8f8f8; padding: 15px; border-radius: 6px; border: 1px solid #eee; margin-top: 15px; }"
        << ".meta-item { font-size: 14px; }"
        << ".meta-label { font-weight: bold; color: #555; }"
        << ".tag { display: inline-block; background: #d97706; color: #fff; padding: 3px 10px; border-radius: 12px; font-size: 12px; font-weight: bold; margin-right: 6px; margin-bottom: 6px; }"
        << ".chords-box { font-family: 'Courier New', Courier, monospace; background: #f4f4f5; border-left: 4px solid #d97706; padding: 12px 15px; border-radius: 4px; white-space: pre-wrap; word-wrap: break-word; font-size: 14px; color: #18181b; margin-top: 10px; }"
        << ".todo-table { width: 100%; border-collapse: collapse; margin-top: 10px; }"
        << ".todo-table td { padding: 8px 10px; border-bottom: 1px solid #eee; font-size: 14px; }"
        << ".status-done { color: #16a34a; font-weight: bold; width: 40px; }"
        << ".status-open { color: #dc2626; font-weight: bold; width: 40px; }"
        << ".todo-text-done { text-decoration: line-through; color: #777; }"
        << "@media print { body { margin: 0; } }"
        << "</style></head><body>";

    // Header & Meta Info
    html << "<h1>" << song.title << "</h1>";
    html << "<div style='color: #666; font-size: 12px;'>DAW Notes Studio Sheet &bull; " << tr("html_generated", "Generiert am") << " " << juce::Time::getCurrentTime().formatted("%d.%m.%Y %H:%M") << "</div>";

    html << "<div class='meta-grid'>"
        << "<div class='meta-item'><span class='meta-label'>" << tr("html_bpm", "BPM:") << "</span> " << (song.bpm > 0 ? juce::String(song.bpm, 1) : "-") << "</div>"
        << "<div class='meta-item'><span class='meta-label'>" << tr("html_key", "Tonart:") << "</span> " << (song.key.isNotEmpty() ? song.key : "-") << "</div>"
        << "<div class='meta-item'><span class='meta-label'>" << tr("html_genre", "Genre:") << "</span> " << (song.genre.isNotEmpty() ? song.genre : "-") << "</div>"
        << "<div class='meta-item'><span class='meta-label'>" << tr("html_reference", "Referenz-Track:") << "</span> " << (song.referenceTrack.isNotEmpty() ? song.referenceTrack : "-") << "</div>"
        << "<div class='meta-item'><span class='meta-label'>" << tr("html_created", "Erstellt am:") << "</span> " << song.createdAt.formatted("%d.%m.%Y") << "</div>"
        << "</div>";

    // Moods Section
    if (!allMoods.isEmpty())
    {
        html << "<h2>" << tr("html_moods", "Moods & Vibe") << "</h2><div style='margin-top: 10px;'>";
        for (const auto& mood : allMoods)
        {
            html << "<span class='tag'>" << mood << "</span>";
        }
        html << "</div>";
    }

    // Akkordfolge Section
    if (song.chordProgression.trim().isNotEmpty())
    {
        html << "<h2>" << tr("html_chords", "Akkordfolge / Harmonien") << "</h2>";
        html << "<div class='chords-box'>" << song.chordProgression << "</div>";
    }

    // Session ToDos (Jetzt garantiert aus der Session-Liste!)
    html << "<h2>" << tr("html_session_todos", "Session To-Dos") << " (" << sessionTodos.size() << ")</h2>";
    if (sessionTodos.empty())
    {
        html << "<p style='color: #777; font-size: 14px;'>" << tr("html_no_session_todos", "Keine Session To-Dos vorhanden.") << "</p>";
    }
    else
    {
        html << "<table class='todo-table'>";
        for (const auto& todo : sessionTodos)
        {
            html << "<tr>"
                << "<td class='" << (todo.isDone ? "status-done" : "status-open") << "'>" << (todo.isDone ? "[&#10004;]" : "[ &nbsp; ]") << "</td>"
                << "<td class='" << (todo.isDone ? "todo-text-done" : "") << "'>" << todo.text << "</td>"
                << "</tr>";
        }
        html << "</table>";
    }

    // Release ToDos (Jetzt garantiert aus der Release-Liste!)
    html << "<h2>" << tr("html_release_todos", "Release To-Dos") << " (" << releaseTodos.size() << ")</h2>";
    if (releaseTodos.empty())
    {
        html << "<p style='color: #777; font-size: 14px;'>" << tr("html_no_release_todos", "Keine Release To-Dos vorhanden.") << "</p>";
    }
    else
    {
        html << "<table class='todo-table'>";
        for (const auto& todo : releaseTodos)
        {
            html << "<tr>"
                << "<td class='" << (todo.isDone ? "status-done" : "status-open") << "'>" << (todo.isDone ? "[&#10004;]" : "[ &nbsp; ]") << "</td>"
                << "<td class='" << (todo.isDone ? "todo-text-done" : "") << "'>" << todo.text << "</td>"
                << "</tr>";
        }
        html << "</table>";
    }

    html << "</body></html>";

    // Speicher-Dialog
    juce::String defaultFileName = juce::File::createLegalFileName(song.title) + "_Sheet.html";

    auto fileChooser = std::make_shared<juce::FileChooser>(
        settings.getText("dialog_save_html", juce::String::fromUTF8("HTML Studio-Sheet speichern")),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(defaultFileName),
        "*.html"
    );

    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [fileChooser, html](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file != juce::File())
            {
                file.replaceWithText(html, false, false, "UTF-8");
                file.startAsProcess();
            }
        });
}

void SongLibraryView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
}

void SongLibraryView::resized()
{
    auto area = getLocalBounds().reduced(10);

    auto bottomBar = area.removeFromBottom(35);
    int buttonWidth = (bottomBar.getWidth() - 30) / 4;

    addSongButton.setBounds(bottomBar.removeFromLeft(buttonWidth));
    bottomBar.removeFromLeft(10);

    renameSongButton.setBounds(bottomBar.removeFromLeft(buttonWidth));
    bottomBar.removeFromLeft(10);

    pdfExportButton.setBounds(bottomBar.removeFromLeft(buttonWidth));
    bottomBar.removeFromLeft(10);

    deleteSongButton.setBounds(bottomBar);

    area.removeFromBottom(10);
    table.setBounds(area);
}