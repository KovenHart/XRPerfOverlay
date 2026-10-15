// Lecture de l'image de la fenêtre capturée par XRPerfSensors (mémoire partagée
// Local\XRPerfOverlay.Window, voir shared_block.h), pour la layer et pour XRPerfVR.
#pragma once

#include <cstdint>
#include <vector>

namespace xrpo {

class WindowShareReader {
  public:
    WindowShareReader() = default;
    WindowShareReader(const WindowShareReader&) = delete;
    WindowShareReader& operator=(const WindowShareReader&) = delete;
    ~WindowShareReader();

    // Signale à XRPerfSensors que l'image est voulue : la capture tourne tant que ce signal est
    // renouvelé (à appeler quelques fois par seconde tant que la fenêtre doit être affichée).
    void markWanted();

    // Copie l'image si elle a changé depuis la dernière copie : `pixels` reçoit width x height pixels
    // BGRA (alpha prémultiplié, lignes contiguës). Retourne false s'il n'y a rien de nouveau.
    bool readNew(std::vector<uint8_t>& pixels, int& width, int& height);

    // Oublie la dernière image copiée : la prochaine lecture la renverra (nouvelle texture...).
    void resetFrame() { m_lastFrame = 0; m_hasFrame = false; }

  private:
    void tryOpen();

    void* m_mapping = nullptr;
    uint8_t* m_view = nullptr;
    unsigned long long m_lastAttemptMs = 0;
    uint32_t m_lastFrame = 0;
    bool m_hasFrame = false;
};

}  // namespace xrpo
