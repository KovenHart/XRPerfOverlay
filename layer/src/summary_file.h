// Fichier texte du résumé de session, gardé ouvert pendant tout le jeu.
//
// Le fichier est créé une fois, au début de la session, puis réécrit sur place de temps en temps :
// une écriture dans un fichier déjà ouvert se fait dans le cache du système, en quelques
// microsecondes, sans accès disque dans la boucle de rendu. Si le jeu est fermé brutalement,
// Windows ferme le fichier et son dernier contenu est conservé.
#pragma once

#include <cstddef>
#include <string>

namespace xrpo {

class SummaryFile {
  public:
    SummaryFile() = default;
    SummaryFile(const SummaryFile&) = delete;
    SummaryFile& operator=(const SummaryFile&) = delete;
    ~SummaryFile() { close(); }

    // Crée `folder\fileName`. Tant que keep() n'a pas été appelé, le fichier est provisoire : il
    // disparaît à la fermeture (les sessions trop courtes ne laissent donc aucune trace).
    bool open(const std::wstring& folder, const std::wstring& fileName);
    bool isOpen() const { return m_handle != nullptr; }
    bool kept() const { return m_kept; }
    void keep();
    // Remplace tout le contenu du fichier.
    void write(const char* text, size_t length);
    void close();
    const std::wstring& path() const { return m_path; }

  private:
    void* m_handle = nullptr;
    bool m_kept = false;
    std::wstring m_path;
};

// Date et heure locales : "2026-10-10 06:50:12", ou "2026-10-10_06-50-12" pour un nom de fichier.
std::string LocalTimeText();
// Heure locale en minutes depuis minuit (pour l'horloge du panneau).
int LocalClockMinutes();
// Heure locale en secondes depuis minuit.
int LocalClockSeconds();
std::wstring LocalTimeFileStamp();

}  // namespace xrpo
