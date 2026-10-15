// Historique des temps d'image pour la présentation "graphique" du panneau.
// Code portable (aucune dépendance Windows), testé dans tests/preview.cpp.
//
// Le temps est découpé en tranches fixes d'un trentième de seconde, alignées sur l'horloge : une
// image appartient toujours à la même tranche, quelle que soit l'heure du relevé. La courbe défile
// donc sans se déformer d'un affichage au suivant.
#pragma once

#include <cmath>
#include <cstdint>

#include "panel.h"

namespace xrpo {

class FrameHistory {
  public:
    static constexpr int kColumns = PanelData::kGraphColumns;
    static constexpr double kSliceSeconds = 1.0 / 30.0;
    // Durée couverte par la courbe (un peu moins de dix secondes).
    static constexpr double kWindowSeconds = kColumns * kSliceSeconds;

    FrameHistory() { reset(); }

    void reset() {
        for (int i = 0; i < kColumns; ++i) m_frame[i] = m_cpu[i] = m_gpu[i] = 0.f;
        m_lastTime = -1.0;
        m_lastGpuTime = -1.0;
        m_lastSlice = 0;
        m_hasSlice = false;
    }

    // À chaque image soumise. `cpuMs` peut valoir NaN.
    void onFrame(double now, float cpuMs) {
        const double previous = m_lastTime;
        m_lastTime = now;
        if (previous < 0.0) return;  // première image : pas encore d'intervalle
        const double dt = now - previous;
        if (!(dt > 0.0) || dt > kPauseSeconds) return;  // pause ou chargement : un trou dans la courbe
        const float frameMs = static_cast<float>(dt * 1000.0);

        // L'image occupe toutes les tranches entre la précédente et elle : une image de 100 ms
        // apparaît comme un palier de trois tranches, pas comme un point isolé.
        const int64_t last = sliceOf(now);
        int64_t first = sliceOf(previous) + 1;
        if (first > last) first = last;
        if (last - first >= kColumns) first = last - kColumns + 1;
        advanceTo(last);
        for (int64_t slice = first; slice <= last; ++slice) {
            const int index = indexOf(slice);
            if (frameMs > m_frame[index]) m_frame[index] = frameMs;
            if (!std::isnan(cpuMs) && cpuMs > m_cpu[index]) m_cpu[index] = cpuMs;
        }
    }

    // Temps GPU mesuré d'une image terminée (côté CPU) à l'instant `time`. Les mesures arrivent
    // quelques images en retard : elles sont rangées à la place de leur image, si elle est encore visible.
    void onGpuFrame(double time, float gpuMs) {
        const double previous = m_lastGpuTime;
        m_lastGpuTime = time;
        if (!m_hasSlice || std::isnan(gpuMs) || !(gpuMs >= 0.f)) return;
        const int64_t last = sliceOf(time);
        int64_t first = previous >= 0.0 && time - previous > 0.0 && time - previous <= kPauseSeconds ? sliceOf(previous) + 1 : last;
        if (first > last) first = last;
        markGpu(first, last, gpuMs);
    }

    // Temps GPU estimé (relevé à chaque rafraîchissement, de 50 ms à 5 s selon le .ini) : vaut pour
    // les tranches entre `from` et `to` qui contiennent des images (rien sur une coupure).
    void onGpuEstimate(double from, double to, float gpuMs) {
        if (!m_hasSlice || std::isnan(gpuMs) || !(gpuMs >= 0.f) || !(to >= from)) return;
        int64_t first = to - from <= kEstimateSpanSeconds ? sliceOf(from) + 1 : sliceOf(to);
        const int64_t last = sliceOf(to);
        if (first > last) first = last;
        markGpu(first, last, gpuMs, true);
    }

    // Remplit les colonnes de la courbe, de la plus ancienne à la plus récente (0 = pas d'image).
    void columns(double now, float* frameMs, float* cpuMs, float* gpuMs = nullptr) const {
        const int64_t newest = sliceOf(now);
        for (int i = 0; i < kColumns; ++i) {
            const int64_t slice = newest - (kColumns - 1 - i);
            const bool known = m_hasSlice && slice <= m_lastSlice && slice > m_lastSlice - kColumns;
            frameMs[i] = known ? m_frame[indexOf(slice)] : 0.f;
            cpuMs[i] = known ? m_cpu[indexOf(slice)] : 0.f;
            if (gpuMs) gpuMs[i] = known ? m_gpu[indexOf(slice)] : 0.f;
        }
    }

  private:
    static constexpr double kPauseSeconds = 1.0;
    static constexpr double kEstimateSpanSeconds = 6.0;  // intervalle de rafraîchissement le plus long, plus une marge

    static int64_t sliceOf(double time) { return static_cast<int64_t>(std::floor(time / kSliceSeconds)); }
    static int indexOf(int64_t slice) { return static_cast<int>(((slice % kColumns) + kColumns) % kColumns); }

    // Vide les tranches comprises entre la dernière connue et `slice` (elles vont être réutilisées).
    void advanceTo(int64_t slice) {
        if (!m_hasSlice || slice - m_lastSlice >= kColumns || slice < m_lastSlice) {
            for (int i = 0; i < kColumns; ++i) m_frame[i] = m_cpu[i] = m_gpu[i] = 0.f;
        } else {
            for (int64_t s = m_lastSlice + 1; s <= slice; ++s) m_frame[indexOf(s)] = m_cpu[indexOf(s)] = m_gpu[indexOf(s)] = 0.f;
        }
        m_lastSlice = slice;
        m_hasSlice = true;
    }

    // Note une valeur GPU sur les tranches [first, last] encore visibles (jamais au-delà de la plus
    // récente) ; avec `framesOnly`, seulement sur celles qui contiennent au moins une image.
    void markGpu(int64_t first, int64_t last, float gpuMs, bool framesOnly = false) {
        if (last > m_lastSlice) last = m_lastSlice;
        if (first <= m_lastSlice - kColumns) first = m_lastSlice - kColumns + 1;
        for (int64_t slice = first; slice <= last; ++slice) {
            const int index = indexOf(slice);
            if (framesOnly && !(m_frame[index] > 0.f)) continue;
            if (gpuMs > m_gpu[index]) m_gpu[index] = gpuMs;
        }
    }

    float m_frame[kColumns];
    float m_cpu[kColumns];
    float m_gpu[kColumns];
    double m_lastTime;
    double m_lastGpuTime;
    int64_t m_lastSlice;
    bool m_hasSlice;
};

}  // namespace xrpo
