#pragma execution_character_set("utf-8")

#include "TrackAnalyser.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include "SettingsManager.h"

namespace
{
    double powerToLufs(double meanPower)
    {
        return meanPower > 0.0 ? -0.691 + 10.0 * std::log10(meanPower) : -100.0;
    }

    constexpr double bandEdges[AnalysisMetrics::numBands + 1] = { 20.0, 60.0, 250.0, 500.0, 2000.0, 6000.0, 20000.0 };
    constexpr float silenceThreshold = 0.001f; // -60 dBFS
}

// ==================== AnalysisMetrics (Speicherung als JSON) ====================

juce::var AnalysisMetrics::toVar() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("duration", durationSeconds);
    obj->setProperty("lufs", integratedLufs);
    obj->setProperty("lra", loudnessRange);
    obj->setProperty("maxShortTerm", maxShortTermLufs);
    obj->setProperty("peak", samplePeakDb);
    obj->setProperty("clipped", clippedSamples);
    obj->setProperty("correlation", correlation);
    obj->setProperty("lowCorrelation", lowCorrelation);
    obj->setProperty("sideToMid", sideToMidDb);

    juce::Array<juce::var> bands;
    for (auto b : bandDb) bands.add(b);
    obj->setProperty("bands", bands);

    obj->setProperty("tempo", tempoBpm);
    obj->setProperty("tempoFromHost", tempoFromHost);
    obj->setProperty("key", detectedKey);
    obj->setProperty("keyConfidence", keyConfidence);
    obj->setProperty("rhythm", rhythmStrength);
    obj->setProperty("chords", chordProgression);
    return juce::var(obj);
}

AnalysisMetrics AnalysisMetrics::fromVar(const juce::var& v)
{
    AnalysisMetrics m;
    m.durationSeconds = v.getProperty("duration", 0.0);
    m.integratedLufs = v.getProperty("lufs", -70.0);
    m.loudnessRange = v.getProperty("lra", 0.0);
    m.maxShortTermLufs = v.getProperty("maxShortTerm", -70.0);
    m.samplePeakDb = v.getProperty("peak", -100.0);
    m.clippedSamples = v.getProperty("clipped", 0);
    m.correlation = v.getProperty("correlation", 1.0);
    m.lowCorrelation = v.getProperty("lowCorrelation", 1.0);
    m.sideToMidDb = v.getProperty("sideToMid", -100.0);

    if (auto* bands = v.getProperty("bands", {}).getArray())
        for (int i = 0; i < numBands && i < bands->size(); ++i)
            m.bandDb[(size_t)i] = (*bands)[i];

    m.tempoBpm = v.getProperty("tempo", 0.0);
    m.tempoFromHost = v.getProperty("tempoFromHost", false);
    m.detectedKey = v.getProperty("key", "").toString();
    m.keyConfidence = v.getProperty("keyConfidence", 0.0);
    m.rhythmStrength = v.getProperty("rhythm", 0.0);
    m.chordProgression = v.getProperty("chords", "").toString();
    return m;
}

// ==================== TrackAnalyser ====================

TrackAnalyser::TrackAnalyser() : juce::Thread("DAW Notes Analyse")
{
    specRing.assign(specSize, 0.0f);
    specFrame.assign(specSize * 2, 0.0f);
    specWindow.resize(specSize);
    for (int i = 0; i < specSize; ++i)
        specWindow[(size_t)i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / (specSize - 1));

    onsetFrame.assign(onsetSize * 2, 0.0f);
    onsetWindow.resize(onsetSize);
    for (int i = 0; i < onsetSize; ++i)
        onsetWindow[(size_t)i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / (onsetSize - 1));
}

TrackAnalyser::~TrackAnalyser()
{
    capturing = false;
    stopThread(3000);
}

void TrackAnalyser::prepare(double newSampleRate)
{
    if (newSampleRate > 0.0)
        sampleRate = newSampleRate;
}

void TrackAnalyser::setHostTransport(bool isPlaying, double bpm)
{
    hostPlaying.store(isPlaying, std::memory_order_relaxed);
    hostBpm.store(bpm, std::memory_order_relaxed);
}

void TrackAnalyser::pushAudio(const juce::AudioBuffer<float>& buffer)
{
    if (!capturing.load(std::memory_order_relaxed) || buffer.getNumChannels() == 0)
        return;

    const int numSamples = buffer.getNumSamples();
    const float* left = buffer.getReadPointer(0);
    const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : left; // Mono wie Dual-Mono behandeln

    // Bei voller FIFO werden überzählige Samples verworfen (der Audio-Thread wartet nie)
    const auto scope = fifo.write(numSamples);

    if (scope.blockSize1 > 0)
    {
        fifoBuffer.copyFrom(0, scope.startIndex1, left, scope.blockSize1);
        fifoBuffer.copyFrom(1, scope.startIndex1, right, scope.blockSize1);
    }
    if (scope.blockSize2 > 0)
    {
        fifoBuffer.copyFrom(0, scope.startIndex2, left + scope.blockSize1, scope.blockSize2);
        fifoBuffer.copyFrom(1, scope.startIndex2, right + scope.blockSize1, scope.blockSize2);
    }
}

void TrackAnalyser::startAnalysis(int64_t songId)
{
    if (isThreadRunning())
        return;

    capturing = false;

    // Alte Reste in der FIFO verwerfen
    while (fifo.getNumReady() > 0)
        fifo.read(fifo.getNumReady());

    fs = sampleRate.load();
    resetAccumulators();

    analysedSongId = songId;
    finishRequested = false;
    elapsedSeconds = 0.0;
    currentLevelDb = -100.0f;
    momentaryLufs = -70.0f;
    state = State::WaitingForSignal;
    capturing = true;

    startThread(juce::Thread::Priority::low);
}

void TrackAnalyser::finishAnalysis()
{
    finishRequested = true;
}

void TrackAnalyser::cancelAnalysis()
{
    capturing = false;
    stopThread(3000);
    state = State::Idle;
}

void TrackAnalyser::resetAccumulators()
{
    setupFilters();

    hostWasPlaying = false;
    hostStoppedSince = -1.0;
    lastActivityMs = juce::Time::getMillisecondCounter();
    hostBpmSeen = 0.0;

    samplesListened = 0;
    trailingSilence = 0;

    blockSum = 0.0;
    blockSamples = 0;
    blockLength = juce::jmax(1, (int)std::round(fs * 0.1));
    blockPowers.clear();
    blockPowers.reserve(6000);
    meterSum = 0.0;
    meterSamples = 0;

    peak = 0.0;
    clipCount = 0;
    sumLL = sumRR = sumLR = lowLL = lowRR = lowLR = sumMid = sumSide = 0.0;

    std::fill(specRing.begin(), specRing.end(), 0.0f);
    specWritePos = 0;
    specHopCounter = 0;
    powerSum.assign(specSize / 2 + 1, 0.0);
    chroma.fill(0.0);
    chromaFrames.clear();
    chromaFrames.reserve(8000);

    binPitchClass.assign(specSize / 2 + 1, -1);
    for (int k = 1; k <= specSize / 2; ++k)
    {
        const double freq = k * fs / specSize;
        if (freq >= 100.0 && freq <= 5000.0)
        {
            const int midi = (int)std::lround(69.0 + 12.0 * std::log2(freq / 440.0));
            binPitchClass[(size_t)k] = ((midi % 12) + 12) % 12;
        }
    }

    prevOnsetMag.assign(onsetSize / 2 + 1, 0.0f);
    onsetEnvelope.clear();
    onsetEnvelope.reserve(60000);
    onsetHopCounter = 0;

    result = AnalysisMetrics();
}

void TrackAnalyser::setupFilters()
{
    // K-Gewichtung nach ITU-R BS.1770 für beliebige Abtastraten (Koeffizienten wie libebur128)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(juce::MathConstants<double>::pi * f0 / fs);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        for (auto& f : kShelf)
        {
            f.b0 = (Vh + Vb * K / Q + K * K) / a0;
            f.b1 = 2.0 * (K * K - Vh) / a0;
            f.b2 = (Vh - Vb * K / Q + K * K) / a0;
            f.a1 = 2.0 * (K * K - 1.0) / a0;
            f.a2 = (1.0 - K / Q + K * K) / a0;
            f.reset();
        }
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(juce::MathConstants<double>::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;

        for (auto& f : kHighPass)
        {
            f.b0 = 1.0; f.b1 = -2.0; f.b2 = 1.0;
            f.a1 = 2.0 * (K * K - 1.0) / a0;
            f.a2 = (1.0 - K / Q + K * K) / a0;
            f.reset();
        }
    }
    {
        // Tiefpass 120 Hz für die Mono-Kompatibilität im Bassbereich
        const double w0 = juce::MathConstants<double>::twoPi * 120.0 / fs;
        const double alpha = std::sin(w0) / (2.0 * 0.7071);
        const double c = std::cos(w0);
        const double a0 = 1.0 + alpha;

        for (auto& f : lowPass)
        {
            f.b0 = (1.0 - c) / 2.0 / a0;
            f.b1 = (1.0 - c) / a0;
            f.b2 = f.b0;
            f.a1 = -2.0 * c / a0;
            f.a2 = (1.0 - alpha) / a0;
            f.reset();
        }
    }
}

void TrackAnalyser::run()
{
    std::vector<float> left(4096), right(4096);

    auto drainFifo = [&] {
        int ready;
        while ((ready = fifo.getNumReady()) > 0)
        {
            const int n = juce::jmin(ready, (int)left.size());
            {
                const auto scope = fifo.read(n);
                if (scope.blockSize1 > 0)
                {
                    std::copy_n(fifoBuffer.getReadPointer(0, scope.startIndex1), scope.blockSize1, left.data());
                    std::copy_n(fifoBuffer.getReadPointer(1, scope.startIndex1), scope.blockSize1, right.data());
                }
                if (scope.blockSize2 > 0)
                {
                    std::copy_n(fifoBuffer.getReadPointer(0, scope.startIndex2), scope.blockSize2, left.data() + scope.blockSize1);
                    std::copy_n(fifoBuffer.getReadPointer(1, scope.startIndex2), scope.blockSize2, right.data() + scope.blockSize1);
                }
            }
            processChunk(left.data(), right.data(), n);
            lastActivityMs = juce::Time::getMillisecondCounter();
        }
    };

    while (!threadShouldExit())
    {
        drainFifo();

        if (finishRequested.load() || shouldAutoFinish())
        {
            capturing = false;
            wait(30); // ggf. noch laufenden processBlock-Aufruf abschließen lassen
            drainFifo();

            if (samplesListened < (juce::int64)fs)
            {
                state = State::Idle; // praktisch nichts gehört
                return;
            }

            state = State::Finishing;
            computeResult();
            state = State::Done;
            return;
        }

        wait(20);
    }
}

bool TrackAnalyser::shouldAutoFinish() const
{
    if (state.load() != State::Listening)
        return false;

    const auto now = juce::Time::getMillisecondCounter();

    // 1. DAW-Transport wurde gestoppt (falls der Host das meldet)
    if (hostWasPlaying && hostStoppedSince >= 0.0 && now - (juce::uint32)hostStoppedSince > 1500)
        return true;

    // 2. Der Host ruft processBlock nicht mehr auf (manche DAWs pausieren Plugins im Stopp)
    if (now - lastActivityMs > 2500)
        return true;

    // 3. Mehrere Sekunden Stille nach dem Song
    return samplesListened > (juce::int64)(fs * 10.0) && trailingSilence > (juce::int64)(fs * 6.0);
}

void TrackAnalyser::processChunk(const float* left, const float* right, int numSamples)
{
    // Transport-Status auswerten
    if (hostPlaying.load(std::memory_order_relaxed))
    {
        if (state.load() == State::Listening)
            hostWasPlaying = true;
        hostStoppedSince = -1.0;

        const double bpm = hostBpm.load(std::memory_order_relaxed);
        if (bpm > 0.0) hostBpmSeen = bpm;
    }
    else if (hostWasPlaying && hostStoppedSince < 0.0)
    {
        hostStoppedSince = (double)juce::Time::getMillisecondCounter();
    }

    double chunkSum = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float L = left[i], R = right[i];
        const float level = juce::jmax(std::abs(L), std::abs(R));

        if (state.load(std::memory_order_relaxed) == State::WaitingForSignal)
        {
            if (level < silenceThreshold) continue;
            state = State::Listening; // Analyse beginnt mit dem ersten hörbaren Sample
        }

        ++samplesListened;
        trailingSilence = level < silenceThreshold ? trailingSilence + 1 : 0;

        peak = juce::jmax(peak, (double)level);
        if (level >= 0.9999f) ++clipCount;

        // Lautheit (K-gewichtet, 100-ms-Blöcke)
        const float kl = kHighPass[0].process(kShelf[0].process(L));
        const float kr = kHighPass[1].process(kShelf[1].process(R));
        blockSum += (double)kl * kl + (double)kr * kr;
        if (++blockSamples >= blockLength)
        {
            blockPowers.push_back((float)(blockSum / blockLength));
            blockSum = 0.0;
            blockSamples = 0;

            const size_t n = blockPowers.size();
            if (n >= 4)
                momentaryLufs = (float)powerToLufs((blockPowers[n - 1] + blockPowers[n - 2] + blockPowers[n - 3] + blockPowers[n - 4]) / 4.0);
        }

        chunkSum += (double)L * L + (double)R * R;

        // Stereo
        sumLL += (double)L * L;
        sumRR += (double)R * R;
        sumLR += (double)L * R;

        const double lowL = lowPass[0].process(L), lowR = lowPass[1].process(R);
        lowLL += lowL * lowL;
        lowRR += lowR * lowR;
        lowLR += lowL * lowR;

        const float mid = 0.5f * (L + R), side = 0.5f * (L - R);
        sumMid += (double)mid * mid;
        sumSide += (double)side * side;

        // Mono-Ringpuffer für FFT-Auswertungen
        specRing[(size_t)specWritePos] = mid;
        specWritePos = (specWritePos + 1) & (specSize - 1);

        if (++onsetHopCounter >= onsetHop)
        {
            onsetHopCounter = 0;

            for (int j = 0; j < onsetSize; ++j)
                onsetFrame[(size_t)j] = specRing[(size_t)((specWritePos - onsetSize + j) & (specSize - 1))] * onsetWindow[(size_t)j];
            std::fill(onsetFrame.begin() + onsetSize, onsetFrame.end(), 0.0f);
            onsetFft.performFrequencyOnlyForwardTransform(onsetFrame.data());

            // Spektraler Fluss (nur Zuwächse) bis ca. 8 kHz
            const int maxBin = juce::jmin(onsetSize / 2, (int)(8000.0 * onsetSize / fs));
            double flux = 0.0;
            for (int k = 1; k <= maxBin; ++k)
            {
                const float mag = std::log1p(100.0f * onsetFrame[(size_t)k]);
                flux += juce::jmax(0.0f, mag - prevOnsetMag[(size_t)k]);
                prevOnsetMag[(size_t)k] = mag;
            }
            onsetEnvelope.push_back((float)flux);
        }

        if (++specHopCounter >= specSize / 2)
        {
            specHopCounter = 0;

            for (int j = 0; j < specSize; ++j)
                specFrame[(size_t)j] = specRing[(size_t)((specWritePos + j) & (specSize - 1))] * specWindow[(size_t)j];
            std::fill(specFrame.begin() + specSize, specFrame.end(), 0.0f);
            specFft.performFrequencyOnlyForwardTransform(specFrame.data());

            std::array<float, 12> frameChroma{};
            for (int k = 1; k <= specSize / 2; ++k)
            {
                const double mag = specFrame[(size_t)k];
                powerSum[(size_t)k] += mag * mag;
                if (const int pc = binPitchClass[(size_t)k]; pc >= 0)
                {
                    chroma[(size_t)pc] += mag;
                    frameChroma[(size_t)pc] += (float)mag;
                }
            }
            chromaFrames.push_back(frameChroma);
        }
    }

    elapsedSeconds = samplesListened / fs;
    if (numSamples > 0)
        currentLevelDb = (float)juce::Decibels::gainToDecibels(std::sqrt(chunkSum / (2.0 * numSamples)), -100.0);
}

void TrackAnalyser::computeResult()
{
    AnalysisMetrics m;
    m.durationSeconds = (double)(samplesListened - trailingSilence) / fs;

    // --- Integrierte Lautheit (400-ms-Blöcke, 75 % Überlappung, Gating nach BS.1770)
    std::vector<double> gated400;
    for (size_t i = 3; i < blockPowers.size(); ++i)
    {
        const double p = (blockPowers[i] + blockPowers[i - 1] + blockPowers[i - 2] + blockPowers[i - 3]) / 4.0;
        if (powerToLufs(p) > -70.0) gated400.push_back(p);
    }

    if (!gated400.empty())
    {
        const double relGate = powerToLufs(std::accumulate(gated400.begin(), gated400.end(), 0.0) / gated400.size()) - 10.0;
        double sum = 0.0;
        int count = 0;
        for (auto p : gated400)
            if (powerToLufs(p) > relGate) { sum += p; ++count; }
        m.integratedLufs = count > 0 ? powerToLufs(sum / count) : -70.0;
    }

    // --- Short-Term (3 s) für Maximalwert und Loudness Range
    std::vector<double> shortTerm;
    double running = 0.0;
    for (size_t i = 0; i < blockPowers.size(); ++i)
    {
        running += blockPowers[i];
        if (i >= 30) running -= blockPowers[i - 30];
        if (i >= 29) shortTerm.push_back(running / 30.0);
    }

    std::vector<double> stAbs;
    for (auto p : shortTerm)
    {
        m.maxShortTermLufs = juce::jmax(m.maxShortTermLufs, powerToLufs(p));
        if (powerToLufs(p) > -70.0) stAbs.push_back(p);
    }

    if (stAbs.size() > 2)
    {
        const double relGate = powerToLufs(std::accumulate(stAbs.begin(), stAbs.end(), 0.0) / stAbs.size()) - 20.0;
        std::vector<double> loud;
        for (auto p : stAbs)
            if (powerToLufs(p) > relGate) loud.push_back(powerToLufs(p));

        if (loud.size() > 2)
        {
            std::sort(loud.begin(), loud.end());
            auto percentile = [&loud](double q) { return loud[(size_t)std::round(q * (loud.size() - 1))]; };
            m.loudnessRange = percentile(0.95) - percentile(0.10);
        }
    }

    // --- Pegel & Stereo
    m.samplePeakDb = juce::Decibels::gainToDecibels(peak, -100.0);
    m.clippedSamples = clipCount;
    m.correlation = (sumLL > 0.0 && sumRR > 0.0) ? sumLR / std::sqrt(sumLL * sumRR) : 1.0;
    m.lowCorrelation = (lowLL > 0.0 && lowRR > 0.0) ? lowLR / std::sqrt(lowLL * lowRR) : 1.0;
    m.sideToMidDb = (sumMid > 0.0 && sumSide > 0.0) ? 10.0 * std::log10(sumSide / sumMid) : -100.0;

    // --- Frequenzbalance
    std::array<double, AnalysisMetrics::numBands> bandPower{};
    for (int k = 1; k <= specSize / 2; ++k)
    {
        const double freq = k * fs / specSize;
        for (int b = 0; b < AnalysisMetrics::numBands; ++b)
            if (freq >= bandEdges[b] && freq < bandEdges[b + 1]) { bandPower[(size_t)b] += powerSum[(size_t)k]; break; }
    }
    const double totalPower = std::accumulate(bandPower.begin(), bandPower.end(), 0.0);
    for (int b = 0; b < AnalysisMetrics::numBands; ++b)
        m.bandDb[(size_t)b] = (totalPower > 0.0 && bandPower[(size_t)b] > 0.0) ? 10.0 * std::log10(bandPower[(size_t)b] / totalPower) : -60.0;

    // --- Tempo aus der Onset-Hüllkurve schätzen (liefert auch die Beat-Ausprägung)
    if (onsetEnvelope.size() > 200)
    {
        const double envRate = fs / onsetHop;
        const int n = (int)onsetEnvelope.size();
        const int smooth = juce::jmax(1, (int)envRate / 2);

        std::vector<double> env((size_t)n);
        double acc = 0.0;
        for (int i = 0; i < n; ++i)
        {
            acc += onsetEnvelope[(size_t)i];
            if (i >= smooth) acc -= onsetEnvelope[(size_t)(i - smooth)];
            env[(size_t)i] = juce::jmax(0.0, (double)onsetEnvelope[(size_t)i] - acc / juce::jmin(i + 1, smooth));
        }

        const int lagMin = (int)(envRate * 60.0 / 200.0), lagMax = (int)(envRate * 60.0 / 60.0);
        std::vector<double> ac((size_t)lagMax + 2, 0.0);
        for (int lag = lagMin; lag <= lagMax + 1 && lag < n; ++lag)
        {
            double s = 0.0;
            for (int i = 0; i + lag < n; ++i) s += env[(size_t)i] * env[(size_t)(i + lag)];
            ac[(size_t)lag] = s / (n - lag);
        }

        int best = -1;
        double bestScore = 0.0;
        for (int lag = lagMin + 1; lag <= lagMax && lag < n; ++lag)
        {
            const double bpm = 60.0 * envRate / lag;
            const double weight = std::exp(-0.5 * std::pow(std::log2(bpm / 120.0), 2.0)); // leichte Bevorzugung um 120 BPM
            if (ac[(size_t)lag] * weight > bestScore) { bestScore = ac[(size_t)lag] * weight; best = lag; }
        }

        // Ohne ausgeprägte Periodizität (z.B. Flächen ohne Beat) lieber kein Tempo angeben
        double acMean = 0.0;
        for (int lag = lagMin; lag <= lagMax && lag < n; ++lag) acMean += ac[(size_t)lag];
        acMean /= juce::jmax(1, juce::jmin(lagMax, n - 1) - lagMin + 1);

        m.rhythmStrength = (best > 0 && acMean > 0.0) ? ac[(size_t)best] / acMean : 0.0;

        if (best > 0 && ac[(size_t)best] > 1.5 * acMean)
        {
            // Parabolische Interpolation für Nachkommastellen
            const double a = ac[(size_t)best - 1], b = ac[(size_t)best], c = ac[(size_t)best + 1];
            const double denom = a - 2.0 * b + c;
            const double offset = std::abs(denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
            m.tempoBpm = 60.0 * envRate / (best + juce::jlimit(-0.5, 0.5, offset));
        }
    }

    // Das Projekt-Tempo der DAW ist exakter als jede Schätzung
    if (hostBpmSeen > 0.0)
    {
        m.tempoBpm = hostBpmSeen;
        m.tempoFromHost = true;
    }

    // --- Tonart (Krumhansl-Schmuckler auf dem Chromagramm)
    {
        static const double majorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double minorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        auto pearson = [](const double* x, const double* y) {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += x[i]; my += y[i]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i) { sxy += (x[i] - mx) * (y[i] - my); sxx += (x[i] - mx) * (x[i] - mx); syy += (y[i] - my) * (y[i] - my); }
            return (sxx > 0 && syy > 0) ? sxy / std::sqrt(sxx * syy) : 0.0;
        };

        double best = -2.0, second = -2.0;
        juce::String bestName;
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            double rotated[12];
            for (int i = 0; i < 12; ++i) rotated[i] = chroma[(size_t)((tonic + i) % 12)];

            for (int minor = 0; minor < 2; ++minor)
            {
                const double r = pearson(rotated, minor ? minorProfile : majorProfile);
                if (r > best)
                {
                    second = best;
                    best = r;
                    bestName = juce::String(names[tonic]) + (minor ? " Minor" : " Major");
                }
                else if (r > second)
                {
                    second = r;
                }
            }
        }

        if (best > 0.0)
        {
            m.detectedKey = bestName;
            m.keyConfidence = best - second;
        }
    }

    m.chordProgression = estimateChords(m.tempoBpm);

    result = m;
}

juce::String TrackAnalyser::estimateChords(double tempoBpm) const
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (chromaFrames.size() < 8) return {};

    // Ein Akkord pro Takt (4/4), ohne Tempo alle 2 Sekunden
    const double hopSeconds = (specSize / 2) / fs;
    const double segmentSeconds = tempoBpm > 0.0 ? 4.0 * 60.0 / tempoBpm : 2.0;
    const int framesPerSegment = juce::jmax(1, (int)std::round(segmentSeconds / hopSeconds));

    std::vector<int> segmentChords; // 0..11 Dur, 12..23 Moll, -1 = kein Akkord
    for (size_t start = 0; start + (size_t)framesPerSegment <= chromaFrames.size(); start += (size_t)framesPerSegment)
    {
        std::array<double, 12> sum{};
        for (size_t f = start; f < start + (size_t)framesPerSegment; ++f)
        {
            double norm = 0.0;
            for (auto v : chromaFrames[f]) norm += (double)v * v;
            if (norm <= 1e-9) continue;
            norm = std::sqrt(norm);
            for (int i = 0; i < 12; ++i) sum[(size_t)i] += chromaFrames[f][(size_t)i] / norm;
        }

        double sumNorm = 0.0;
        for (auto v : sum) sumNorm += v * v;
        if (sumNorm <= 1e-9) { segmentChords.push_back(-1); continue; }
        sumNorm = std::sqrt(sumNorm);

        // Kosinus-Ähnlichkeit mit Dur-/Moll-Dreiklängen (Grundton etwas stärker gewichtet)
        int bestChord = -1;
        double bestScore = 0.0;
        for (int root = 0; root < 12; ++root)
        {
            for (int minor = 0; minor < 2; ++minor)
            {
                const int third = (root + (minor ? 3 : 4)) % 12, fifth = (root + 7) % 12;
                const double score = (1.2 * sum[(size_t)root] + sum[(size_t)third] + sum[(size_t)fifth])
                                   / (sumNorm * std::sqrt(1.2 * 1.2 + 2.0));
                if (score > bestScore) { bestScore = score; bestChord = root + (minor ? 12 : 0); }
            }
        }
        segmentChords.push_back(bestScore > 0.55 ? bestChord : -1);
    }

    auto chordName = [](int c) { return juce::String(names[c % 12]) + (c >= 12 ? "m" : ""); };

    // Nur Akkordwechsel betrachten (gehaltene Akkorde über mehrere Takte zählen einmal)
    std::vector<int> changes;
    for (auto c : segmentChords)
        if (c >= 0 && (changes.empty() || changes.back() != c))
            changes.push_back(c);

    // Häufigste Folge aus vier Akkordwechseln suchen
    std::map<std::array<int, 4>, int> counts;
    std::array<int, 4> bestPattern{ -1, -1, -1, -1 };
    int bestCount = 0;
    for (size_t i = 0; i + 4 <= changes.size(); ++i)
    {
        std::array<int, 4> pattern{ changes[i], changes[i + 1], changes[i + 2], changes[i + 3] };
        const int c = ++counts[pattern];
        if (c > bestCount) { bestCount = c; bestPattern = pattern; }
    }

    juce::StringArray chordNames;
    if (bestCount >= 2)
    {
        for (auto c : bestPattern) chordNames.add(chordName(c));
    }
    else
    {
        // Fallback: die vier häufigsten Akkorde in der Reihenfolge ihres ersten Auftretens
        std::map<int, int> freq;
        for (auto c : segmentChords) if (c >= 0) ++freq[c];
        std::vector<std::pair<int, int>> sorted(freq.begin(), freq.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::vector<int> top;
        for (size_t i = 0; i < sorted.size() && i < 4; ++i) top.push_back(sorted[i].first);
        for (auto c : segmentChords)
            if (std::find(top.begin(), top.end(), c) != top.end() && !chordNames.contains(chordName(c)))
                chordNames.add(chordName(c));
    }

    return chordNames.joinIntoString(" | ");
}

bool TrackAnalyser::analyseFile(const juce::File& file, AnalysisMetrics& out, juce::String& error,
                                const std::function<bool(double)>& progress)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = tr("ta_die_datei_konnte_nicht", "Die Datei konnte nicht gelesen werden (Format nicht unterstützt?).");
        return false;
    }

    TrackAnalyser analyser;
    analyser.fs = reader->sampleRate;
    analyser.resetAccumulators();
    analyser.state = State::WaitingForSignal;

    const int blockSize = 16384;
    juce::AudioBuffer<float> buffer(2, blockSize);
    const juce::int64 total = reader->lengthInSamples;

    for (juce::int64 pos = 0; pos < total; pos += blockSize)
    {
        const int n = (int)juce::jmin((juce::int64)blockSize, total - pos);
        reader->read(&buffer, 0, n, pos, true, true); // Mono wird auf beide Kanäle gelegt
        analyser.processChunk(buffer.getReadPointer(0), buffer.getReadPointer(1), n);

        if (progress && !progress((double)(pos + n) / (double)total))
        {
            error = "Abgebrochen.";
            return false;
        }
    }

    if (analyser.samplesListened < (juce::int64)analyser.fs)
    {
        error = tr("ta_die_datei_enthaelt_fast", "Die Datei enthält (fast) nur Stille.");
        return false;
    }

    analyser.computeResult();
    out = analyser.result;
    return true;
}
