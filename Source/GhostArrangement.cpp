#pragma execution_character_set("utf-8")

#include "GhostArrangement.h"
#include "TrackAnalyser.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include "SettingsManager.h"

namespace
{
    constexpr int fftOrder = 11, fftSize = 1 << fftOrder, hopSize = 1024;

    // Merkmale eines FFT-Frames
    struct Frame
    {
        float power = 0.0f;                 // Mittlere Leistung (linear)
        std::array<float, 6> band{};        // Sub, Bass, Tiefe Mitten, Mitten, Präsenz, Höhen (linear)
        float kickFlux = 0, snareFlux = 0, hatFlux = 0;
        float tonality = 0;                 // 1 - spektrale Flachheit in den Mitten
        float width = 0;                    // Side/Mid-Verhältnis
        std::array<float, 12> chroma{};
    };

    // Merkmale eines Takts
    struct Bar
    {
        double powerDb = -100.0;
        std::array<double, 6> bandDb{};
        double kick = 0, snare = 0, hats = 0, tonality = 0, width = 0;
        double backbeat = 0;  // Snare-Energie auf Zählzeit 2+4 im Verhältnis zum Rest
        double movement = 0;  // Harmonische Bewegung innerhalb des Takts (Melodie statt Fläche)
        std::array<double, 12> chroma{};
    };

    constexpr double bandEdges[7] = { 30.0, 90.0, 250.0, 800.0, 3000.0, 6000.0, 16000.0 };

    double toDb(double p) { return p > 1e-12 ? 10.0 * std::log10(p) : -120.0; }

    double percentile(std::vector<double> v, double q)
    {
        if (v.empty()) return 0.0;
        std::sort(v.begin(), v.end());
        return v[(size_t)std::round(q * (v.size() - 1))];
    }

    juce::String barRange(int start, int end) // 0-basiert, end exklusiv -> "T1-8"
    {
        return "T" + juce::String(start + 1) + (end - start > 1 ? "-" + juce::String(end) : juce::String());
    }

    // Aktivität glätten (Mehrheitsfilter) und zu kurze Bereiche verwerfen
    std::vector<std::pair<int, int>> toRegions(std::vector<bool> active, int minLength)
    {
        const int n = (int)active.size();
        std::vector<bool> smooth(active);
        for (int i = 1; i + 1 < n; ++i)
            smooth[(size_t)i] = ((int)active[(size_t)i - 1] + (int)active[(size_t)i] + (int)active[(size_t)i + 1]) >= 2;

        std::vector<std::pair<int, int>> regions;
        int start = -1;
        for (int i = 0; i <= n; ++i)
        {
            const bool on = i < n && smooth[(size_t)i];
            if (on && start < 0) start = i;
            if (!on && start >= 0)
            {
                if (i - start >= minLength) regions.push_back({ start, i });
                start = -1;
            }
        }
        return regions;
    }
}

bool GhostArrangement::build(const juce::File& file, double bpm, GhostArrangement& out, juce::String& error,
                             const std::function<bool(double)>& progress)
{
    // --- Tempo bei Bedarf vorab schätzen (erste Hälfte des Fortschritts)
    double progressOffset = 0.0, progressScale = 1.0;
    if (bpm <= 0.0)
    {
        AnalysisMetrics metrics;
        if (!TrackAnalyser::analyseFile(file, metrics, error, [&](double p) { return progress ? progress(p * 0.5) : true; }))
            return false;
        bpm = metrics.tempoBpm;
        progressOffset = 0.5;
        progressScale = 0.5;
    }
    if (bpm <= 0.0)
    {
        error = tr("ghost_im_referenztrack_wurde_kein", "Im Referenztrack wurde kein klares Tempo erkannt – ein Ghost-Arrangement braucht ein Taktraster.");
        return false;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= fftSize)
    {
        error = tr("ghost_die_datei_konnte_nicht", "Die Datei konnte nicht gelesen werden (Format nicht unterstützt?).");
        return false;
    }

    const double fs = reader->sampleRate;
    const juce::int64 total = reader->lengthInSamples;

    // --- Frame-Merkmale berechnen
    juce::dsp::FFT fft(fftOrder);
    std::vector<float> window(fftSize), fftData(fftSize * 2), prevLog(fftSize / 2 + 1, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t)i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / (fftSize - 1));

    std::vector<int> binBand(fftSize / 2 + 1, -1), binPc(fftSize / 2 + 1, -1);
    for (int k = 1; k <= fftSize / 2; ++k)
    {
        const double f = k * fs / fftSize;
        for (int b = 0; b < 6; ++b)
            if (f >= bandEdges[b] && f < bandEdges[b + 1]) binBand[(size_t)k] = b;
        if (f >= 100.0 && f <= 5000.0)
        {
            const int midi = (int)std::lround(69.0 + 12.0 * std::log2(f / 440.0));
            binPc[(size_t)k] = ((midi % 12) + 12) % 12;
        }
    }
    auto binOf = [&](double hz) { return juce::jlimit(1, fftSize / 2, (int)(hz * fftSize / fs)); };
    const int kickLo = binOf(30), kickHi = binOf(120), snareLo = binOf(1500), snareHi = binOf(4000);
    const int hatLo = binOf(7000), hatHi = binOf(16000), tonalLo = binOf(250), tonalHi = binOf(3000);

    std::vector<Frame> frames;
    frames.reserve((size_t)(total / hopSize) + 1);

    juce::AudioBuffer<float> buffer(2, fftSize);
    std::vector<float> side(fftSize);

    for (juce::int64 pos = 0; pos + fftSize <= total; pos += hopSize)
    {
        reader->read(&buffer, 0, fftSize, pos, true, true);
        const float* L = buffer.getReadPointer(0);
        const float* R = buffer.getReadPointer(1);

        Frame fr;
        double midSum = 0.0, sideSum = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
            midSum += (double)m * m;
            sideSum += (double)s * s;
            fftData[(size_t)i] = m * window[(size_t)i];
        }
        std::fill(fftData.begin() + fftSize, fftData.end(), 0.0f);
        fr.power = (float)(midSum / fftSize);
        fr.width = midSum > 1e-12 ? (float)(sideSum / midSum) : 0.0f;

        fft.performFrequencyOnlyForwardTransform(fftData.data());

        double logSum = 0.0, linSum = 0.0;
        int tonalCount = 0;
        for (int k = 1; k <= fftSize / 2; ++k)
        {
            const float mag = fftData[(size_t)k];
            const float pw = mag * mag;
            if (binBand[(size_t)k] >= 0) fr.band[(size_t)binBand[(size_t)k]] += pw;
            if (binPc[(size_t)k] >= 0) fr.chroma[(size_t)binPc[(size_t)k]] += mag;

            const float lg = std::log1p(100.0f * mag);
            const float diff = juce::jmax(0.0f, lg - prevLog[(size_t)k]);
            prevLog[(size_t)k] = lg;
            if (k >= kickLo && k <= kickHi) fr.kickFlux += diff;
            if (k >= snareLo && k <= snareHi) fr.snareFlux += diff;
            if (k >= hatLo && k <= hatHi) fr.hatFlux += diff;

            if (k >= tonalLo && k <= tonalHi)
            {
                logSum += std::log(pw + 1e-12);
                linSum += pw;
                ++tonalCount;
            }
        }
        if (tonalCount > 0 && linSum > 1e-10)
            fr.tonality = (float)(1.0 - std::exp(logSum / tonalCount) / (linSum / tonalCount));

        frames.push_back(fr);

        if (progress && (frames.size() % 64) == 0
            && !progress(progressOffset + progressScale * (double)pos / (double)total))
        {
            error = "Abgebrochen.";
            return false;
        }
    }

    if (frames.size() < 32)
    {
        error = tr("ghost_der_track_ist_zu", "Der Track ist zu kurz für ein Arrangement.");
        return false;
    }

    const double frameSeconds = hopSize / fs;

    // --- Taktraster: Takt 1 beginnt beim ersten deutlichen Kick (sonst beim ersten hörbaren Signal)
    std::vector<double> kickValues;
    for (const auto& f : frames) kickValues.push_back(f.kickFlux);
    const double kickRef = percentile(kickValues, 0.95);

    size_t originFrame = 0;
    bool foundKick = false;
    for (size_t i = 0; i < frames.size(); ++i)
        if (toDb(frames[i].power) > -40.0 && frames[i].kickFlux > 0.4 * kickRef) { originFrame = i; foundKick = true; break; }
    if (!foundKick)
        for (size_t i = 0; i < frames.size(); ++i)
            if (toDb(frames[i].power) > -50.0) { originFrame = i; break; }

    // Zeitpunkte beziehen sich auf die Fenstermitte eines Frames
    const double centre = 0.5 * fftSize / fs;
    auto frameTime = [&](size_t i) { return (double)i * frameSeconds + centre; };
    auto frameAt = [&](double t) { return (size_t)juce::jmax(0.0, (t - centre) / frameSeconds); };

    // Geschätztes Tempo verfeinern: Schon 0,5 BPM Abweichung verschieben das Raster über einen
    // ganzen Song um einen Schlag. Gewählt wird das Tempo, bei dem die Kicks am besten auf den Vierteln liegen.
    if (foundKick)
    {
        auto gridScore = [&](double candidate) {
            const double beat = 60.0 / candidate;
            double score = 0;
            for (double t = frameTime(originFrame); t < frameTime(frames.size() - 1); t += beat)
            {
                const size_t i = frameAt(t);
                double peak = 0;
                for (size_t k = i > 0 ? i - 1 : 0; k <= i + 1 && k < frames.size(); ++k) peak = juce::jmax(peak, (double)frames[k].kickFlux);
                score += peak;
            }
            return score * beat; // auf gleiche Songlänge normieren
        };

        double best = bpm, bestScore = -1.0;
        for (double c = bpm - 1.0; c <= bpm + 1.0; c += 0.05)
        {
            const double sc = gridScore(c);
            if (sc > bestScore) { bestScore = sc; best = c; }
        }
        bpm = std::abs(best - std::round(best)) < 0.15 ? std::round(best) : std::round(best * 100.0) / 100.0;
    }

    // Kick erst später? Dann Takt 1 um ganze Takte zurück an den Signalanfang legen
    const double barSeconds = 4.0 * 60.0 / bpm;
    double origin = frameTime(originFrame);
    for (size_t i = 0; i < frames.size(); ++i)
    {
        if (toDb(frames[i].power) > -50.0)
        {
            const double firstSignal = frameTime(i);
            while (origin - barSeconds >= firstSignal - 0.25 * barSeconds) origin -= barSeconds;
            break;
        }
    }

    // Stille am Ende zählt nicht mit
    double duration = frameTime(frames.size() - 1);
    for (size_t i = frames.size(); i-- > 0;)
        if (toDb(frames[i].power) > -50.0) { duration = frameTime(i); break; }
    const int numBars = (int)std::floor((duration - origin) / barSeconds + 0.5);
    if (numBars < 4)
    {
        error = tr("ghost_der_track_ist_zu", "Der Track ist zu kurz für ein Arrangement.");
        return false;
    }

    // --- Takt-Merkmale
    std::vector<Bar> bars((size_t)numBars);
    for (int b = 0; b < numBars; ++b)
    {
        const size_t from = frameAt(origin + b * barSeconds);
        const size_t to = juce::jmin(frames.size(), frameAt(origin + (b + 1) * barSeconds));
        if (to <= from) continue;

        double power = 0;
        std::array<double, 6> band{};
        auto& bar = bars[(size_t)b];
        for (size_t i = from; i < to; ++i)
        {
            const auto& f = frames[i];
            power += f.power;
            for (int k = 0; k < 6; ++k) band[(size_t)k] += f.band[(size_t)k];
            bar.kick += f.kickFlux;
            bar.snare += f.snareFlux;
            bar.hats += f.hatFlux;
            bar.tonality += f.tonality;
            bar.width += f.width;
            for (int k = 0; k < 12; ++k) bar.chroma[(size_t)k] += f.chroma[(size_t)k];
        }

        const double n = (double)(to - from);
        bar.powerDb = toDb(power / n);
        for (int k = 0; k < 6; ++k) bar.bandDb[(size_t)k] = toDb(band[(size_t)k] / n);
        bar.kick /= n; bar.snare /= n; bar.hats /= n; bar.tonality /= n; bar.width /= n;

        // Backbeat: Snare-Impulse auf 2 und 4 gegenüber 1 und 3
        const double beatSeconds = barSeconds / 4.0;
        auto fluxAround = [&](double t) {
            double sum = 0;
            for (size_t i = frameAt(t - 0.05); i <= frameAt(t + 0.05) && i < frames.size(); ++i) sum = juce::jmax(sum, (double)frames[i].snareFlux);
            return sum;
        };
        const double barStart = origin + b * barSeconds;
        const double back = 0.5 * (fluxAround(barStart + beatSeconds) + fluxAround(barStart + 3 * beatSeconds));
        // Vergleich nur mit 1 und 3: Offbeat-Hats würden das Ergebnis sonst verwässern
        const double rest = 0.5 * (fluxAround(barStart) + fluxAround(barStart + 2 * beatSeconds));
        bar.backbeat = back / (rest + 1e-6);

        // Bewegung: mittlere Chroma-Änderung zwischen Frames im Abstand von ca. 1/8 Note
        const size_t step = juce::jmax((size_t)1, (size_t)(beatSeconds / 2.0 / frameSeconds));
        int moves = 0;
        for (size_t i = from; i + step < to; i += step)
        {
            const auto& c1 = frames[i].chroma; const auto& c2 = frames[i + step].chroma;
            double dot = 0, n1 = 0, n2 = 0;
            for (int k = 0; k < 12; ++k) { dot += c1[(size_t)k] * c2[(size_t)k]; n1 += c1[(size_t)k] * c1[(size_t)k]; n2 += c2[(size_t)k] * c2[(size_t)k]; }
            if (n1 > 1e-9 && n2 > 1e-9) { bar.movement += 1.0 - dot / std::sqrt(n1 * n2); ++moves; }
        }
        if (moves > 0) bar.movement /= moves;

        double norm = 0;
        for (auto c : bar.chroma) norm += c * c;
        norm = std::sqrt(norm);
        if (norm > 0) for (auto& c : bar.chroma) c /= norm;
    }

    // --- Elemente: Aktivität je Takt relativ zum lautesten Teil des Songs
    auto column = [&](auto getter) { std::vector<double> v; for (const auto& b : bars) v.push_back(getter(b)); return v; };
    const double kickP = percentile(column([](const Bar& b) { return b.kick; }), 0.9);
    const double snareP = percentile(column([](const Bar& b) { return b.snare; }), 0.9);
    const double hatsP = percentile(column([](const Bar& b) { return b.hats; }), 0.9);
    const double tonalP = percentile(column([](const Bar& b) { return b.tonality; }), 0.9);
    std::array<double, 6> bandP{};
    for (int k = 0; k < 6; ++k) bandP[(size_t)k] = percentile(column([k](const Bar& b) { return b.bandDb[(size_t)k]; }), 0.9);
    const double movementP = percentile(column([](const Bar& b) { return b.movement; }), 0.9);

    struct Rule { const char* key; const char* name; std::function<bool(const Bar&)> active; };
    const std::vector<Rule> rules = {
        { "ghost_el_kick", "Kick", [&](const Bar& b) { return b.kick > 0.4 * kickP && b.bandDb[0] > bandP[0] - 12.0; } },
        { "ghost_el_bass", "Bass", [&](const Bar& b) { return b.bandDb[1] > bandP[1] - 9.0 && b.bandDb[0] > bandP[0] - 15.0; } },
        { "ghost_el_snare", "Snare / Clap", [&](const Bar& b) { return b.snare > 0.35 * snareP && b.backbeat > 1.5 && b.bandDb[3] > bandP[3] - 12.0; } },
        { "ghost_el_hats", "Hi-Hats", [&](const Bar& b) { return b.hats > 0.4 * hatsP && b.bandDb[5] > bandP[5] - 14.0; } },
        { "ghost_el_pads", "Pads / Flächen", [&](const Bar& b) { return b.tonality > 0.4 * tonalP && b.bandDb[2] > bandP[2] - 8.0; } },
        { "ghost_el_lead", "Lead / Vocals", [&](const Bar& b) { return b.movement > 0.12 && b.movement > 0.6 * movementP
                                                      && b.tonality > 0.5 * tonalP && b.bandDb[3] > bandP[3] - 9.0; } }
    };

    GhostArrangement result;
    result.bpm = bpm;
    result.totalBars = numBars;
    result.gridOffsetSeconds = origin;

    for (const auto& rule : rules)
    {
        std::vector<bool> active;
        for (const auto& b : bars) active.push_back(b.powerDb > -60.0 && rule.active(b));
        Element e;
        e.name = tr(rule.key, rule.name);
        e.regions = toRegions(active, 2);
        if (!e.regions.empty()) result.elements.push_back(e);
    }

    // --- Abschnitte: Neuheit an 4-Takt-Grenzen (Klangfarbe, Harmonie, Elemente)
    auto features = [&](int from, int to) {
        std::vector<double> v(6 + 12 + rules.size(), 0.0);
        const int n = juce::jmax(1, to - from);
        for (int b = from; b < to; ++b)
        {
            const auto& bar = bars[(size_t)b];
            for (int k = 0; k < 6; ++k) v[(size_t)k] += (bar.bandDb[(size_t)k] - bandP[(size_t)k]) / 6.0;
            for (int k = 0; k < 12; ++k) v[(size_t)(6 + k)] += bar.chroma[(size_t)k] * 1.5;
            for (size_t r = 0; r < rules.size(); ++r) v[18 + r] += rules[r].active(bar) ? 1.0 : 0.0;
        }
        for (auto& x : v) x /= n;
        return v;
    };

    std::vector<std::pair<int, double>> novelty; // {Takt, Wert}
    for (int b = 4; b + 4 <= numBars; b += 4)
    {
        const auto before = features(b - 4, b), after = features(b, b + 4);
        double d = 0;
        for (size_t k = 0; k < before.size(); ++k) d += (before[k] - after[k]) * (before[k] - after[k]);
        novelty.push_back({ b, std::sqrt(d) });
    }

    std::vector<int> boundaries;
    if (!novelty.empty())
    {
        double mean = 0, sq = 0;
        for (auto& [b, v] : novelty) { mean += v; sq += v * v; }
        mean /= novelty.size();
        const double sd = std::sqrt(juce::jmax(0.0, sq / novelty.size() - mean * mean));

        // Stärkste Wechsel zuerst, mindestens 8 Takte Abstand
        auto sorted = novelty;
        std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
        for (auto& [b, v] : sorted)
        {
            if (v < mean + 0.4 * sd) break;
            bool farEnough = true;
            for (int x : boundaries) if (std::abs(x - b) < 8) farEnough = false;
            if (farEnough) boundaries.push_back(b);
        }
        std::sort(boundaries.begin(), boundaries.end());
    }

    std::vector<int> starts{ 0 };
    starts.insert(starts.end(), boundaries.begin(), boundaries.end());

    struct Info { int start, end; double loud; bool kick; juce::String label; double rise = 0; };
    std::vector<Info> infos;
    for (size_t i = 0; i < starts.size(); ++i)
    {
        Info info{ starts[i], i + 1 < starts.size() ? starts[i + 1] : numBars, 0.0, false, {} };
        double p = 0; int kicks = 0;
        for (int b = info.start; b < info.end; ++b)
        {
            p += std::pow(10.0, bars[(size_t)b].powerDb / 10.0);
            if (rules[0].active(bars[(size_t)b])) ++kicks;
        }
        info.loud = toDb(p / juce::jmax(1, info.end - info.start));
        info.kick = kicks * 2 >= (info.end - info.start);

        // Anstieg: zweite Hälfte gegenüber erster Hälfte (dB)
        const int mid = (info.start + info.end) / 2;
        double first = 0, second = 0;
        for (int b = info.start; b < info.end; ++b)
            (b < mid ? first : second) += std::pow(10.0, bars[(size_t)b].powerDb / 10.0);
        info.rise = toDb(second / juce::jmax(1, info.end - mid)) - toDb(first / juce::jmax(1, mid - info.start));
        infos.push_back(info);
    }

    std::vector<double> louds;
    for (auto& s : infos) louds.push_back(s.loud);
    const double maxLoud = *std::max_element(louds.begin(), louds.end());
    const double median = percentile(louds, 0.5);

    for (size_t i = 0; i < infos.size(); ++i)
    {
        auto& s = infos[i];
        if (i == 0 && (s.loud < median - 1.5 || !s.kick) && infos.size() > 1) s.label = "Intro";
        else if (i == infos.size() - 1 && (s.loud < median - 1.5 || !s.kick) && infos.size() > 2) s.label = "Outro";
        else if (s.loud >= maxLoud - 1.5) s.label = s.kick ? "Drop" : "Refrain";
        else if (s.loud < median - 3.0 || !s.kick) s.label = "Break";
        else s.label = "Strophe";
    }
    // Teil direkt vor einem Drop/Refrain, der lauter wird oder kurz ist = Build-up
    for (size_t i = 0; i + 1 < infos.size(); ++i)
        if ((infos[i].label == "Strophe" || infos[i].label == "Break")
            && (infos[i + 1].label == "Drop" || infos[i + 1].label == "Refrain")
            && infos[i].end - infos[i].start <= 8 && infos[i].rise >= 2.0)
            infos[i].label = "Build-up";

    std::map<juce::String, int> counts, seen;
    for (auto& s : infos) ++counts[s.label];
    for (auto& s : infos)
    {
        // Interne Bezeichnung -> angezeigter (übersetzbarer) Name
        static const std::map<juce::String, std::pair<const char*, const char*>> sectionKeys = {
            { "Intro", { "ghost_sec_intro", "Intro" } }, { "Outro", { "ghost_sec_outro", "Outro" } },
            { "Drop", { "ghost_sec_drop", "Drop" } }, { "Refrain", { "ghost_sec_chorus", "Refrain" } },
            { "Break", { "ghost_sec_break", "Break" } }, { "Strophe", { "ghost_sec_verse", "Strophe" } },
            { "Build-up", { "ghost_sec_buildup", "Build-up" } } };
        const auto found = sectionKeys.find(s.label);
        juce::String name = found != sectionKeys.end() ? tr(found->second.first, found->second.second) : s.label;
        if (counts[s.label] > 1) name << " " << ++seen[s.label];
        result.sections.push_back({ name, s.start, s.end });
    }

    // FX/Übergänge: jeweils der Takt vor einem Abschnittswechsel (Riser, Crash, Fill)
    Element fx;
    fx.name = tr("ghost_fx_uebergaenge", "FX / Übergänge");
    for (int b : boundaries) fx.regions.push_back({ b - 1, b });
    if (!fx.regions.empty()) result.elements.push_back(fx);

    out = result;
    if (progress) progress(1.0);
    return true;
}

bool GhostArrangement::writeMidi(const juce::File& target, bool placeholders) const
{
    constexpr int ppq = 960;
    const int barTicks = 4 * ppq;
    const double endTick = (double)totalBars * barTicks;

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(ppq);

    // Spur 1: Tempo, Taktart und Abschnitts-Marker
    juce::MidiMessageSequence arrangement;
    auto trackName = juce::MidiMessage::textMetaEvent(3, tr("ghost_marker_track", "Arrangement (Referenz)"));
    trackName.setTimeStamp(0);
    arrangement.addEvent(trackName);
    auto tempo = juce::MidiMessage::tempoMetaEvent((int)std::round(60000000.0 / bpm));
    tempo.setTimeStamp(0);
    arrangement.addEvent(tempo);
    auto timeSig = juce::MidiMessage::timeSignatureMetaEvent(4, 4);
    timeSig.setTimeStamp(0);
    arrangement.addEvent(timeSig);
    for (const auto& s : sections)
    {
        auto marker = juce::MidiMessage::textMetaEvent(6, s.name);
        marker.setTimeStamp((double)s.startBar * barTicks);
        arrangement.addEvent(marker);
    }
    auto end0 = juce::MidiMessage::endOfTrack();
    end0.setTimeStamp(endTick);
    arrangement.addEvent(end0);
    midi.addTrack(arrangement);

    // Je Element eine benannte Spur; Platzhalter = stumme Note (C-2, Velocity 1) über die aktiven Takte
    int channel = 1;
    for (const auto& e : elements)
    {
        juce::MidiMessageSequence seq;
        auto name = juce::MidiMessage::textMetaEvent(3, e.name);
        name.setTimeStamp(0);
        seq.addEvent(name);

        if (placeholders)
        {
            for (const auto& [start, end] : e.regions)
            {
                auto on = juce::MidiMessage::noteOn(channel, 0, (juce::uint8)1);
                on.setTimeStamp((double)start * barTicks);
                auto off = juce::MidiMessage::noteOff(channel, 0);
                off.setTimeStamp((double)end * barTicks - 1);
                seq.addEvent(on);
                seq.addEvent(off);
            }
            seq.updateMatchedPairs();
        }

        auto endTrack = juce::MidiMessage::endOfTrack();
        endTrack.setTimeStamp(endTick);
        seq.addEvent(endTrack);
        midi.addTrack(seq);
        channel = channel % 16 + 1;
    }

    target.getParentDirectory().createDirectory();
    target.deleteFile();
    juce::FileOutputStream stream(target);
    return stream.openedOk() && midi.writeTo(stream, 1);
}

juce::String GhostArrangement::describe() const
{
    const int offsetMs = (int)std::round(gridOffsetSeconds * 1000.0);
    juce::String text;
    text << juce::String(bpm, 1) << " BPM  ·  " << totalBars << tr("ghost_summary_bars", " Takte  ·  ")
         << tr("ghost_takt_1", "Takt 1 = ") << offsetMs / 60000 << ":"
         << juce::String((offsetMs / 1000) % 60).paddedLeft('0', 2) << "." << juce::String(offsetMs % 1000).paddedLeft('0', 3)
         << tr("ghost_im_referenztrack_abschnitte", " im Referenztrack\n\nAbschnitte:\n");

    for (const auto& s : sections)
        text << "   " << barRange(s.startBar, s.endBar).paddedRight(' ', 10) << s.name << "\n";

    text << tr("ghost_summary_tracks", "\nSpuren:\n");
    for (const auto& e : elements)
    {
        juce::StringArray ranges;
        for (const auto& [a, b] : e.regions) ranges.add(barRange(a, b));
        juce::String list = ranges.joinIntoString(", ");
        if (list.length() > 70) list = list.substring(0, 67) + juce::String::fromUTF8(" …");
        text << "   " << e.name << ": " << list << "\n";
    }
    return text.trimEnd();
}
