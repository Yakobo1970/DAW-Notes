#pragma once

#include <JuceHeader.h>
#include "DatabaseManager.h"

class SongLibraryView : public juce::Component,
    public juce::TableListBoxModel
{
public:
    SongLibraryView(DatabaseManager& dbManager,
        std::function<void(int64_t)> onSongSelectedCallback,
        std::function<void()> onRequestSwitchToVisionCallback,
        std::function<void(int64_t, const juce::String&)> onSongRenamedCallback);

    ~SongLibraryView() override;

    void refreshSongList();

    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent& e) override;
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    DatabaseManager& db;
    std::function<void(int64_t)> onSongSelected;
    std::function<void()> onRequestSwitchToVision;
    std::function<void(int64_t, const juce::String&)> onSongRenamed;

    std::vector<Song> songs;
    juce::TableListBox table{ {}, this };

    juce::TextButton addSongButton;
    juce::TextButton renameSongButton;
    juce::TextButton pdfExportButton;
    juce::TextButton deleteSongButton;

    int64_t selectedSongId = -1;

    void applySorting();
    void showContextMenu(int rowNumber);
    void renameSong(int64_t songId, const juce::String& oldTitle);
    void deleteSelectedSongWithConfirmation();
    void generatePdfExport();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongLibraryView)
};