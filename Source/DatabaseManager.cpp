#pragma execution_character_set("utf-8")

#include "DatabaseManager.h"
#include "SettingsManager.h"

#if __has_include("sqlite3.h")
#include "sqlite3.h"
#elif __has_include(<sqlite3.h>)
#include <sqlite3.h>
#else
#include "../sqlite3.h"
#endif

DatabaseManager::DatabaseManager()
{
    openDatabase();
}

DatabaseManager::~DatabaseManager()
{
    if (db)
    {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool DatabaseManager::openDatabase(const juce::File& customDbFile)
{
    if (db != nullptr) return true;

    juce::File dbFile = customDbFile;
    if (!dbFile.exists() && dbFile.getFullPathName().isEmpty())
    {
        auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("DAW Notes");
        if (!appDataDir.exists())
            appDataDir.createDirectory();

        dbFile = appDataDir.getChildFile("daw_notes.db");
    }

    if (sqlite3_open(dbFile.getFullPathName().toRawUTF8(), &db) == SQLITE_OK)
    {
        createTablesIfNotExist();
        return true;
    }
    return false;
}

void DatabaseManager::initDatabase()
{
    openDatabase();
}

void DatabaseManager::createTablesIfNotExist()
{
    if (!db) return;

    const char* sqlSongs =
        "CREATE TABLE IF NOT EXISTS songs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "title TEXT, "
        "bpm REAL, "
        "key TEXT, "
        "genre TEXT, "
        "reference_track TEXT, "
        "chord_progression TEXT, "
        "created_at INTEGER);";

    const char* sqlToDos =
        "CREATE TABLE IF NOT EXISTS todos ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "song_id INTEGER, "
        "text TEXT, "
        "is_permanent INTEGER, "
        "is_done INTEGER, "
        "created_at INTEGER, "
        "completed_at INTEGER, "
        "type INTEGER);";

    const char* sqlSongMoods =
        "CREATE TABLE IF NOT EXISTS song_moods ("
        "song_id INTEGER, "
        "mood_id INTEGER, "
        "PRIMARY KEY (song_id, mood_id));";

    const char* sqlGlobalCustomMoods =
        "CREATE TABLE IF NOT EXISTS global_custom_moods ("
        "name TEXT PRIMARY KEY);";

    const char* sqlSongCustomMoods =
        "CREATE TABLE IF NOT EXISTS song_custom_moods ("
        "song_id INTEGER, "
        "mood_name TEXT, "
        "PRIMARY KEY (song_id, mood_name));";

    char* errMsg = nullptr;
    sqlite3_exec(db, sqlSongs, nullptr, nullptr, &errMsg);
    sqlite3_exec(db, sqlToDos, nullptr, nullptr, &errMsg);
    sqlite3_exec(db, sqlSongMoods, nullptr, nullptr, &errMsg);
    sqlite3_exec(db, sqlGlobalCustomMoods, nullptr, nullptr, &errMsg);
    sqlite3_exec(db, sqlSongCustomMoods, nullptr, nullptr, &errMsg);

    // Migration für bereits existierende Datenbanken
    const char* sqlAlterSongs = "ALTER TABLE songs ADD COLUMN chord_progression TEXT;";
    sqlite3_exec(db, sqlAlterSongs, nullptr, nullptr, nullptr);
}

// ==================== SONG METHODS ====================

int64_t DatabaseManager::addSong(const Song& song)
{
    if (!db) return -1;

    juce::String sql = "INSERT INTO songs (title, bpm, key, genre, reference_track, chord_progression, created_at) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, song.title.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, song.bpm);
        sqlite3_bind_text(stmt, 3, song.key.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, song.genre.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, song.referenceTrack.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, song.chordProgression.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 7, song.createdAt.toMilliseconds());

        if (sqlite3_step(stmt) == SQLITE_DONE)
        {
            int64_t newSongId = sqlite3_last_insert_rowid(db);
            sqlite3_finalize(stmt);

            // =========================================================
            // PERMANENTE (GEPINNTE) TODOS FÜR DEN NEUEN SONG ÜBERNEHMEN
            // =========================================================

            // 1. Eindeutige Kombinationen aus Text & Typ aller aktuell gepinnten ToDos abfragen
            struct PinnedTemplate {
                juce::String text;
                ToDoType type;
            };
            std::vector<PinnedTemplate> pinnedTemplates;

            juce::String getPinnedSql = "SELECT DISTINCT text, type FROM todos WHERE is_permanent = 1;";
            if (sqlite3_prepare_v2(db, getPinnedSql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
            {
                while (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    PinnedTemplate t;
                    t.text = juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 0));
                    t.type = static_cast<ToDoType>(sqlite3_column_int(stmt, 1));
                    pinnedTemplates.push_back(t);
                }
                sqlite3_finalize(stmt);
            }

            // 2. Diese gepinnten Aufgaben als neue, unerledigte Einträge für den neuen Song einfügen
            for (const auto& item : pinnedTemplates)
            {
                ToDoItem newItem;
                newItem.songId = newSongId;
                newItem.text = item.text;
                newItem.isDone = false;
                newItem.isPermanent = true;
                newItem.createdAt = juce::Time::getCurrentTime();
                newItem.type = item.type;

                addToDo(newItem);
            }

            return newSongId;
        }
    }
    sqlite3_finalize(stmt);
    return -1;
}

std::vector<Song> DatabaseManager::getAllSongs()
{
    std::vector<Song> list;
    if (!db) return list;

    juce::String sql = "SELECT id, title, bpm, key, genre, reference_track, chord_progression, created_at FROM songs ORDER BY id DESC;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            Song s;
            s.id = sqlite3_column_int64(stmt, 0);
            s.title = juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 1));
            s.bpm = sqlite3_column_double(stmt, 2);
            s.key = juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 3));
            s.genre = juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 4));

            auto refPtr = sqlite3_column_text(stmt, 5);
            s.referenceTrack = refPtr ? juce::String::fromUTF8((const char*)refPtr) : "";

            auto chordPtr = sqlite3_column_text(stmt, 6);
            s.chordProgression = chordPtr ? juce::String::fromUTF8((const char*)chordPtr) : "";

            int64_t millis = sqlite3_column_int64(stmt, 7);
            s.createdAt = (millis > 0) ? juce::Time(millis) : juce::Time::getCurrentTime();

            list.push_back(s);
        }
    }
    sqlite3_finalize(stmt);
    return list;
}

bool DatabaseManager::updateSong(const Song& song)
{
    if (!db) return false;

    juce::String sql = "UPDATE songs SET title = ?, bpm = ?, key = ?, genre = ?, reference_track = ?, chord_progression = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, song.title.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, song.bpm);
        sqlite3_bind_text(stmt, 3, song.key.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, song.genre.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, song.referenceTrack.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, song.chordProgression.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 7, song.id);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    sqlite3_finalize(stmt);
    return false;
}

bool DatabaseManager::deleteSong(int64_t songId)
{
    if (!db) return false;

    auto execDelete = [this, songId](const char* sqlStr) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sqlStr, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, songId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        };

    execDelete("DELETE FROM song_moods WHERE song_id = ?;");
    execDelete("DELETE FROM song_custom_moods WHERE song_id = ?;");

    // Löscht alle ToDos dieses Songs. Die Einträge in anderen Songs bleiben erhalten!
    execDelete("DELETE FROM todos WHERE song_id = ?;");

    juce::String sql = "DELETE FROM songs WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, songId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    sqlite3_finalize(stmt);
    return false;
}

// ==================== MOOD METHODS ====================

std::vector<std::pair<int, juce::String>> DatabaseManager::getStandardMoodsFromIni()
{
    return SettingsManager::getInstance().getStandardMoods();
}

std::vector<int> DatabaseManager::getStandardMoodsForSong(int64_t songId)
{
    std::vector<int> moods;
    if (!db) return moods;

    juce::String sql = "SELECT mood_id FROM song_moods WHERE song_id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, songId);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            moods.push_back(sqlite3_column_int(stmt, 0));
        }
    }
    sqlite3_finalize(stmt);
    return moods;
}

bool DatabaseManager::toggleStandardMoodForSong(int64_t songId, int moodId, bool active)
{
    if (!db) return false;

    juce::String sql = active
        ? "INSERT OR IGNORE INTO song_moods (song_id, mood_id) VALUES (?, ?);"
        : "DELETE FROM song_moods WHERE song_id = ? AND mood_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, songId);
        sqlite3_bind_int(stmt, 2, moodId);
        bool res = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return res;
    }
    sqlite3_finalize(stmt);
    return false;
}

std::vector<juce::String> DatabaseManager::getAllGlobalCustomMoods()
{
    std::vector<juce::String> list;
    if (!db) return list;

    juce::String sql = "SELECT name FROM global_custom_moods ORDER BY name ASC;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            list.push_back(juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 0)));
        }
    }
    sqlite3_finalize(stmt);
    return list;
}

std::vector<juce::String> DatabaseManager::getActiveCustomMoodsForSong(int64_t songId)
{
    std::vector<juce::String> list;
    if (!db) return list;

    juce::String sql = "SELECT mood_name FROM song_custom_moods WHERE song_id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, songId);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            list.push_back(juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 0)));
        }
    }
    sqlite3_finalize(stmt);
    return list;
}

bool DatabaseManager::toggleCustomMoodForSong(int64_t songId, const juce::String& moodName, bool active)
{
    if (!db || moodName.isEmpty()) return false;

    juce::String insertGlobalSql = "INSERT OR IGNORE INTO global_custom_moods (name) VALUES (?);";
    sqlite3_stmt* stmtGlobal = nullptr;
    if (sqlite3_prepare_v2(db, insertGlobalSql.toRawUTF8(), -1, &stmtGlobal, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmtGlobal, 1, moodName.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmtGlobal);
    }
    sqlite3_finalize(stmtGlobal);

    juce::String sql = active
        ? "INSERT OR IGNORE INTO song_custom_moods (song_id, mood_name) VALUES (?, ?);"
        : "DELETE FROM song_custom_moods WHERE song_id = ? AND mood_name = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, songId);
        sqlite3_bind_text(stmt, 2, moodName.toRawUTF8(), -1, SQLITE_TRANSIENT);
        bool res = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return res;
    }
    sqlite3_finalize(stmt);
    return false;
}

bool DatabaseManager::removeCustomMoodGlobally(const juce::String& moodName)
{
    if (!db || moodName.isEmpty()) return false;

    juce::String sql1 = "DELETE FROM song_custom_moods WHERE mood_name = ?;";
    juce::String sql2 = "DELETE FROM global_custom_moods WHERE name = ?;";

    sqlite3_stmt* stmt1 = nullptr;
    if (sqlite3_prepare_v2(db, sql1.toRawUTF8(), -1, &stmt1, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt1, 1, moodName.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt1);
    }
    sqlite3_finalize(stmt1);

    sqlite3_stmt* stmt2 = nullptr;
    bool success = false;
    if (sqlite3_prepare_v2(db, sql2.toRawUTF8(), -1, &stmt2, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt2, 1, moodName.toRawUTF8(), -1, SQLITE_TRANSIENT);
        success = (sqlite3_step(stmt2) == SQLITE_DONE);
    }
    sqlite3_finalize(stmt2);

    return success;
}

// ==================== TODO METHODS ====================

int64_t DatabaseManager::addToDo(const ToDoItem& item)
{
    if (!db) return -1;

    juce::String sql = "INSERT INTO todos (song_id, text, is_permanent, is_done, created_at, completed_at, type) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, item.songId);
        sqlite3_bind_text(stmt, 2, item.text.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, item.isPermanent ? 1 : 0);
        sqlite3_bind_int(stmt, 4, item.isDone ? 1 : 0);
        sqlite3_bind_int64(stmt, 5, item.createdAt.toMilliseconds());
        sqlite3_bind_int64(stmt, 6, item.completedAt.toMilliseconds());
        sqlite3_bind_int(stmt, 7, static_cast<int>(item.type));

        if (sqlite3_step(stmt) == SQLITE_DONE)
        {
            int64_t newId = sqlite3_last_insert_rowid(db);
            sqlite3_finalize(stmt);
            return newId;
        }
    }
    sqlite3_finalize(stmt);
    return -1;
}

bool DatabaseManager::toggleTodoPin(int64_t todoId, bool setPinned)
{
    if (!db) return false;

    // Details des Ursprungs-ToDos auslesen
    ToDoItem sourceItem;
    juce::String getSql = "SELECT song_id, text, type FROM todos WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, getSql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, todoId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            sourceItem.songId = sqlite3_column_int64(stmt, 0);
            sourceItem.text = juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 1));
            sourceItem.type = static_cast<ToDoType>(sqlite3_column_int(stmt, 2));
        }
        sqlite3_finalize(stmt);
    }
    else
    {
        return false;
    }

    if (setPinned)
    {
        // 1. Alle verfügbaren Songs abrufen
        std::vector<int64_t> allSongIds;
        juce::String songSql = "SELECT id FROM songs;";
        if (sqlite3_prepare_v2(db, songSql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
                allSongIds.push_back(sqlite3_column_int64(stmt, 0));
            sqlite3_finalize(stmt);
        }

        // 2. Für jeden Song prüfen & pinnen
        for (int64_t sId : allSongIds)
        {
            juce::String checkSql = "SELECT id FROM todos WHERE song_id = ? AND text = ? AND type = ?;";
            int64_t existingId = -1;

            if (sqlite3_prepare_v2(db, checkSql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int64(stmt, 1, sId);
                sqlite3_bind_text(stmt, 2, sourceItem.text.toRawUTF8(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 3, static_cast<int>(sourceItem.type));

                if (sqlite3_step(stmt) == SQLITE_ROW)
                    existingId = sqlite3_column_int64(stmt, 0);
                sqlite3_finalize(stmt);
            }

            if (existingId != -1)
            {
                // ToDo existiert bereits im Song -> Pin wieder aktivieren
                juce::String updateSql = "UPDATE todos SET is_permanent = 1 WHERE id = ?;";
                if (sqlite3_prepare_v2(db, updateSql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int64(stmt, 1, existingId);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
            else
            {
                // ToDo existiert noch nicht -> Neu anlegen (unerledigt + gepinnt)
                ToDoItem newItem;
                newItem.songId = sId;
                newItem.text = sourceItem.text;
                newItem.isDone = false;
                newItem.isPermanent = true;
                newItem.createdAt = juce::Time::getCurrentTime();
                newItem.type = sourceItem.type;

                addToDo(newItem);
            }
        }
    }
    else
    {
        // Pin global von allen ToDos mit demselben Text & Typ entfernen (Aufgabe bleibt erhalten)
        juce::String unpinSql = "UPDATE todos SET is_permanent = 0 WHERE text = ? AND type = ?;";
        if (sqlite3_prepare_v2(db, unpinSql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, sourceItem.text.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, static_cast<int>(sourceItem.type));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    return true;
}

std::vector<ToDoItem> DatabaseManager::getToDosForSong(int64_t songId, ToDoType type)
{
    std::vector<ToDoItem> list;
    if (!db) return list;

    juce::String sql = "SELECT id, song_id, text, is_permanent, is_done, created_at, completed_at, type "
        "FROM todos WHERE song_id = ? AND type = ? ORDER BY id ASC;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, songId);
        sqlite3_bind_int(stmt, 2, static_cast<int>(type));

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            ToDoItem t;
            t.id = sqlite3_column_int64(stmt, 0);
            t.songId = sqlite3_column_int64(stmt, 1);
            t.text = juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 2));
            t.isPermanent = sqlite3_column_int(stmt, 3) != 0;
            t.isDone = sqlite3_column_int(stmt, 4) != 0;
            t.createdAt = juce::Time(sqlite3_column_int64(stmt, 5));
            t.completedAt = juce::Time(sqlite3_column_int64(stmt, 6));
            t.type = static_cast<ToDoType>(sqlite3_column_int(stmt, 7));

            list.push_back(t);
        }
    }
    sqlite3_finalize(stmt);
    return list;
}

std::vector<ToDoItem> DatabaseManager::getToDosForSong(int64_t songId, bool isPermanent)
{
    std::vector<ToDoItem> list;
    if (!db) return list;

    juce::String sql = "SELECT id, song_id, text, is_permanent, is_done, created_at, completed_at, type FROM todos WHERE song_id = ? AND is_permanent = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, songId);
        sqlite3_bind_int(stmt, 2, isPermanent ? 1 : 0);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            ToDoItem t;
            t.id = sqlite3_column_int64(stmt, 0);
            t.songId = sqlite3_column_int64(stmt, 1);
            t.text = juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 2));
            t.isPermanent = sqlite3_column_int(stmt, 3) != 0;
            t.isDone = sqlite3_column_int(stmt, 4) != 0;
            t.createdAt = juce::Time(sqlite3_column_int64(stmt, 5));
            t.completedAt = juce::Time(sqlite3_column_int64(stmt, 6));
            t.type = static_cast<ToDoType>(sqlite3_column_int(stmt, 7));

            list.push_back(t);
        }
    }
    sqlite3_finalize(stmt);
    return list;
}

bool DatabaseManager::updateToDo(const ToDoItem& item)
{
    if (!db) return false;

    juce::String sql = "UPDATE todos SET text = ?, is_permanent = ?, is_done = ?, completed_at = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, item.text.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, item.isPermanent ? 1 : 0);
        sqlite3_bind_int(stmt, 3, item.isDone ? 1 : 0);
        sqlite3_bind_int64(stmt, 4, item.completedAt.toMilliseconds());
        sqlite3_bind_int64(stmt, 5, item.id);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    sqlite3_finalize(stmt);
    return false;
}

bool DatabaseManager::updateTodoStatus(int64_t todoId, bool isDone)
{
    if (!db) return false;

    juce::String sql = "UPDATE todos SET is_done = ?, completed_at = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, isDone ? 1 : 0);
        sqlite3_bind_int64(stmt, 2, isDone ? juce::Time::getCurrentTime().toMilliseconds() : 0);
        sqlite3_bind_int64(stmt, 3, todoId);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    sqlite3_finalize(stmt);
    return false;
}

bool DatabaseManager::updateTodoPermanent(int64_t todoId, bool isPermanent)
{
    return toggleTodoPin(todoId, isPermanent);
}

bool DatabaseManager::updateTodoText(int64_t todoId, const juce::String& newText)
{
    if (!db) return false;

    juce::String sql = "UPDATE todos SET text = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, newText.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, todoId);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    sqlite3_finalize(stmt);
    return false;
}

bool DatabaseManager::deleteToDo(int64_t todoId)
{
    if (!db) return false;

    juce::String sql = "DELETE FROM todos WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, todoId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    sqlite3_finalize(stmt);
    return false;
}