// Lecture des réglages publiés par la fenêtre de XRPerfSensors.exe (mémoire partagée).
#pragma once

#include "shared_block.h"

namespace xrpo {

class SettingsReader {
  public:
    SettingsReader() = default;
    SettingsReader(const SettingsReader&) = delete;
    SettingsReader& operator=(const SettingsReader&) = delete;
    ~SettingsReader();

    // Copie les réglages publiés dans `out`. Retourne false si XRPerfSensors n'a rien publié
    // (programme non lancé, ou version sans fenêtre de réglages).
    bool read(SettingsBlock& out);

  private:
    void tryOpen();

    void* m_mapping = nullptr;
    const SettingsBlock* m_view = nullptr;
    unsigned long long m_lastAttemptMs = 0;
};

}  // namespace xrpo
