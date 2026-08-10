#pragma once

#include <JuceHeader.h>
#include "DatabaseManager.h"
#include "DataModel.h"

class ToDoView : public juce::Component, public juce::TableListBoxModel
{
public:
    ToDoView(DatabaseManager& dbManager, bool isReleaseType);
    ~ToDoView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void loadToDosForSong(int64_t songId);

    // TableListBoxModel Overrides
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected, juce::Component* existingComponentToUpdate) override;

    // Für Spaltensortierung
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;
    void applySorting();

    void createNewToDoFromInput();

private:
    DatabaseManager& db;
    bool isRelease;
    int64_t currentSongId = -1;

    std::vector<ToDoItem> todoList;

    juce::TableListBox table{ {}, this };
    juce::TextEditor inputField;

    void reloadDataFromDb();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToDoView)
};