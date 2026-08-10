#pragma once

#include <JuceHeader.h>
#include <vector>
#include <utility>
#include "DataModel.h"

struct sqlite3;

class DatabaseManager
{
public:
    DatabaseManager();
    ~DatabaseManager();

    // Akzeptiert optional den Pfad oder nutzt den Standardpfad
    bool openDatabase(const juce::File& customDbFile = juce::File());
    void initDatabase();

    // Song-Aktionen
    int64_t addSong(const Song& song);
    std::vector<Song> getAllSongs();
    bool updateSong(const Song& song);
    bool deleteSong(int64_t songId);

    // Standard Moods-Aktionen
    std::vector<std::pair<int, juce::String>> getStandardMoodsFromIni();
    std::vector<int> getStandardMoodsForSong(int64_t songId);
    bool toggleStandardMoodForSong(int64_t songId, int moodId, bool active = false);

    // Custom (User) Moods-Aktionen (Global für alle Songs)
    std::vector<juce::String> getAllGlobalCustomMoods();
    std::vector<juce::String> getActiveCustomMoodsForSong(int64_t songId);
    bool toggleCustomMoodForSong(int64_t songId, const juce::String& moodName, bool active);
    bool removeCustomMoodGlobally(const juce::String& moodName);

    // ToDo-Aktionen (Hauptmethoden mit Enum)
    int64_t addToDo(const ToDoItem& item);
    std::vector<ToDoItem> getToDosForSong(int64_t songId, ToDoType type);
    bool updateToDo(const ToDoItem& item);
    bool updateTodoStatus(int64_t todoId, bool isDone);
    bool updateTodoPermanent(int64_t todoId, bool isPermanent);
    bool updateTodoText(int64_t todoId, const juce::String& newText);
    bool deleteToDo(int64_t todoId);

    // Pin-Synchronisierung über alle Songs
    bool toggleTodoPin(int64_t todoId, bool setPinned);

    // ToDo-Aktionen (Überladungen für alternative Schreibweisen / bool-Parameter)
    int64_t addTodo(const ToDoItem& item) { return addToDo(item); }

    std::vector<ToDoItem> getToDosForSong(int64_t songId, bool isPermanent);
    std::vector<ToDoItem> getTodosForSong(int64_t songId, ToDoType type) { return getToDosForSong(songId, type); }
    std::vector<ToDoItem> getTodosForSong(int64_t songId, bool isPermanent) { return getToDosForSong(songId, isPermanent); }

    bool deleteTodo(int64_t todoId) { return deleteToDo(todoId); }

private:
    sqlite3* db = nullptr;
    void createTablesIfNotExist();
};