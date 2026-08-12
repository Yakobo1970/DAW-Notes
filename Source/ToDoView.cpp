#pragma execution_character_set("utf-8")

#include "ToDoView.h"

ToDoView::ToDoView(DatabaseManager& dbManager, bool isReleaseType)
    : db(dbManager), isRelease(isReleaseType)
{
    table.getHeader().addColumn("Pin", 1, 40, 30, 50);
    table.getHeader().addColumn("Status", 2, 80, 60, 100);
    table.getHeader().addColumn("Aufgabe / ToDo", 3, 300, 150, 1000);
    table.getHeader().addColumn("Erstellt am", 4, 130, 100, 160);
    table.getHeader().addColumn("", 5, 40, 40, 40);

    table.getHeader().setSortColumnId(1, true);

    addAndMakeVisible(table);

    inputField.onReturnKey = [this] { createNewToDoFromInput(); };
    addAndMakeVisible(inputField);
}

ToDoView::~ToDoView() {}

void ToDoView::loadToDosForSong(int64_t songId)
{
    currentSongId = songId;
    reloadDataFromDb();
}

void ToDoView::reloadDataFromDb()
{
    if (currentSongId >= 0)
    {
        ToDoType type = isRelease ? ToDoType::Release : ToDoType::Session;
        todoList = db.getToDosForSong(currentSongId, type);
        applySorting();
    }
    else
    {
        todoList.clear();
        table.updateContent();
        table.repaint();
    }
}

void ToDoView::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    juce::ignoreUnused(newSortColumnId, isForwards);
    applySorting();
}

void ToDoView::applySorting()
{
    const int columnId = table.getHeader().getSortColumnId();
    const bool isForwards = table.getHeader().isSortedForwards();

    if (columnId == 0) return;

    std::sort(todoList.begin(), todoList.end(), [columnId, isForwards](const ToDoItem& a, const ToDoItem& b) {
        int result = 0;

        switch (columnId)
        {
        case 1: // Pin (Permanent)
            result = (a.isPermanent == b.isPermanent) ? 0 : (a.isPermanent ? 1 : -1);
            break;
        case 2: // Status (Erledigt)
            result = (a.isDone == b.isDone) ? 0 : (a.isDone ? 1 : -1);
            break;
        case 3: // Aufgaben-Text
            result = a.text.compareIgnoreCase(b.text);
            break;
        case 4: // Erstellt am (Datum)
            result = (a.createdAt.toMilliseconds() < b.createdAt.toMilliseconds()) ? -1 :
                (a.createdAt.toMilliseconds() > b.createdAt.toMilliseconds()) ? 1 : 0;
            break;
        default:
            break;
        }

        return isForwards ? (result > 0) : (result < 0);
        });

    table.updateContent();
    table.repaint();
}

void ToDoView::createNewToDoFromInput()
{
    juce::String text = inputField.getText().trim();
    if (text.isEmpty() || currentSongId < 0) return;

    ToDoItem newItem;
    newItem.songId = currentSongId;
    newItem.text = text;
    newItem.isDone = false;
    newItem.isPermanent = false;
    newItem.createdAt = juce::Time::getCurrentTime();
    newItem.type = isRelease ? ToDoType::Release : ToDoType::Session;

    if (db.addToDo(newItem) > 0)
    {
        inputField.clear();
        reloadDataFromDb();
    }
}

int ToDoView::getNumRows()
{
    return static_cast<int>(todoList.size());
}

void ToDoView::paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
{
    juce::ignoreUnused(width, height, rowIsSelected);

    if (rowNumber % 2 == 0)
        g.fillAll(juce::Colour(0xff2a2a2a));
    else
        g.fillAll(juce::Colour(0xff222222));
}

void ToDoView::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected)
{
    juce::ignoreUnused(rowIsSelected);

    if (rowNumber < 0 || rowNumber >= (int)todoList.size()) return;

    const auto& item = todoList[(size_t)rowNumber];

    if (columnId == 4) // Erstellt am
    {
        g.setColour(juce::Colours::lightgrey);
        g.setFont(12.0f);
        juce::String dateStr = item.createdAt.formatted("%d.%m.%Y %H:%M");
        g.drawText(dateStr, juce::Rectangle<int>(width, height).reduced(5, 0), juce::Justification::centredLeft);
    }
}

juce::Component* ToDoView::refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected, juce::Component* existingComponentToUpdate)
{
    juce::ignoreUnused(isRowSelected);

    if (rowNumber < 0 || (size_t)rowNumber >= todoList.size())
    {
        delete existingComponentToUpdate;
        return nullptr;
    }

    const auto item = todoList[(size_t)rowNumber];

    // Spalte 1: Permanent Pin Toggle
    if (columnId == 1)
    {
        auto* toggle = dynamic_cast<juce::ToggleButton*>(existingComponentToUpdate);
        if (!toggle) toggle = new juce::ToggleButton();

        toggle->setButtonText("");
        toggle->setToggleState(item.isPermanent, juce::dontSendNotification);

        toggle->onClick = [this, item, toggle] {
            // Ruft die neue Synchronisierungs-Logik auf
            db.toggleTodoPin(item.id, toggle->getToggleState());
            reloadDataFromDb();
            };
        return toggle;
    }

    // Spalte 2: Status Button
    if (columnId == 2)
    {
        auto* btn = dynamic_cast<juce::TextButton*>(existingComponentToUpdate);
        if (!btn) btn = new juce::TextButton();

        btn->setButtonText(item.isDone ? "Erledigt" : "Offen");
        btn->setColour(juce::TextButton::buttonColourId, item.isDone ? juce::Colours::darkgreen : juce::Colours::darkred);

        btn->onClick = [this, item] {
            db.updateTodoStatus(item.id, !item.isDone);
            reloadDataFromDb();
            };
        return btn;
    }

    // Spalte 3: Aufgaben TextEditor
    if (columnId == 3)
    {
        auto* editor = dynamic_cast<juce::TextEditor*>(existingComponentToUpdate);
        if (!editor) editor = new juce::TextEditor();

        editor->setText(item.text, juce::dontSendNotification);
        editor->onFocusLost = [this, item, editor] {
            if (editor->getText() != item.text)
            {
                db.updateTodoText(item.id, editor->getText());
            }
            };
        return editor;
    }

    // Spalte 5: Löschen Button ("X")
    if (columnId == 5)
    {
        auto* btn = dynamic_cast<juce::TextButton*>(existingComponentToUpdate);
        if (!btn) btn = new juce::TextButton();

        btn->setButtonText("X");
        btn->onClick = [this, item] {
            db.deleteToDo(item.id);
            reloadDataFromDb();
            };
        return btn;
    }

    delete existingComponentToUpdate;
    return nullptr;
}

void ToDoView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
}

void ToDoView::resized()
{
    auto area = getLocalBounds().reduced(10);

    inputField.setBounds(area.removeFromBottom(28));
    area.removeFromBottom(8);

    table.setBounds(area);
}