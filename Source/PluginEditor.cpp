#pragma execution_character_set("utf-8")
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SettingsManager.h"
#include <algorithm>

// ==============================================================================
// 1. Inhalts-Container Zeichnen (Hintergründe + GIFs skalieren hier mit!)
// ==============================================================================
void MainContentComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181818));

    // Header GIF (fest eingepasst in 850x60 Basis-Koordinaten)
    if (editor.headerGif.isValid())
    {
        g.drawImageWithin(editor.headerGif,
            0, 0, 850, 60,
            juce::RectanglePlacement::fillDestination,
            false);
    }

    // Song-Header Balken
    g.setColour(juce::Colour(0xff111111));
    g.fillRect(0, 60, 850, 34);

    g.setColour(juce::Colour(0xff333333));
    g.drawHorizontalLine(94, 0.0f, 850.0f);

    // Footer GIF (fest eingepasst am Boden der 650px Basis-Koordinaten)
    if (editor.footerGif.isValid())
    {
        g.drawImageWithin(editor.footerGif,
            0, 650 - 40, 850, 40,
            juce::RectanglePlacement::fillDestination,
            false);
    }
}

// ==============================================================================
// 2. DawNotesAudioProcessorEditor Konstruktor
// ==============================================================================
DawNotesAudioProcessorEditor::DawNotesAudioProcessorEditor(DawNotesAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName("Segoe UI");

    // 1. Fenstergröße festlegen & freie Skalierung aktivieren
    setSize(850, 650);
    setResizable(true, true);
    setResizeLimits(700, 500, 1600, 1200);

    // Seitenverhältnis fixieren, damit keine schwarzen Ränder entstehen
    getConstrainer()->setFixedAspectRatio(850.0 / 650.0);

    // 2. Den Haupt-Container sichtbar machen
    addAndMakeVisible(contentComponent);

    // 3. GIF-Bilder aus AppData laden (bzw. bei Bedarf aus BinaryData als Standard entpacken)
    juce::File dawDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("DAW Notes");
    if (!dawDir.exists())
        dawDir.createDirectory();

    // 3.1 Header GIF entpacken/laden
    juce::File headerFile = dawDir.getChildFile("header_banner.gif");
    if (!headerFile.existsAsFile())
    {
        headerFile.replaceWithData(BinaryData::header_banner_gif, BinaryData::header_banner_gifSize);
    }
    if (headerFile.existsAsFile())
        headerGif = juce::ImageFileFormat::loadFrom(headerFile);

    // 3.2 Footer GIF entpacken/laden
    juce::File footerFile = dawDir.getChildFile("footer_banner.gif");
    if (!footerFile.existsAsFile())
    {
        footerFile.replaceWithData(BinaryData::footer_banner_gif, BinaryData::footer_banner_gifSize);
    }
    if (footerFile.existsAsFile())
        footerGif = juce::ImageFileFormat::loadFrom(footerFile);

    auto& settings = SettingsManager::getInstance();

    // 4. Header-Label für den aktiven Song einrichten (An contentComponent hängen!)
    activeSongHeaderLabel.setFont(juce::Font(20.0f, juce::Font::bold));
    activeSongHeaderLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    activeSongHeaderLabel.setJustificationType(juce::Justification::centred);

    juce::String noSongText = settings.getText("header_no_song", juce::String::fromUTF8("Kein Song geladen – Bitte erst einen Song in der Library wählen"));
    activeSongHeaderLabel.setText(noSongText, juce::dontSendNotification);
    contentComponent.addAndMakeVisible(activeSongHeaderLabel);

    // 5. Handbuch-/Anleitungs-Textfeld (An contentComponent hängen!)
    instructionBox.setMultiLine(true);
    instructionBox.setReadOnly(true);
    instructionBox.setCaretVisible(false);
    instructionBox.setScrollbarsShown(true);
    instructionBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff141414));
    instructionBox.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff333333));
    instructionBox.setColour(juce::TextEditor::textColourId, juce::Colour(0xffaaaaaa));
    instructionBox.setFont(juce::Font(12.5f));
    contentComponent.addAndMakeVisible(instructionBox);

    // 5.1. Button für detaillierte PDF-Anleitung einrichten
    openManualButton.setButtonText(juce::String::fromUTF8("📄 Detaillierte Anleitung (PDF) öffnen"));
    openManualButton.setFont(juce::Font(12.0f, juce::Font::underlined), false);
    openManualButton.setColour(juce::HyperlinkButton::textColourId, juce::Colours::orange);
    openManualButton.onClick = [this]() { openManualPdf(); };
    contentComponent.addAndMakeVisible(openManualButton);

    // 6. Views instanziieren mit Callbacks
    songLibraryView = std::make_unique<SongLibraryView>(
        audioProcessor.getDbManager(),
        [this](int64_t songId) { onSongSelected(songId); },
        [this]() { tabs.setCurrentTabIndex(1); },
        [this](int64_t songId, const juce::String& newTitle) {
            if (songId == activeSongId)
            {
                auto& set = SettingsManager::getInstance();
                juce::String headerPattern = set.getText("header_active_song", "Aktiver Song: %s");
                activeSongHeaderLabel.setText(headerPattern.replace("%s", newTitle), juce::dontSendNotification);

                if (songVisionView) songVisionView->loadSong(activeSongId);
            }
        }
    );

    songVisionView = std::make_unique<SongVisionView>(audioProcessor.getDbManager());
    sessionToDoView = std::make_unique<ToDoView>(audioProcessor.getDbManager(), false);
    releaseToDoView = std::make_unique<ToDoView>(audioProcessor.getDbManager(), true);

    // 7. Tabs hinzufügen (An contentComponent hängen!)
    tabs.addTab(settings.getText("tab_library", "Library"), juce::Colour(0xff222222), songLibraryView.get(), false);
    tabs.addTab(settings.getText("tab_vision", "Song Vision"), juce::Colour(0xff222222), songVisionView.get(), false);
    tabs.addTab(settings.getText("tab_session_todos", "Session ToDos"), juce::Colour(0xff222222), sessionToDoView.get(), false);
    tabs.addTab(settings.getText("tab_release_todos", "Release ToDos"), juce::Colour(0xff222222), releaseToDoView.get(), false);

    tabs.onTabChanged = [this](int newIndex) {
        if (songVisionView)
            songVisionView->saveCurrentSongData();

        if (newIndex == 0 && songLibraryView != nullptr)
            songLibraryView->refreshSongList();

        updateInstructionText(newIndex);
        };

    contentComponent.addAndMakeVisible(tabs);
    updateTabEnableState();

    updateInstructionText(0);

    // 8. Beim Öffnen des Editors den aktuell in der DAW gespeicherten Song abrufen
    onSongSelected(audioProcessor.getActiveSongId());
}

DawNotesAudioProcessorEditor::~DawNotesAudioProcessorEditor()
{
    tabs.clearTabs();
}

void DawNotesAudioProcessorEditor::openManualPdf()
{
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DAW Notes");

    if (!appDataDir.exists())
        appDataDir.createDirectory();

    juce::File pdfFile = appDataDir.getChildFile("Anleitung.pdf");

    if (!pdfFile.existsAsFile())
    {
        pdfFile.replaceWithData(BinaryData::Anleitung_pdf, BinaryData::Anleitung_pdfSize);
    }

    if (pdfFile.existsAsFile())
    {
        pdfFile.startAsProcess();
    }
}

void DawNotesAudioProcessorEditor::updateInstructionText(int tabIndex)
{
    auto& settings = SettingsManager::getInstance();
    juce::String text;

    switch (tabIndex)
    {
    case 0:
        text = settings.getText("help_library", juce::String::fromUTF8("LIBRARY:\n• Wähle einen Song aus der Liste, um ihn zu bearbeiten.\n• Nutze 'Neuer Song', um ein neues Projekt hinzuzufügen."));
        break;
    case 1:
        text = settings.getText("help_vision", juce::String::fromUTF8("SONG VISION:\n• Halte deine Ideen, Stimmung und Referenztracks fest.\n• Nutze Keywords/Moods zur Kategorisierung."));
        break;
    case 2:
        text = settings.getText("help_session_todos", juce::String::fromUTF8("SESSION TO-DOS:\n• Aufgaben für die aktuelle Recording-/Mixing-Session.\n• Mit 'Pin' bleiben ToDos für ALLE Songs sichtbar."));
        break;
    case 3:
        text = settings.getText("help_release_todos", juce::String::fromUTF8("RELEASE TO-DOS:\n• Checkliste für Master, Artwork, Distribution & Promotion.\n• Klicke auf die Spaltenköpfe, um zu sortieren."));
        break;
    default:
        text = "";
        break;
    }

    text = text.replace("\\n", "\n");
    instructionBox.setText(text, juce::dontSendNotification);
}

void DawNotesAudioProcessorEditor::onSongSelected(int64_t songId)
{
    activeSongId = songId;

    // Dem AudioProcessor mitteilen, welcher Song gewählt ist (damit die DAW es im Preset speichert)
    audioProcessor.setActiveSongId(songId);

    auto& settings = SettingsManager::getInstance();

    if (activeSongId >= 0)
    {
        auto songs = audioProcessor.getDbManager().getAllSongs();
        for (const auto& s : songs)
        {
            if (s.id == activeSongId)
            {
                juce::String headerPattern = settings.getText("header_active_song", "Aktiver Song: %s");
                activeSongHeaderLabel.setText(headerPattern.replace("%s", s.title), juce::dontSendNotification);
                break;
            }
        }

        if (songVisionView)   songVisionView->loadSong(activeSongId);
        if (sessionToDoView) sessionToDoView->loadToDosForSong(activeSongId);
        if (releaseToDoView) releaseToDoView->loadToDosForSong(activeSongId);
    }
    else
    {
        activeSongHeaderLabel.setText(settings.getText("header_no_song", juce::String::fromUTF8("Kein Song geladen – Bitte erst einen Song in der Library wählen")), juce::dontSendNotification);
    }

    updateTabEnableState();
}

void DawNotesAudioProcessorEditor::updateTabEnableState()
{
    bool hasActiveSong = (activeSongId >= 0);

    for (int i = 1; i < tabs.getNumTabs(); ++i)
    {
        tabs.setTabBackgroundColour(i, hasActiveSong ? juce::Colour(0xff222222) : juce::Colour(0xff111111));
    }

    if (auto* bar = dynamic_cast<juce::TabbedButtonBar*>(tabs.getChildComponent(0)))
    {
        for (int i = 1; i < tabs.getNumTabs(); ++i)
        {
            if (auto* tabButton = bar->getTabButton(i))
            {
                tabButton->setEnabled(hasActiveSong);
            }
        }
    }
}

// Der Editor muss selbst nur noch einen dunklen Hintergrund zeichnen
void DawNotesAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181818));
}

void DawNotesAudioProcessorEditor::resized()
{
    // 1. Festes Layout für die Inhaltskomponente definieren
    contentComponent.setBounds(0, 0, 850, 650);

    // 2. Skalierungsfaktor ohne Konflikte auf das Unter-Element (contentComponent) anwenden
    float scaleX = (float)getWidth() / 850.0f;
    float scaleY = (float)getHeight() / 650.0f;
    float scale = std::min(scaleX, scaleY);

    contentComponent.setTransform(juce::AffineTransform::scale(scale));

    // 3. Inneres Layout von contentComponent setzen
    juce::Rectangle<int> area(0, 0, 850, 650);

    area.removeFromTop(60); // Header GIF Platz

    auto songLabelArea = area.removeFromTop(34).reduced(10, 2);
    activeSongHeaderLabel.setBounds(songLabelArea);

    area.removeFromBottom(40); // Footer GIF Platz

    // Unteren Hilfebereich (85px) in Kurzanleitung + PDF-Link-Button aufteilen
    auto bottomHelpArea = area.removeFromBottom(85).reduced(10, 2);
    openManualButton.setBounds(bottomHelpArea.removeFromBottom(18));
    instructionBox.setBounds(bottomHelpArea);

    tabs.setBounds(area);
}