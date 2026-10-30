#pragma execution_character_set("utf-8")

#include "ToDoView.h"
#include "SettingsManager.h"

// Kleiner Button mit gezeichnetem Kalender-Symbol (unabhängig von Emoji-Schriften)
class CalendarButton : public juce::Button
{
public:
    CalendarButton() : juce::Button("calendar") {}

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(isButtonDown ? juce::Colour(0xff2a4d7a) : isMouseOver ? juce::Colour(0xff35608f) : juce::Colour(0xff2b2b2b));
        g.fillRoundedRectangle(bounds, 4.0f);

        auto icon = bounds.withSizeKeepingCentre(16.0f, 15.0f).translated(0.0f, 0.5f);
        g.setColour(juce::Colours::white);
        g.drawRoundedRectangle(icon, 2.0f, 1.3f);
        g.fillRect(icon.removeFromTop(4.0f));

        // Aufhänger-Ringe
        g.fillRect(bounds.getCentreX() - 4.5f, icon.getY() - 7.0f, 1.6f, 4.0f);
        g.fillRect(bounds.getCentreX() + 3.0f, icon.getY() - 7.0f, 1.6f, 4.0f);

        // Tages-Raster
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c)
                g.fillRect(icon.getX() + 3.0f + c * 4.0f, icon.getY() + 2.0f + r * 4.0f, 2.0f, 2.0f);
    }
};

ToDoView::ToDoView(DatabaseManager& dbManager, bool isReleaseType)
    : db(dbManager), isRelease(isReleaseType)
{
    table.getHeader().addColumn(tr("col_pin", "Pin"), 1, 40, 30, 50);
    table.getHeader().addColumn(tr("col_status", "Status"), 2, 80, 60, 100);
    table.getHeader().addColumn(tr("col_task", "Aufgabe / ToDo"), 3, 300, 150, 1000);
    table.getHeader().addColumn(tr("col_date", "Erstellt am"), 4, 130, 100, 160);
    table.getHeader().addColumn("", 5, 40, 40, 40);
    table.getHeader().addColumn("", 6, 40, 40, 40);

    table.getHeader().setSortColumnId(1, true);

    addAndMakeVisible(table);

    inputField.setTextToShowWhenEmpty(tr("todo_input_placeholder", "Neue Aufgabe … (Enter übernimmt)"), juce::Colour(0xff6a6a6a));
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

        btn->setButtonText(item.isDone ? tr("todo_status_done", "Erledigt") : tr("todo_status_open", "Offen"));
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
        // Liste sofort mitführen, damit z.B. der Google-Export den aktuellen Text sieht
        editor->onTextChange = [this, id = item.id, editor] {
            for (auto& t : todoList)
                if (t.id == id) t.text = editor->getText();
            };
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

    // Spalte 6: In Google Kalender übertragen
    if (columnId == 6)
    {
        auto* btn = dynamic_cast<CalendarButton*>(existingComponentToUpdate);
        if (!btn)
        {
            delete existingComponentToUpdate;
            btn = new CalendarButton();
        }

        btn->setTooltip(tr("todo_calendar_tooltip", "Als Google-Aufgabe oder -Termin übernehmen"));
        btn->onClick = [this, id = item.id] {
            for (const auto& t : todoList)
                if (t.id == id)
                {
                    db.updateTodoText(t.id, t.text); // noch nicht gespeicherte Änderung sichern
                    exportToGoogleCalendar(t);
                    break;
                }
            };
        return btn;
    }

    delete existingComponentToUpdate;
    return nullptr;
}

void ToDoView::exportToGoogleCalendar(const ToDoItem& item)
{
    auto tomorrow = juce::Time::getCurrentTime() + juce::RelativeTime::days(1);

    auto dialogWindow = std::make_shared<juce::AlertWindow>(
        tr("calendar_title", "In Google übernehmen"),
        tr("calendar_prompt", "\"%s\"\n\nAufgabe: Der Text wird in die Zwischenablage kopiert und Google Tasks geöffnet – dort mit Strg+V einfügen und Fälligkeit setzen.\nTermin: Google Kalender öffnet sich mit vorausgefülltem Termin zum gewählten Datum.").replace("%s", item.text),
        juce::AlertWindow::QuestionIcon);

    dialogWindow->addComboBox("kind", { tr("calendar_kind_task", "Aufgabe (Google Tasks)"), tr("calendar_kind_event", "Termin (Google Kalender)") },
                              tr("calendar_kind_label", "Eintragen als:"));
    dialogWindow->getComboBoxComponent("kind")->setSelectedItemIndex(0, juce::dontSendNotification);
    dialogWindow->addTextEditor("date", tomorrow.formatted("%d.%m.%Y"), tr("calendar_date_label", "Datum (TT.MM.JJJJ):"));
    dialogWindow->addTextEditor("time", "", tr("calendar_time_label", "Uhrzeit (HH:MM, leer = ganztägig):"));
    dialogWindow->addButton(tr("calendar_btn_open", "Google öffnen"), 1);
    dialogWindow->addButton(tr("calendar_btn_cancel", "Abbrechen"), 0);

    dialogWindow->enterModalState(true, juce::ModalCallbackFunction::create([this, item, dialogWindow](int result) {
        if (result != 1) return;

        auto& set = SettingsManager::getInstance();

        juce::String songTitle;
        for (const auto& s : db.getAllSongs())
            if (s.id == currentSongId) { songTitle = s.title; break; }

        const juce::String title = songTitle.isNotEmpty() ? songTitle + ": " + item.text : item.text;

        // Aufgabe: Google bietet dafür keinen vorausgefüllten Link -> Zwischenablage + Google Tasks öffnen
        if (dialogWindow->getComboBoxComponent("kind")->getSelectedItemIndex() == 0)
        {
            juce::SystemClipboard::copyTextToClipboard(title);
            juce::URL("https://tasks.google.com/tasks/").launchInDefaultBrowser();
            return;
        }

        // Datum TT.MM.JJJJ prüfen
        auto dateParts = juce::StringArray::fromTokens(dialogWindow->getTextEditorContents("date").trim(), ".", "");
        int day = dateParts.size() == 3 ? dateParts[0].getIntValue() : 0;
        int month = dateParts.size() == 3 ? dateParts[1].getIntValue() : 0;
        int year = dateParts.size() == 3 ? dateParts[2].getIntValue() : 0;
        if (year > 0 && year < 100) year += 2000;

        juce::Time start(year, month - 1, day, 0, 0, 0, 0, true);
        bool dateValid = year >= 1970 && start.getDayOfMonth() == day && start.getMonth() == month - 1;

        // Uhrzeit HH:MM prüfen (optional)
        juce::String timeText = dialogWindow->getTextEditorContents("time").trim();
        bool allDay = timeText.isEmpty();
        bool timeValid = true;

        if (!allDay)
        {
            auto timeParts = juce::StringArray::fromTokens(timeText, ":.", "");
            int hours = timeParts[0].getIntValue();
            int minutes = timeParts.size() > 1 ? timeParts[1].getIntValue() : 0;
            timeValid = timeParts.size() <= 2 && timeParts[0].containsOnly("0123456789") && hours < 24 && minutes < 60;
            start = juce::Time(year, month - 1, day, hours, minutes, 0, 0, true);
        }

        if (!dateValid || !timeValid)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                tr("calendar_invalid_title", "Ungültige Eingabe"),
                tr("calendar_invalid_msg", "Bitte das Datum als TT.MM.JJJJ und die Uhrzeit als HH:MM angeben."));
            return;
        }

        // Google erwartet ganztägig YYYYMMDD/YYYYMMDD (Ende exklusiv), sonst lokale Zeit ohne "Z"
        juce::String dates = allDay
            ? start.formatted("%Y%m%d") + "/" + (start + juce::RelativeTime::days(1)).formatted("%Y%m%d")
            : start.formatted("%Y%m%dT%H%M00") + "/" + (start + juce::RelativeTime::hours(1)).formatted("%Y%m%dT%H%M00");

        juce::String listName = isRelease ? set.getText("tab_release_todos", "Release ToDos")
                                          : set.getText("tab_session_todos", "Session ToDos");

        juce::String details = "Song: " + songTitle + "\n" + listName + "\n\n" + item.text
            + "\n\n" + tr("calendar_details_footer", "Eingetragen mit DAW Notes");

        juce::URL calendarUrl = juce::URL("https://calendar.google.com/calendar/render")
            .withParameter("action", "TEMPLATE")
            .withParameter("text", title)
            .withParameter("dates", dates)
            .withParameter("details", details);

        calendarUrl.launchInDefaultBrowser();
        }));
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