// Direct vers Twitch et YouTube : envoi RTMP (le protocole de leurs serveurs d'entrée) d'une vidéo H.264 et
// d'un son AAC déjà encodés, en paquets FLV.
//
// Portable (Winsock sous Windows, sockets POSIX ailleurs) : testé dans tests/rtmp_test.cpp contre ffmpeg
// en serveur. L'enregistreur (video_record.cpp) encode et pousse les paquets ; un fil d'envoi les transmet
// dans l'ordre, sans jamais bloquer l'encodage : file bornée (au-delà, des images sont perdues jusqu'à la
// prochaine image clé), reconnexion automatique pendant une minute après une coupure.
//
// RTMP sans chiffrement (rtmp://) : accepté par Twitch et YouTube, comme le réglage par défaut d'OBS pour
// Twitch.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace xrpo {

// --- H.264 et AAC dans FLV ------------------------------------------------------------------------------

// Unités NAL d'un flux H.264 « Annex B » (préfixes 00 00 01 ou 00 00 00 01) : début et taille de chacune,
// sans le préfixe.
std::vector<std::pair<const uint8_t*, size_t>> SplitAnnexB(const uint8_t* data, size_t size);
// AVCDecoderConfigurationRecord (en-tête de la vidéo dans FLV) tiré des SPS et PPS d'une image Annex B.
bool AvcConfig(const uint8_t* annexB, size_t size, std::vector<uint8_t>& config);
// Image Annex B -> unités préfixées de leur taille sur 4 octets (FLV), sans SPS, PPS ni délimiteur
// (déjà dans l'en-tête). Retourne false si rien ne reste.
bool AnnexBToAvcc(const uint8_t* data, size_t size, std::vector<uint8_t>& out);
// AudioSpecificConfig de l'AAC-LC (en-tête du son dans FLV).
std::vector<uint8_t> AacConfig(int sampleRate, int channels);

// --- Envoi ------------------------------------------------------------------------------------------------

enum StreamState : uint32_t {
    kStreamIdle = 0,
    kStreamConnecting = 1,
    kStreamLive = 2,
    kStreamReconnecting = 3,
    kStreamFailed = 4,
};

struct StreamConfig {
    std::string url;  // rtmp://serveur[:port]/application
    std::string key;  // clé de stream (nom du flux publié)
    int width = 0, height = 0, fps = 0;
    int videoKbps = 0, audioKbps = 0;
    int audioRate = 48000, channels = 2;
    int reconnectSeconds = 60;  // durée des essais de reconnexion après une coupure
};

struct StreamStats {
    StreamState state = kStreamIdle;
    uint64_t bytes = 0;         // octets envoyés
    uint32_t kbps = 0;          // débit des dernières secondes
    uint32_t dropped = 0;       // images perdues (réseau trop lent, reconnexion)
    uint32_t reconnects = 0;
    std::string error;          // raison d'un échec (journal)
    bool refused = false;       // serveur joint, mais publication refusée (clé ?)
};

class RtmpConnection;

class RtmpStreamer {
  public:
    RtmpStreamer();
    ~RtmpStreamer();
    RtmpStreamer(const RtmpStreamer&) = delete;
    RtmpStreamer& operator=(const RtmpStreamer&) = delete;

    // Première connexion (bloquante, `timeoutMs` au plus), puis le fil d'envoi démarre. false : `error`.
    bool start(const StreamConfig& config, int timeoutMs, std::string& error, bool& refused);
    // En-têtes : à donner avant les paquets (renvoyés à chaque reconnexion).
    void setVideoConfig(std::vector<uint8_t> avcConfig);
    void setAudioConfig(std::vector<uint8_t> aacConfig);
    // Paquets dans l'ordre de leur temps (millisecondes depuis le début du direct).
    void pushVideo(uint32_t timeMs, bool keyframe, std::vector<uint8_t>&& avcc);
    void pushAudio(uint32_t timeMs, std::vector<uint8_t>&& aac);
    void stop();
    StreamStats stats();

  private:
    struct Packet {
        bool video = false;
        bool key = false;
        uint32_t time = 0;
        std::vector<uint8_t> data;
    };
    void run();
    void push(Packet&& packet);
    bool reconnect();

    StreamConfig m_config;
    RtmpConnection* m_connection = nullptr;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Packet> m_queue;
    size_t m_queued = 0;      // octets en attente
    size_t m_budget = 0;      // au-delà : paquets perdus
    bool m_waitKey = false;   // vidéo perdue : attendre la prochaine image clé
    bool m_stopping = false;
    std::vector<uint8_t> m_avcConfig, m_aacConfig;
    StreamStats m_stats;
    uint64_t m_rateBytes = 0;
    uint64_t m_connectionSent = 0;  // octets de la connexion en cours déjà comptés
    uint64_t m_rateStartMs = 0;
};

}  // namespace xrpo
