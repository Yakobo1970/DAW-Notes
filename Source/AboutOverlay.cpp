#pragma execution_character_set("utf-8")

#include "AboutOverlay.h"
#include "SettingsManager.h"

namespace
{
    const juce::Colour panelColour(0xff1c1c1c);
    const juce::Colour accent(0xffd9822b);

    const char* defaultStory =
        "Wie DAW Notes entstanden ist\n\n"
        "DAW Notes ist während der S.P.R.I.N.T.-Challenge von Daniel Agema (www.hearbeat.de) entstanden: "
        "sechs Wochen Training, um schneller, fokussierter und mit klaren Zielen zu producen.\n\n"
        "Das Prinzip: eine Stunde pro Producer-Tag. Zuerst entsteht ein 8- bis 16-taktiger Prototyp – "
        "egal wie weit man kommt. Dann bleibt er 7 bis 10 Tage liegen, ohne ihn anzuhören.\n\n"
        "Beim ersten frischen Hören kommt DAW Notes ins Spiel: Welche Gefühle löst der Proto aus? "
        "BPM, Genre, Akkorde und Moods landen in der Song Vision – dem Rettungsanker, "
        "wenn der Song später vom Kurs abkommt.\n\n"
        "Danach wird der Proto mit Sounds überladen, ein Referenzsong gesucht und ein Ghost-Arrangement "
        "gebaut: das blanke Baugerüst eines erfolgreichen Songs, das nach und nach befüllt wird.\n\n"
        "Beim nächsten Durchhören wird nicht „ganz kurz mal“ korrigiert. Alles, was auffällt, "
        "wandert in die Session ToDos – so bleibt das Ohr frisch bis zum letzten Takt.\n\n"
        "An Tag X ist der Song fertig. Denn: Fertig kommt vor Perfekt!\n\n"
        "Die Release ToDos erinnern an alles, was danach noch ansteht: Release-Datum, Distribution, "
        "Cover, Videos für TikTok und YouTube.\n\n"
        "Mit Version 2.0 hört DAW Notes jetzt auch zu: Der Reiter Analyse vergleicht deinen Mix mit "
        "typischen Werten deines Genres, und Referenztracks verraten Tempo, Tonart, Akkorde und Stimmung.\n\n"
        "Danke an Daniel Agema für sechs intensive Wochen, an Kerstin für ihre Geduld "
        "und an die KI-Assistenten, ohne die DAW Notes nicht so schnell fertig geworden wäre.\n\n"
        "DAW Notes ist kostenlos und speichert alles lokal auf deinem Studio-Rechner. "
        "Wenn es dir hilft, freue ich mich über eine kleine Spende oder einen Besuch auf "
        "www.undabtanzbar.de – dort findest du auch meine Musik.\n\n"
        "Viel Spaß beim nächsten Song!\n\n"
        "Kay Fiedler a.k.a. Yakobo Yak / Un(d)AbtanzBAR";
}

AboutOverlay::AboutOverlay()
{
    logo = juce::ImageCache::getFromMemory(BinaryData::dawnotes_png, BinaryData::dawnotes_pngSize);

    closeButton.setButtonText(juce::String::fromUTF8("✕"));
    closeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeButton);

    donateButton.setButtonText(tr("about_spenden_via_paypal", "♥  Spenden via PayPal"));
    donateButton.setColour(juce::TextButton::buttonColourId, accent);
    donateButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    donateButton.setTooltip("paypal.me/yakobo");
    donateButton.onClick = [] { juce::URL(donateUrl).launchInDefaultBrowser(); };
    addAndMakeVisible(donateButton);

    websiteButton.setButtonText("www.undabtanzbar.de");
    websiteButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2f2f2f));
    websiteButton.onClick = [] { juce::URL(websiteUrl).launchInDefaultBrowser(); };
    addAndMakeVisible(websiteButton);

    setWantsKeyboardFocus(true);
    startTimerHz(40);
}

AboutOverlay::~AboutOverlay()
{
    stopTimer();
}

void AboutOverlay::buildStoryLayout()
{
    auto text = SettingsManager::getInstance().getText("about_story", juce::String::fromUTF8(defaultStory)).replace("\\n", "\n");

    juce::AttributedString as;
    as.setJustification(juce::Justification::centred);
    as.setLineSpacing(2.0f);

    // Erste Zeile als Überschrift
    const auto firstBreak = text.indexOf("\n");
    as.append(text.substring(0, firstBreak), juce::Font(juce::FontOptions(16.0f, juce::Font::bold)), accent);
    as.append(text.substring(firstBreak), juce::Font(juce::FontOptions(13.5f)), juce::Colour(0xffd0d0d0));

    storyLayout.createLayout(as, (float)storyArea.getWidth());
    storyHeight = storyLayout.getHeight();
}

void AboutOverlay::timerCallback()
{
    if (paused || storyArea.isEmpty()) return;

    scrollPos += 0.55f;
    if (scrollPos > storyHeight + (float)storyArea.getHeight())
        scrollPos = 0.0f;

    repaint(storyArea);
}

void AboutOverlay::paint(juce::Graphics& g)
{
    // Abgedunkelter Hintergrund
    g.fillAll(juce::Colours::black.withAlpha(0.72f));

    g.setColour(panelColour);
    g.fillRoundedRectangle(panel.toFloat(), 10.0f);
    g.setColour(accent.withAlpha(0.8f));
    g.drawRoundedRectangle(panel.toFloat().reduced(0.5f), 10.0f, 1.5f);

    auto area = panel.reduced(20, 16);

    // Kopfbereich: Logo + Titel + Version
    auto head = area.removeFromTop(74);
    if (logo.isValid())
        g.drawImageWithin(logo, head.getX(), head.getY(), 74, 74, juce::RectanglePlacement::centred);

    auto titleArea = head.withTrimmedLeft(88).withTrimmedRight(30);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(26.0f, juce::Font::bold)));
    g.drawText("DAW Notes", titleArea.removeFromTop(34), juce::Justification::bottomLeft);
    g.setColour(juce::Colour(0xffaaaaaa));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(tr("about_version", "Version ") + JucePlugin_VersionString + tr("about_notizblock_fuer_daw_user", "  ·  Notizblock für DAW-User"),
               titleArea.removeFromTop(20), juce::Justification::centredLeft);
    g.drawText(tr("about_2026_kay_yakobo_fiedler", "© 2026 Kay „Yakobo“ Fiedler a.k.a. Un(d)AbtanzBAR"),
               titleArea.removeFromTop(18), juce::Justification::centredLeft);

    // Scrollende Geschichte
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(storyArea);
        const float y = (float)storyArea.getBottom() - scrollPos;
        storyLayout.draw(g, juce::Rectangle<float>((float)storyArea.getX(), y, (float)storyArea.getWidth(), storyHeight));

        // Weiche Ein-/Ausblendung oben und unten
        const float fade = 34.0f;
        auto top = storyArea.toFloat().removeFromTop(fade);
        auto bottom = storyArea.toFloat().removeFromBottom(fade);
        g.setGradientFill(juce::ColourGradient(panelColour, top.getX(), top.getY(), panelColour.withAlpha(0.0f), top.getX(), top.getBottom(), false));
        g.fillRect(top);
        g.setGradientFill(juce::ColourGradient(panelColour.withAlpha(0.0f), bottom.getX(), bottom.getY(), panelColour, bottom.getX(), bottom.getBottom(), false));
        g.fillRect(bottom);
    }

    g.setColour(juce::Colour(0xff777777));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(paused ? tr("about_pausiert_mausrad_zum_blaettern", "Pausiert – Mausrad zum Blättern") : tr("about_maus_ueber_den_text", "Maus über den Text: anhalten"),
               storyArea.withY(storyArea.getBottom() + 2).withHeight(14), juce::Justification::centred);
}

void AboutOverlay::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre(580, 470);

    closeButton.setBounds(panel.getRight() - 40, panel.getY() + 10, 28, 28);

    auto area = panel.reduced(20, 16);
    area.removeFromTop(74 + 14);

    auto buttons = area.removeFromBottom(34);
    area.removeFromBottom(24);
    storyArea = area;

    const int w = 210;
    auto row = buttons.withSizeKeepingCentre(2 * w + 16, 34);
    donateButton.setBounds(row.removeFromLeft(w));
    row.removeFromLeft(16);
    websiteButton.setBounds(row.removeFromLeft(w));

    buildStoryLayout();

    // Beim Öffnen steht die Überschrift schon im Bild
    if (scrollPos <= 0.0f)
        scrollPos = (float)storyArea.getHeight() - 24.0f;
}

void AboutOverlay::mouseDown(const juce::MouseEvent& e)
{
    // Klick neben das Panel schließt das Fenster
    if (!panel.contains(e.getPosition()) && onClose)
        onClose();
}

void AboutOverlay::mouseMove(const juce::MouseEvent& e)
{
    const bool overStory = storyArea.contains(e.getPosition());
    if (overStory != paused)
    {
        paused = overStory;
        repaint(panel);
    }
}

void AboutOverlay::mouseExit(const juce::MouseEvent&)
{
    paused = false;
    repaint(panel);
}

void AboutOverlay::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    scrollPos = juce::jlimit(0.0f, storyHeight + (float)storyArea.getHeight(), scrollPos - wheel.deltaY * 120.0f);
    repaint(storyArea);
}
