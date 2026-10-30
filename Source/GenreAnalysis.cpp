#pragma execution_character_set("utf-8")

#include "GenreAnalysis.h"
#include <cmath>
#include "SettingsManager.h"

namespace
{
    using Status = AnalysisFinding::Status;

    GenreProfile makeProfile(const juce::String& name, const juce::String& aliases,
                             double bpmMin, double bpmMax, double lufsMin, double lufsMax,
                             double plrMin, double plrMax, double lraMin, double lraMax,
                             std::array<double, AnalysisMetrics::numBands> bands,
                             double lowCorrelationMin = 0.8, double widthMin = -15.0, double widthMax = -6.0)
    {
        GenreProfile p;
        p.name = name;
        p.aliases = juce::StringArray::fromTokens(aliases, "|", "");
        p.bpmMin = bpmMin; p.bpmMax = bpmMax;
        p.lufsMin = lufsMin; p.lufsMax = lufsMax;
        p.plrMin = plrMin; p.plrMax = plrMax;
        p.lraMin = lraMin; p.lraMax = lraMax;
        p.bandTargets = bands;
        p.lowCorrelationMin = lowCorrelationMin;
        p.widthMin = widthMin; p.widthMax = widthMax;
        return p;
    }

    juce::String fmt(double v, int decimals = 1) { return juce::String(v, decimals); }

    juce::String formatDuration(double seconds)
    {
        const int total = (int)std::round(seconds);
        return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
    }

    Status rangeStatus(double value, double minV, double maxV, double warnMargin)
    {
        if (value >= minV && value <= maxV) return Status::Good;
        if (value >= minV - warnMargin && value <= maxV + warnMargin) return Status::Warning;
        return Status::Problem;
    }

    // Tonart-Text ("A Minor", "Am", "fis-Moll", "Eb Dur" ...) -> Tonika (0 = C) und Tongeschlecht
    bool parseKey(const juce::String& text, int& pitchClass, bool& isMinor)
    {
        auto s = text.toLowerCase().trim();
        if (s.isEmpty()) return false;

        const juce::String letters = "cdefgabh";
        static const int pcs[] = { 0, 2, 4, 5, 7, 9, 11, 11 };
        const int idx = letters.indexOfChar(s[0]);
        if (idx < 0) return false;

        pitchClass = pcs[idx];
        int i = 1;
        auto rest = s.substring(i);

        if (rest.startsWith("#") || rest.startsWith(juce::String::fromUTF8("♯"))) { ++pitchClass; i += 1; }
        else if (rest.startsWith("is")) { ++pitchClass; i += 2; }
        else if (rest.startsWith("b") || rest.startsWith(juce::String::fromUTF8("♭"))) { --pitchClass; i += 1; }
        else if (rest.startsWith("es")) { --pitchClass; i += 2; }
        else if ((s[0] == 'e' || s[0] == 'a') && rest.startsWith("s")) { --pitchClass; i += 1; }

        pitchClass = (pitchClass + 12) % 12;
        rest = s.substring(i).trim().trimCharactersAtStart("- ");

        isMinor = (rest.startsWith("m") && !rest.startsWith("maj")) || rest.contains("moll") || rest.contains("min");
        return true;
    }

    const char* bandAdviceLow[AnalysisMetrics::numBands] = {
        "Zu wenig Sub-Bass: Kick/Bass-Fundament (40-60 Hz) stärken, z.B. Sub-Layer oder Low-Shelf.",
        "Bassbereich zu dünn: Kick und Bass mehr Körper geben (80-200 Hz).",
        "Tiefe Mitten sehr schlank: Etwas Wärme bei 250-500 Hz kann den Mix voller machen.",
        "Mitten zurückhaltend: Lead, Vocals oder Synths könnten mehr Präsenz bei 0,5-2 kHz vertragen.",
        "Wenig Präsenz: Der Mix wirkt evtl. dumpf - 2-6 kHz vorsichtig anheben (Vocals, Snare, Leads).",
        "Wenig Höhen/Air: Hi-Hats, Becken oder ein sanftes High-Shelf ab 8 kHz bringen Glanz."
    };

    const char* bandAdviceHigh[AnalysisMetrics::numBands] = {
        "Sehr viel Sub-Bass: Gefahr von Dröhnen/verschenktem Headroom - Sub entschlacken oder High-Pass auf Nicht-Bass-Spuren.",
        "Bassbereich dominant: Kick und Bass per Sidechain/EQ entzerren, Mud bei 100-200 Hz prüfen.",
        "Tiefe Mitten zu voll (\"Mulm\"): Bei 250-500 Hz auf Pads, Gitarren, Synths etwas absenken.",
        "Mitten sehr präsent: Kann nasal/boxy klingen - Überlagerungen bei 0,5-2 kHz ausdünnen.",
        "Präsenzbereich scharf: Ermüdungsgefahr - 2-6 kHz zähmen, De-Esser oder dynamischer EQ.",
        "Sehr viele Höhen: Kann zischeln - Hi-Hats/Becken leiser oder High-Shelf ab 8 kHz absenken."
    };
}

namespace GenreAnalysis
{
    const std::vector<GenreProfile>& getProfiles()
    {
        // Bänder: Sub (20-60), Bass (60-250), Tiefe Mitten (250-500), Mitten (0,5-2k), Präsenz (2-6k), Air (6-20k)
        static const std::vector<GenreProfile> profiles = {
            makeProfile(tr("ga_profile_general", "Allgemein"), "", 60, 200, -14, -7, 7, 14, 3, 12, { -6.5, -4.5, -9.5, -8.5, -11.5, -14.0 }, 0.7),
            makeProfile("Dark Electro / EBM / Industrial", "dark electro|darkelectro|ebm|industrial|aggrotech|futurepop|electro-industrial|hellektro|harsh",
                        115, 150, -10, -7, 6, 10, 3, 8, { -6.5, -4.5, -9.5, -8.5, -11.5, -14.0 }),
            makeProfile("Techno", "techno|hard techno|industrial techno|schranz", 125, 150, -9, -6, 6, 10, 3, 8, { -5.0, -4.5, -10.0, -9.5, -12.5, -14.0 }, 0.85),
            makeProfile("House", "house|deep house|tech house|garage", 118, 130, -10, -7, 7, 11, 3, 8, { -5.5, -4.5, -10.0, -9.0, -12.5, -14.5 }, 0.85),
            makeProfile("Trance", "trance|psytrance|psy", 128, 145, -9, -6.5, 6, 10, 3, 8, { -6.0, -4.5, -10.0, -9.0, -12.0, -13.5 }, 0.85),
            makeProfile("Drum & Bass", "drum|dnb|d&b|jungle|neurofunk", 160, 178, -9, -6, 6, 10, 3, 8, { -4.5, -5.0, -10.5, -9.5, -12.0, -14.0 }, 0.85),
            makeProfile("Dubstep / Bass Music", "dubstep|riddim|bass music|brostep", 136, 150, -8, -5, 5, 9, 3, 8, { -4.0, -5.0, -10.0, -9.5, -12.0, -14.0 }, 0.85),
            makeProfile("Synthwave / Synthpop", "synthwave|synthpop|synth pop|retrowave|darkwave|new wave|coldwave|80s", 80, 130, -11, -8, 7, 12, 3, 9, { -7.0, -4.5, -9.5, -8.5, -11.5, -14.0 }),
            makeProfile("Hip-Hop / Trap", "hip|rap|trap|drill|boom bap", 65, 100, -10, -7, 6, 10, 3, 8, { -4.0, -5.0, -10.5, -10.0, -13.0, -15.0 }, 0.85),
            makeProfile("Pop", "pop|dance|edm|electro", 90, 130, -11, -8, 7, 11, 3, 8, { -6.5, -4.5, -9.5, -8.5, -11.5, -14.0 }),
            makeProfile("Rock / Punk", "rock|punk|grunge|indie|alternative", 90, 170, -11, -8, 7, 12, 4, 10, { -9.0, -4.5, -8.5, -7.5, -10.5, -14.0 }, 0.7),
            makeProfile("Metal", "metal|metalcore|djent|doom", 100, 200, -10, -7, 6, 10, 3, 8, { -9.5, -4.5, -8.5, -7.5, -10.0, -14.0 }, 0.7),
            makeProfile("Ambient / Chillout", "ambient|chill|downtempo|lofi|lo-fi|drone|soundscape|cinematic|filmmusik", 60, 110, -18, -12, 10, 18, 6, 16, { -6.5, -4.5, -9.0, -8.5, -11.5, -13.5 }, 0.6, -13.0, -3.0)
        };
        return profiles;
    }

    int findProfileIndex(const juce::String& genreText)
    {
        const auto text = genreText.toLowerCase();
        if (text.isEmpty()) return 0;

        // Spezifische Profile zuerst prüfen ("dark electro" vor "electro", "synthpop" vor "pop")
        const auto& profiles = getProfiles();
        for (int i = 1; i < (int)profiles.size(); ++i)
            for (const auto& alias : profiles[(size_t)i].aliases)
                if (alias.isNotEmpty() && text.contains(alias))
                    return i;

        return 0;
    }

    juce::String getBandName(int band)
    {
        static const char* names[AnalysisMetrics::numBands] = {
            "Sub-Bass (20-60 Hz)", "Bass (60-250 Hz)", "Tiefe Mitten (250-500 Hz)",
            "Mitten (0,5-2 kHz)", "Präsenz (2-6 kHz)", "Air (6-20 kHz)"
        };
        const int b = juce::jlimit(0, AnalysisMetrics::numBands - 1, band);
        return tr(("ga_band_name_" + juce::String(b)).toRawUTF8(), names[b]);
    }

    GenreEvaluation evaluate(const AnalysisMetrics& m, const GenreProfile& p,
                             double songVisionBpm, const juce::String& songVisionKey)
    {
        GenreEvaluation eval;
        double scoreSum = 0.0, weightSum = 0.0;

        auto add = [&](Status status, double weight, const juce::String& label, const juce::String& value,
                       const juce::String& target, const juce::String& advice) {
            eval.findings.push_back({ status, label, value, target, advice });
            if (status == Status::Info) return;
            weightSum += weight;
            scoreSum += weight * (status == Status::Good ? 1.0 : status == Status::Warning ? 0.5 : 0.0);
        };


        // --- Lautheit
        {
            auto st = rangeStatus(m.integratedLufs, p.lufsMin, p.lufsMax, 2.0);
            juce::String advice = st == Status::Good ? tr("ga_lautheit_passt_zum_genre", "Lautheit passt zum Genre.")
                : m.integratedLufs < p.lufsMin ? tr("ga_leiser_als_im_genre", "Leiser als im Genre üblich: Limiter/Clipper am Master etwas mehr arbeiten lassen oder den Mix verdichten (Bus-Kompression).")
                : tr("ga_lauter_als_noetig_streaming", "Lauter als nötig: Streaming-Dienste regeln ohnehin herunter - etwas weniger Limiting lässt mehr Punch übrig.");
            add(st, 2.0, tr("ga_label_loudness", "Lautheit (integriert)"), fmt(m.integratedLufs) + " LUFS",
                fmt(p.lufsMin, 0) + tr("ga_range_to", " bis ") + fmt(p.lufsMax, 0) + " LUFS", advice);
        }

        // --- Spitzenpegel
        {
            Status st = Status::Good;
            juce::String advice = tr("ga_genug_abstand_zu_0", "Genug Abstand zu 0 dBFS.");
            if (m.clippedSamples > 0 || m.samplePeakDb > -0.1)
            {
                st = Status::Problem;
                advice = tr("ga_clipping_gefahr", "Clipping-Gefahr (") + juce::String(m.clippedSamples) + tr("ga_samples_an_0_dbfs", " Samples an 0 dBFS): Limiter-Ceiling auf -1 dBTP setzen, sonst verzerrt es nach MP3/AAC-Encoding.");
            }
            else if (m.samplePeakDb > -1.0)
            {
                st = Status::Warning;
                advice = tr("ga_sehr_knapp_unter_0", "Sehr knapp unter 0 dBFS: Für Streaming wird -1 dBTP empfohlen (Intersample-Peaks).");
            }
            add(st, 1.0, tr("ga_label_peak", "Spitzenpegel (Sample Peak)"), fmt(m.samplePeakDb) + " dBFS", juce::String::fromUTF8("≤ -1.0 dBFS"), advice);
        }

        // --- Dynamik (PLR)
        {
            const double plr = m.samplePeakDb - m.integratedLufs;
            auto st = rangeStatus(plr, p.plrMin, p.plrMax, 2.0);
            juce::String advice = st == Status::Good ? tr("ga_dynamik_genre_typisch", "Dynamik genre-typisch.")
                : plr < p.plrMin ? tr("ga_stark_komprimiert_limitiert_transien", "Stark komprimiert/limitiert: Transienten (Kick, Snare) verlieren Punch - Limiter entlasten oder parallel komprimieren.")
                : tr("ga_mehr_dynamik_als_im", "Mehr Dynamik als im Genre üblich: Bus-Kompression oder Sättigung macht den Track dichter und konkurrenzfähiger.");
            add(st, 1.5, tr("ga_label_dynamics", "Dynamik (Peak to Loudness)"), fmt(plr) + " dB", fmt(p.plrMin, 0) + tr("ga_range_to", " bis ") + fmt(p.plrMax, 0) + " dB", advice);
        }

        // --- Loudness Range
        {
            auto st = rangeStatus(m.loudnessRange, p.lraMin, p.lraMax, 2.0);
            juce::String advice = st == Status::Good ? tr("ga_lautstaerkeverlauf_ueber_den_song", "Lautstärkeverlauf über den Song passt.")
                : m.loudnessRange < p.lraMin ? tr("ga_kaum_lautstaerke_entwicklung_breakdo", "Kaum Lautstärke-Entwicklung: Breakdowns/Intros etwas zurücknehmen, damit Drops und Refrains stärker wirken.")
                : tr("ga_sehr_grosse_lautstaerkespruenge_leis", "Sehr große Lautstärkesprünge: Leise Passagen anheben oder Automation glätten, damit der Song nicht \"zerfällt\".");
            add(st, 1.0, tr("ga_label_lra", "Loudness Range (Spannungsbogen)"), fmt(m.loudnessRange) + " LU", fmt(p.lraMin, 0) + tr("ga_range_to", " bis ") + fmt(p.lraMax, 0) + " LU", advice);
        }

        // --- Stereo-Korrelation
        {
            Status st = Status::Good;
            juce::String advice = tr("ga_phasenlage_unkritisch", "Phasenlage unkritisch.");
            if (m.correlation < 0.0) { st = Status::Problem; advice = tr("ga_negative_korrelation_phasenprobleme_", "Negative Korrelation: Phasenprobleme! Mono-Check machen, Stereo-Widener/Haas-Effekte prüfen."); }
            else if (m.correlation < 0.2) { st = Status::Warning; advice = tr("ga_sehr_breit_phasig_im", "Sehr breit/phasig: Im Club oder auf dem Handy (Mono) können Elemente verschwinden."); }
            else if (m.correlation > 0.97) { st = Status::Warning; advice = tr("ga_nahezu_mono_pads_hallraeume", "Nahezu Mono: Pads, Hallräume oder Doppelungen breiter verteilen."); }
            add(st, 1.0, tr("ga_label_correlation", "Stereo-Korrelation"), fmt(m.correlation, 2), tr("ga_target_correlation", "0.20 bis 0.97"), advice);
        }

        // --- Stereobreite
        {
            auto st = rangeStatus(m.sideToMidDb, p.widthMin, p.widthMax, 3.0);
            juce::String advice = st == Status::Good ? tr("ga_stereobild_genre_typisch", "Stereobild genre-typisch.")
                : m.sideToMidDb < p.widthMin ? tr("ga_eher_schmales_stereobild_pads", "Eher schmales Stereobild: Pads/FX/Backings mehr panoramieren oder Stereo-Hall nutzen.")
                : tr("ga_sehr_breites_stereobild_die", "Sehr breites Stereobild: Die Mitte (Kick, Bass, Lead, Vocals) kann dadurch an Druck verlieren.");
            add(st, 1.0, tr("ga_label_width", "Stereobreite (Side/Mid)"), fmt(m.sideToMidDb) + " dB", fmt(p.widthMin, 0) + tr("ga_range_to", " bis ") + fmt(p.widthMax, 0) + " dB", advice);
        }

        // --- Bass-Mono
        {
            Status st = m.lowCorrelation >= p.lowCorrelationMin ? Status::Good
                      : m.lowCorrelation >= p.lowCorrelationMin - 0.25 ? Status::Warning : Status::Problem;
            juce::String advice = st == Status::Good ? tr("ga_bassbereich_steht_stabil_in", "Bassbereich steht stabil in der Mitte.")
                : tr("ga_bass_unter_120_hz", "Bass unter 120 Hz ist zu stereo: Bass-Mono (z.B. Utility/Mono-Maker unter 120 Hz) sorgt für Druck im Club und auf Vinyl.");
            add(st, 1.5, tr("ga_label_bass_mono", "Bass-Mono (< 120 Hz)"), fmt(m.lowCorrelation, 2), juce::String::fromUTF8("≥ ") + fmt(p.lowCorrelationMin, 2), advice);
        }

        // --- Frequenzbalance
        for (int b = 0; b < AnalysisMetrics::numBands; ++b)
        {
            const double diff = m.bandDb[(size_t)b] - p.bandTargets[(size_t)b];
            const double absDiff = std::abs(diff);
            Status st = absDiff <= p.bandTolerance ? Status::Good : absDiff <= 2.0 * p.bandTolerance ? Status::Warning : Status::Problem;
            juce::String advice = st == Status::Good ? tr("ga_im_rahmen", "Im Rahmen.")
                : tr(((diff < 0 ? "ga_band_low_" : "ga_band_high_") + juce::String(b)).toRawUTF8(), diff < 0 ? bandAdviceLow[b] : bandAdviceHigh[b]);
            add(st, 1.0, getBandName(b), fmt(m.bandDb[(size_t)b]) + " dB (" + (diff >= 0 ? "+" : "") + fmt(diff) + ")",
                fmt(p.bandTargets[(size_t)b]) + juce::String::fromUTF8(" dB ± ") + fmt(p.bandTolerance), advice);
        }

        // --- Tempo
        if (m.tempoBpm > 0.0)
        {
            const double t = m.tempoBpm;
            const bool inRange = t >= p.bpmMin - 1.0 && t <= p.bpmMax + 1.0;
            const bool halfDouble = (t * 2.0 >= p.bpmMin - 1.0 && t * 2.0 <= p.bpmMax + 1.0) || (t / 2.0 >= p.bpmMin - 1.0 && t / 2.0 <= p.bpmMax + 1.0);

            Status st = inRange || halfDouble ? Status::Good : Status::Warning;
            juce::String advice = inRange ? tr("ga_tempo_genre_typisch", "Tempo genre-typisch.")
                : halfDouble ? tr("ga_passt_als_half_double", "Passt als Half-/Double-Time zum Genre.")
                : tr("ga_tempo_ungewoehnlich_fuer_das", "Tempo ungewöhnlich für das Genre - kann bewusst sein, sonst Projekt-Tempo anpassen.");

            juce::String source = m.tempoFromHost ? tr("ga_daw", " (DAW)") : tr("ga_geschaetzt", " (geschätzt)");
            add(st, 2.0, tr("ga_label_tempo", "Tempo"), fmt(t, m.tempoFromHost ? 2 : 1) + " BPM" + source, fmt(p.bpmMin, 0) + tr("ga_range_to", " bis ") + fmt(p.bpmMax, 0) + " BPM", advice);

            if (songVisionBpm > 0.0)
            {
                const double ratio = t / songVisionBpm;
                const bool same = std::abs(t - songVisionBpm) <= 1.5 || std::abs(ratio - 2.0) < 0.03 || std::abs(ratio - 0.5) < 0.015;
                if (!same)
                    add(Status::Info, 0.0, tr("ga_label_tempo_vision", "Tempo laut Song Vision"), fmt(songVisionBpm, 1) + " BPM", "",
                        tr("ga_weicht_vom_gemessenen_tempo", "Weicht vom gemessenen Tempo ab - BPM in der Song Vision aktualisieren?"));
            }
        }

        // --- Tonart (nur Info, da kein Genre-Kriterium)
        if (m.detectedKey.isNotEmpty())
        {
            const bool unsure = m.keyConfidence < 0.05;
            juce::String advice = unsure ? tr("ga_erkennung_unsicher_viel_perkussion", "Erkennung unsicher (viel Perkussion oder modale Harmonik).") : tr("ga_erkannt_ueber_das_chromagramm", "Erkannt über das Chromagramm.");

            int detPc = 0, visPc = 0;
            bool detMinor = false, visMinor = false;
            if (parseKey(m.detectedKey, detPc, detMinor) && parseKey(songVisionKey, visPc, visMinor))
            {
                const bool relative = (detMinor != visMinor) && ((detMinor ? (detPc + 3) % 12 : (detPc + 9) % 12) == visPc);
                if (detPc == visPc && detMinor == visMinor) advice = tr("ga_stimmt_mit_der_song", "Stimmt mit der Song Vision überein.");
                else if (relative) advice = tr("ga_parallel_tonart_zur_song", "Parallel-Tonart zur Song Vision (") + songVisionKey + tr("ga_gleiche_vorzeichen_schwerpunkt_pruef", ") - gleiche Vorzeichen, Schwerpunkt prüfen.");
                else advice = tr("ga_song_vision_sagt", "Song Vision sagt \"") + songVisionKey + tr("ga_tonart_in_der_song", "\" - Tonart in der Song Vision prüfen.");
            }

            add(Status::Info, 0.0, tr("ga_label_key", "Tonart"), m.detectedKey + (unsure ? " (?)" : ""), "", advice);
        }

        // --- Allgemeine Infos
        add(Status::Info, 0.0, tr("ga_laenge_gehoert", "Länge (gehört)"), formatDuration(m.durationSeconds), "",
            m.durationSeconds < 60.0 ? tr("ga_sehr_kurzer_durchlauf_fuer", "Sehr kurzer Durchlauf - für eine aussagekräftige Analyse den kompletten Song abspielen.") : juce::String());

        if (m.integratedLufs > -14.0)
            add(Status::Info, 0.0, tr("ga_label_streaming", "Streaming-Normalisierung"), "-" + fmt(m.integratedLufs + 14.0) + " dB", tr("ga_target_streaming", "Spotify/YouTube ca. -14 LUFS"),
                tr("ga_um_so_viel_wird", "Um so viel wird der Track dort leiser gedreht - zu viel Limiting bringt also keinen Lautheitsvorteil."));

        eval.fitPercent = weightSum > 0.0 ? (int)std::round(100.0 * scoreSum / weightSum) : 0;
        return eval;
    }

    std::vector<std::pair<int, int>> rankProfiles(const AnalysisMetrics& metrics)
    {
        const auto& profiles = getProfiles();
        std::vector<std::pair<int, int>> ranking;
        for (int i = 1; i < (int)profiles.size(); ++i)
            ranking.push_back({ i, evaluate(metrics, profiles[(size_t)i], 0.0, {}).fitPercent });

        std::stable_sort(ranking.begin(), ranking.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        return ranking;
    }

    namespace
    {
        // Klangliche Eckdaten relativ zu einer "neutralen" Mischung
        struct Character
        {
            bool minor = false, hasKey = false;
            double tempo = 0.0, highs = 0.0, lows = 0.0, width = 0.0, plr = 0.0;
            bool strongBeat = false, weakBeat = false;
        };

        Character characterOf(const AnalysisMetrics& m)
        {
            const auto& neutral = getProfiles().front().bandTargets;
            Character c;
            int pc = 0;
            c.hasKey = parseKey(m.detectedKey, pc, c.minor);
            c.tempo = m.tempoBpm;
            c.highs = 0.5 * ((m.bandDb[4] - neutral[4]) + (m.bandDb[5] - neutral[5]));
            c.lows = 0.5 * ((m.bandDb[0] - neutral[0]) + (m.bandDb[1] - neutral[1]));
            c.width = m.sideToMidDb;
            c.plr = m.samplePeakDb - m.integratedLufs;
            c.strongBeat = m.rhythmStrength >= 2.2;
            c.weakBeat = m.rhythmStrength > 0.0 && m.rhythmStrength < 1.6;
            return c;
        }
    }

    std::vector<int> suggestMoodIds(const AnalysisMetrics& m, int maxMoods)
    {
        const auto c = characterOf(m);
        const double t = c.tempo, L = m.integratedLufs, lra = m.loudnessRange;
        const bool major = c.hasKey && !c.minor, minor = c.hasKey && c.minor;

        // Reihenfolge = Priorität. IDs entsprechen den Standard-Moods der settings.ini
        const std::vector<std::pair<int, bool>> rules = {
            { 6,  L > -8.0 && c.plr < 8.0 && m.bandDb[4] > -11.0 },           // Aggressiv
            { 3,  minor && c.highs < -1.5 },                                    // Düster
            { 7,  c.strongBeat && t >= 115.0 },                                  // Treibend
            { 1,  t >= 120.0 && L > -10.0 },                                     // Energetisch
            { 10, c.strongBeat && lra < 4.0 && t >= 110.0 },                     // Hypnotisch
            { 12, major && t >= 120.0 && lra >= 5.0 },                           // Euphorisierend
            { 2,  minor && t > 0.0 && t < 110.0 && L < -9.0 },                   // Melancholisch
            { 13, lra > 8.0 || (minor && lra > 6.0) },                           // Dramatisch
            { 8,  c.width > -9.0 && (lra > 6.0 || (t > 0.0 && t < 100.0)) },     // Atmosphärisch
            { 14, minor && c.width > -10.0 && c.highs < 0.0 },                   // Mystisch
            { 15, c.strongBeat && t >= 90.0 && t <= 125.0 },                     // Groovig
            { 4,  major && t >= 100.0 },                                         // Fröhlich
            { 18, major && c.highs > 1.5 && t >= 100.0 },                        // Verspielt
            { 9,  t > 0.0 && t < 100.0 && lra > 5.0 },                           // Gefühlvoll
            { 5,  t > 0.0 && t < 100.0 && L < -11.0 },                           // Entspannt
            { 16, (L < -16.0 && !c.strongBeat) || c.weakBeat },                  // Ruhig
            { 17, lra < 3.0 && c.strongBeat }                                    // Monoton
        };

        std::vector<int> ids;
        for (const auto& [id, matches] : rules)
            if (matches && (int)ids.size() < maxMoods)
                ids.push_back(id);
        return ids;
    }

    juce::String describe(const AnalysisMetrics* ref, const juce::String& refName,
                          const AnalysisMetrics* own, const juce::String& genreText)
    {

        auto soundOf = [&](const AnalysisMetrics& m) {
            const auto c = characterOf(m);
            juce::StringArray words;
            words.add(c.lows > 2.0 ? tr("ga_sehr_bassbetont", "sehr bassbetont") : c.lows < -2.0 ? tr("ga_schlank_im_bass", "schlank im Bass") : tr("ga_ausgewogener_bass", "ausgewogener Bass"));
            words.add(c.highs > 2.0 ? tr("ga_hell_brillant", "hell/brillant") : c.highs < -2.0 ? tr("ga_dunkel_warm", "dunkel/warm") : tr("ga_neutrale_hoehen", "neutrale Höhen"));
            words.add(c.width > -8.0 ? tr("ga_breites_stereobild", "breites Stereobild") : c.width < -15.0 ? tr("ga_eher_schmal_mittig", "eher schmal/mittig") : tr("ga_normale_stereobreite", "normale Stereobreite"));
            words.add(c.plr < 8.0 ? tr("ga_stark_verdichtet", "stark verdichtet") : c.plr > 12.0 ? tr("ga_viel_dynamik", "viel Dynamik") : tr("ga_moderat_komprimiert", "moderat komprimiert"));
            if (c.strongBeat) words.add(tr("ga_klarer_durchgehender_beat", "klarer, durchgehender Beat"));
            else if (c.weakBeat) words.add(tr("ga_kaum_beat", "kaum Beat"));
            return words.joinIntoString(", ");
        };

        auto tempoText = [&](const AnalysisMetrics& m) {
            return m.tempoBpm > 0.0 ? juce::String(m.tempoBpm, 1) + " BPM" : tr("ga_kein_klares_tempo", "kein klares Tempo");
        };

        juce::String text;

        if (ref != nullptr)
        {
            text << tr("ga_referenz", "REFERENZ") << (refName.isNotEmpty() ? " \"" + refName + "\"" : juce::String()) << ":\n"
                 << tempoText(*ref) << ", " << (ref->detectedKey.isNotEmpty() ? ref->detectedKey : tr("ga_tonart_unklar", "Tonart unklar"))
                 << ", " << juce::String(ref->integratedLufs, 1) << " LUFS, " << tr("ga_dynamics_word", "Dynamik") << " " << juce::String(ref->samplePeakDb - ref->integratedLufs, 1) << " dB.\n"
                 << tr("ga_klangbild", "Klangbild: ") << soundOf(*ref) << ".\n";

            if (ref->chordProgression.isNotEmpty())
                text << tr("ga_akkord_schema_geschaetzt", "Akkord-Schema (geschätzt): ") << ref->chordProgression << "\n";

            const auto ranking = rankProfiles(*ref);
            if (!ranking.empty())
            {
                juce::StringArray genres;
                for (size_t i = 0; i < ranking.size() && i < 3; ++i)
                    genres.add(getProfiles()[(size_t)ranking[i].first].name + " (" + juce::String(ranking[i].second) + " %)");
                text << tr("ga_klingt_technisch_nach", "Klingt technisch nach: ") << genres.joinIntoString(", ") << "\n";
            }
        }

        if (own != nullptr)
        {
            if (text.isNotEmpty()) text << "\n";

            const auto& profiles = getProfiles();
            const auto& profile = profiles[(size_t)findProfileIndex(genreText)];
            const auto eval = evaluate(*own, profile, 0.0, {});

            text << tr("ga_dein_song_letzte_analyse", "DEIN SONG (letzte Analyse):\n")
                 << tempoText(*own) << ", " << juce::String(own->integratedLufs, 1) << " LUFS, "
                 << tr("ga_passung", "Passung ") << profile.name << ": " << eval.fitPercent << " %.\n"
                 << tr("ga_klangbild", "Klangbild: ") << soundOf(*own) << ".\n";

            juce::StringArray problems;
            for (const auto& f : eval.findings)
                if (f.status == AnalysisFinding::Status::Problem && problems.size() < 3)
                    problems.add(f.label);
            if (problems.size() > 0)
                text << tr("ga_groesste_baustellen", "Größte Baustellen: ") << problems.joinIntoString(", ") << "\n";

            if (ref != nullptr)
            {
                juce::StringArray diffs;
                const double loud = own->integratedLufs - ref->integratedLufs;
                if (std::abs(loud) >= 1.0)
                    diffs.add(juce::String(std::abs(loud), 1) + (loud < 0 ? tr("ga_lu_leiser", " LU leiser") : tr("ga_lu_lauter", " LU lauter")));

                for (int b = 0; b < AnalysisMetrics::numBands; ++b)
                {
                    const double d = own->bandDb[(size_t)b] - ref->bandDb[(size_t)b];
                    if (std::abs(d) >= 2.5)
                        diffs.add(getBandName(b).upToFirstOccurrenceOf(" (", false, false) + (d > 0 ? " +" : " ") + juce::String(d, 1) + " dB");
                }

                const double width = own->sideToMidDb - ref->sideToMidDb;
                if (std::abs(width) >= 3.0)
                    diffs.add(width > 0 ? tr("ga_breiter", "breiter") : tr("ga_schmaler", "schmaler"));

                text << tr("ga_im_vergleich_zur_referenz", "Im Vergleich zur Referenz: ")
                     << (diffs.isEmpty() ? tr("ga_sehr_aehnlich_abgestimmt", "sehr ähnlich abgestimmt.") : diffs.joinIntoString(", ") + ".") << "\n";
            }
        }

        if (text.isEmpty())
            text = tr("ga_noch_keine_diagnose_lade", "Noch keine Diagnose: Lade über \"Analysieren...\" einen Referenztrack oder analysiere deinen Song im Reiter \"Analyse\".");

        return text.trimEnd();
    }
}
