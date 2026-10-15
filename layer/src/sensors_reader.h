// Lecture de la mémoire partagée alimentée par XRPerfSensors.exe.
#pragma once

#include "shared_block.h"

namespace xrpo {

class SensorsReader {
  public:
    ~SensorsReader();

    // Copie les dernières valeurs dans `out`. Retourne false si le programme de capteurs ne tourne
    // pas ou si ses données ont plus de 3 secondes.
    bool read(SensorBlock& out);

  private:
    void tryOpen();

    void* m_mapping = nullptr;
    const SensorBlock* m_view = nullptr;
    unsigned long long m_lastAttemptMs = 0;
};

}  // namespace xrpo
