// Mesure en sortie de chaîne : ce que le runtime reçoit réellement, après les autres layers.
//
// Un outil de génération d'images (framegen) est lui-même une layer placée sous la nôtre : il
// reçoit les images du jeu et en soumet davantage au runtime. Pour compter ces images, la layer
// ajoute un second maillon tout en bas de la chaîne (voir layer.cpp). Ce fichier contient les
// compteurs partagés entre ce maillon et l'overlay. Code portable, testé dans tests/preview.cpp.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>

namespace xrpo {

// Retient, pour chaque swapchain, si une image y a été rendue depuis sa dernière soumission.
// Une soumission ne montre quelque chose de nouveau que si l'une de ses swapchains est dans ce cas ;
// sinon c'est une répétition de l'image précédente, qui ne compte pas comme une image de plus.
// Sans verrou : les appels peuvent venir de plusieurs fils d'exécution.
class SwapchainTracker {
  public:
    // Une image de cette swapchain vient d'être rendue au runtime.
    void onRelease(uint64_t swapchain) {
        if (swapchain == 0) return;
        int slot = find(swapchain);
        if (slot < 0) slot = claim(swapchain);
        m_dirty[slot].store(true, std::memory_order_relaxed);
    }

    // Cette swapchain est soumise : retourne true si elle portait une image nouvelle.
    bool consume(uint64_t swapchain) {
        if (swapchain == 0) return false;
        const int slot = find(swapchain);
        return slot >= 0 && m_dirty[slot].exchange(false, std::memory_order_relaxed);
    }

  private:
    static constexpr int kSlots = 64;

    int find(uint64_t swapchain) const {
        for (int i = 0; i < kSlots; ++i)
            if (m_handle[i].load(std::memory_order_relaxed) == swapchain) return i;
        return -1;
    }

    int claim(uint64_t swapchain) {
        for (int i = 0; i < kSlots; ++i) {
            uint64_t empty = 0;
            if (m_handle[i].compare_exchange_strong(empty, swapchain, std::memory_order_relaxed)) return i;
            if (empty == swapchain) return i;  // un autre fil vient de l'inscrire
        }
        // Table pleine (jeu qui recrée souvent ses swapchains) : on repart d'une table vide.
        for (int i = 1; i < kSlots; ++i) {
            m_handle[i].store(0, std::memory_order_relaxed);
            m_dirty[i].store(false, std::memory_order_relaxed);
        }
        m_handle[0].store(swapchain, std::memory_order_relaxed);
        m_dirty[0].store(false, std::memory_order_relaxed);
        return 0;
    }

    std::atomic<uint64_t> m_handle[kSlots] = {};
    std::atomic<bool> m_dirty[kSlots] = {};
};

struct OutputTap {
    std::atomic<bool> installed{false};  // le maillon de sortie est en place
    std::atomic<uint64_t> submitted{0};  // images reçues par le runtime (xrEndFrame réussis)
    std::atomic<uint64_t> fresh{0};      // ... dont celles qui portaient un contenu nouveau
    // Plus courte durée d'affichage annoncée par le runtime lui-même : la fréquence du casque.
    // (Certains runtimes annoncent une durée allongée quand ils brident le jeu.)
    std::atomic<int64_t> periodNs{0};
    // Pendant une création d'instance : le maillon de sortie a bien été appelé par la layer du dessous.
    std::atomic<bool> reached{false};
    SwapchainTracker swapchains;
};

inline OutputTap g_outputTap;

// Vrai pendant que l'overlay rend sa propre image de swapchain : elle ne doit pas compter comme
// un contenu nouveau du jeu.
inline thread_local bool t_ownSwapchainCall = false;

// Débit d'un compteur sur la dernière seconde.
class RateMeter {
  public:
    void reset() { m_points.clear(); }

    void add(double now, uint64_t count) {
        if (!m_points.empty() && now - m_points.back().t > 2.0) m_points.clear();  // coupure : on repart de zéro
        m_points.push_back({now, count});
        // On garde comme point de départ le relevé le plus récent qui a au moins une seconde.
        while (m_points.size() >= 2 && now - m_points[1].t >= 1.0) m_points.pop_front();
    }

    // Par seconde ; NaN tant qu'il n'y a pas une seconde de recul.
    float rate() const {
        if (m_points.size() < 2) return std::numeric_limits<float>::quiet_NaN();
        const double span = m_points.back().t - m_points.front().t;
        if (span < 0.9) return std::numeric_limits<float>::quiet_NaN();
        return static_cast<float>(static_cast<double>(m_points.back().count - m_points.front().count) / span);
    }

  private:
    struct Point {
        double t;
        uint64_t count;
    };
    std::deque<Point> m_points;
};

// Un compteur relevé sur une seconde se trompe d'une image de temps en temps (71, 72, 73...) alors
// que le casque affiche tout : à moins de 2,5 % de la fréquence du casque, on affiche cette fréquence.
inline float SnapToRefresh(float rate, float refreshHz) {
    if (std::isnan(rate) || refreshHz <= 0.f) return rate;
    return std::fabs(rate - refreshHz) <= 0.025f * refreshHz ? refreshHz : rate;
}

}  // namespace xrpo
