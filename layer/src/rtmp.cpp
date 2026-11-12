// Envoi RTMP du direct (voir rtmp.h).
//
// RTMP en bref : poignée de main (C0 C1 / S0 S1 S2 / C2), puis des messages découpés en morceaux (« chunks »)
// préfixés d'un en-tête ; commandes en AMF0 (connect, createStream, publish) ; vidéo (type 9) et son (type 8)
// au format des balises FLV ; métadonnées (type 18, @setDataFrame). Le serveur peut demander des accusés de
// réception (Window Acknowledgement Size) et des réponses à ses « ping ».

#include "rtmp.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kNoSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kNoSocket = -1;
#endif

namespace xrpo {

namespace {

uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void CloseSocket(SocketHandle s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

// Attend que la socket soit lisible (read) ou inscriptible ; false si le délai passe.
bool WaitSocket(SocketHandle s, bool read, int timeoutMs) {
#ifdef _WIN32
    WSAPOLLFD p = {};
    p.fd = s;
    p.events = read ? POLLRDNORM : POLLWRNORM;
    return WSAPoll(&p, 1, timeoutMs) > 0;
#else
    pollfd p = {};
    p.fd = s;
    p.events = read ? POLLIN : POLLOUT;
    return poll(&p, 1, timeoutMs) > 0;
#endif
}

void SetBlocking(SocketHandle s, bool blocking) {
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
#endif
}

void Put16(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}
void Put24(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}
void Put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}
uint32_t Get24(const uint8_t* p) { return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2]; }
uint32_t Get32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

// --- AMF0 ---------------------------------------------------------------------------------------------------

void AmfNumber(std::vector<uint8_t>& v, double d) {
    v.push_back(0x00);
    uint64_t bits;
    std::memcpy(&bits, &d, 8);
    for (int i = 7; i >= 0; --i) v.push_back(static_cast<uint8_t>(bits >> (i * 8)));
}
void AmfBool(std::vector<uint8_t>& v, bool b) {
    v.push_back(0x01);
    v.push_back(b ? 1 : 0);
}
void AmfKey(std::vector<uint8_t>& v, const std::string& s) {
    Put16(v, static_cast<uint32_t>(s.size()));
    v.insert(v.end(), s.begin(), s.end());
}
void AmfString(std::vector<uint8_t>& v, const std::string& s) {
    v.push_back(0x02);
    AmfKey(v, s);
}
void AmfNull(std::vector<uint8_t>& v) { v.push_back(0x05); }
void AmfObjectEnd(std::vector<uint8_t>& v) {
    v.push_back(0);
    v.push_back(0);
    v.push_back(0x09);
}

// Lecture : chaînes "code" et "description" trouvées dans les objets, nombres en tête de message.
struct AmfReader {
    AmfReader(const uint8_t* begin, const uint8_t* stop) : p(begin), end(stop) {}
    const uint8_t* p;
    const uint8_t* end;
    std::vector<double> numbers;
    std::vector<std::string> strings;
    std::string code, description, lastKey;

    bool readValue(int depth = 0) {
        if (p >= end || depth > 8) return false;
        const uint8_t type = *p++;
        switch (type) {
            case 0x00: {  // nombre
                if (end - p < 8) return false;
                uint64_t bits = 0;
                for (int i = 0; i < 8; ++i) bits = (bits << 8) | p[i];
                p += 8;
                double d;
                std::memcpy(&d, &bits, 8);
                if (depth == 0) numbers.push_back(d);
                return true;
            }
            case 0x01:
                if (p >= end) return false;
                ++p;
                return true;
            case 0x02: {  // chaîne
                if (end - p < 2) return false;
                const size_t n = (size_t(p[0]) << 8) | p[1];
                p += 2;
                if (size_t(end - p) < n) return false;
                std::string s(reinterpret_cast<const char*>(p), n);
                p += n;
                if (depth == 0) strings.push_back(s);
                if (lastKey == "code") code = s;
                if (lastKey == "description") description = s;
                return true;
            }
            case 0x03:  // objet
                return readProperties(depth);
            case 0x08:  // tableau associatif
                if (end - p < 4) return false;
                p += 4;
                return readProperties(depth);
            case 0x05:
            case 0x06:
                return true;
            default:
                return false;  // types inutiles ici : la suite est ignorée
        }
    }
    bool readProperties(int depth) {
        while (end - p >= 3) {
            const size_t n = (size_t(p[0]) << 8) | p[1];
            if (n == 0 && p[2] == 0x09) {
                p += 3;
                return true;
            }
            p += 2;
            if (size_t(end - p) < n) return false;
            const std::string key(reinterpret_cast<const char*>(p), n);
            p += n;
            lastKey = key;
            if (!readValue(depth + 1)) return false;
            lastKey.clear();
        }
        return false;
    }
};

}  // namespace

// --- H.264 et AAC -------------------------------------------------------------------------------------------

std::vector<std::pair<const uint8_t*, size_t>> SplitAnnexB(const uint8_t* data, size_t size) {
    std::vector<std::pair<const uint8_t*, size_t>> units;
    size_t i = 0, start = SIZE_MAX;
    while (i + 3 <= size) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            if (start != SIZE_MAX) {
                size_t endUnit = i;
                while (endUnit > start && data[endUnit - 1] == 0) --endUnit;  // zéro du préfixe sur 4 octets
                if (endUnit > start) units.emplace_back(data + start, endUnit - start);
            }
            i += 3;
            start = i;
        } else {
            ++i;
        }
    }
    if (start != SIZE_MAX && start < size) units.emplace_back(data + start, size - start);
    return units;
}

bool AvcConfig(const uint8_t* annexB, size_t size, std::vector<uint8_t>& config) {
    const uint8_t* sps = nullptr;
    const uint8_t* pps = nullptr;
    size_t spsSize = 0, ppsSize = 0;
    for (const auto& unit : SplitAnnexB(annexB, size)) {
        const int type = unit.first[0] & 0x1F;
        if (type == 7 && !sps && unit.second >= 4) {
            sps = unit.first;
            spsSize = unit.second;
        } else if (type == 8 && !pps) {
            pps = unit.first;
            ppsSize = unit.second;
        }
    }
    if (!sps || !pps) return false;
    config.clear();
    config.push_back(1);       // version
    config.push_back(sps[1]);  // profil
    config.push_back(sps[2]);  // compatibilité
    config.push_back(sps[3]);  // niveau
    config.push_back(0xFF);    // tailles sur 4 octets
    config.push_back(0xE1);    // un SPS
    Put16(config, static_cast<uint32_t>(spsSize));
    config.insert(config.end(), sps, sps + spsSize);
    config.push_back(1);  // un PPS
    Put16(config, static_cast<uint32_t>(ppsSize));
    config.insert(config.end(), pps, pps + ppsSize);
    return true;
}

bool AnnexBToAvcc(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(size + 16);
    for (const auto& unit : SplitAnnexB(data, size)) {
        const int type = unit.first[0] & 0x1F;
        if (type == 7 || type == 8 || type == 9) continue;
        Put32(out, static_cast<uint32_t>(unit.second));
        out.insert(out.end(), unit.first, unit.first + unit.second);
    }
    return !out.empty();
}

std::vector<uint8_t> AacConfig(int sampleRate, int channels) {
    static const int kRates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
    int index = 3;
    for (int i = 0; i < 13; ++i)
        if (kRates[i] == sampleRate) index = i;
    const int objectType = 2;  // AAC-LC
    const uint16_t bits = static_cast<uint16_t>((objectType << 11) | (index << 7) | ((channels & 0xF) << 3));
    return {static_cast<uint8_t>(bits >> 8), static_cast<uint8_t>(bits)};
}

// --- Connexion ----------------------------------------------------------------------------------------------

class RtmpConnection {
  public:
    ~RtmpConnection() { close(); }

    // Connexion, poignée de main et publication. `refused` : le serveur a refusé la publication.
    bool open(const StreamConfig& config, int timeoutMs, std::string& error, bool& refused) {
        refused = false;
        const uint64_t deadline = NowMs() + static_cast<uint64_t>(timeoutMs);
        std::string host, app, port = "1935";
        if (!parseUrl(config.url, host, port, app)) {
            error = "adresse RTMP invalide";
            return false;
        }
        if (!connectTcp(host, port, timeoutMs, error)) return false;
        if (!handshake(deadline, error)) return false;
        // Morceaux de 4096 octets dans notre sens (128 par défaut).
        {
            std::vector<uint8_t> m;
            Put32(m, kChunkSize);
            if (!sendMessage(2, 0, 1, 0, m)) return fail(error, "envoi impossible");
        }
        const std::string tcUrl = "rtmp://" + host + (port != "1935" ? ":" + port : std::string()) + "/" + app;
        {
            std::vector<uint8_t> m;
            AmfString(m, "connect");
            AmfNumber(m, 1);
            m.push_back(0x03);
            AmfKey(m, "app");
            AmfString(m, app);
            AmfKey(m, "type");
            AmfString(m, "nonprivate");
            AmfKey(m, "flashVer");
            AmfString(m, "FMLE/3.0 (compatible; FMSc/1.0)");
            AmfKey(m, "swfUrl");
            AmfString(m, tcUrl);
            AmfKey(m, "tcUrl");
            AmfString(m, tcUrl);
            AmfObjectEnd(m);
            if (!sendMessage(3, 0, 20, 0, m)) return fail(error, "envoi impossible");
        }
        Reply reply;
        if (!waitReply(1, deadline, reply)) return fail(error, "pas de reponse a la connexion");
        if (reply.name != "_result") {
            refused = true;
            return fail(error, "connexion refusee (" + reply.code + " " + reply.description + ")");
        }
        // Publication : releaseStream et FCPublish comme les logiciels courants, puis createStream.
        sendCommand("releaseStream", 2, &config.key);
        sendCommand("FCPublish", 3, &config.key);
        sendCommand("createStream", 4, nullptr);
        if (!waitReply(4, deadline, reply) || reply.name != "_result" || reply.numbers.size() < 2)
            return fail(error, "flux non cree par le serveur");
        m_stream = static_cast<uint32_t>(reply.numbers[1]);
        {
            std::vector<uint8_t> m;
            AmfString(m, "publish");
            AmfNumber(m, 5);
            AmfNull(m);
            AmfString(m, config.key);
            AmfString(m, "live");
            if (!sendMessage(4, 0, 20, m_stream, m)) return fail(error, "envoi impossible");
        }
        // Réponse « NetStream.Publish.Start » ; une clé refusée donne une erreur ou la fermeture.
        while (true) {
            Reply status;
            if (!waitReply(0, deadline, status)) {
                refused = m_closedByPeer;
                return fail(error, m_closedByPeer ? "publication refusee (cle de stream ?)" : "pas de reponse a la publication");
            }
            if (status.name != "onStatus") continue;
            if (status.code == "NetStream.Publish.Start") break;
            refused = true;
            return fail(error, "publication refusee (" + status.code + (status.description.empty() ? "" : " : " + status.description) + ")");
        }
        // Réglages du flux.
        {
            std::vector<uint8_t> m;
            AmfString(m, "@setDataFrame");
            AmfString(m, "onMetaData");
            m.push_back(0x08);
            Put32(m, 11);
            AmfKey(m, "width");
            AmfNumber(m, config.width);
            AmfKey(m, "height");
            AmfNumber(m, config.height);
            AmfKey(m, "framerate");
            AmfNumber(m, config.fps);
            AmfKey(m, "videocodecid");
            AmfNumber(m, 7);
            AmfKey(m, "videodatarate");
            AmfNumber(m, config.videoKbps);
            AmfKey(m, "audiocodecid");
            AmfNumber(m, 10);
            AmfKey(m, "audiodatarate");
            AmfNumber(m, config.audioKbps);
            AmfKey(m, "audiosamplerate");
            AmfNumber(m, config.audioRate);
            AmfKey(m, "audiosamplesize");
            AmfNumber(m, 16);
            AmfKey(m, "stereo");
            AmfBool(m, config.channels == 2);
            AmfKey(m, "encoder");
            AmfString(m, "XR Perf Overlay");
            AmfObjectEnd(m);
            if (!sendMessage(5, 0, 18, m_stream, m)) return fail(error, "envoi impossible");
        }
        return true;
    }

    void close() {
        if (m_socket != kNoSocket) CloseSocket(m_socket);
        m_socket = kNoSocket;
    }

    bool sendVideo(uint32_t time, bool key, bool header, const std::vector<uint8_t>& data) {
        std::vector<uint8_t>& m = m_scratch;
        m.clear();
        m.push_back(static_cast<uint8_t>((key ? 0x10 : 0x20) | 7));
        m.push_back(header ? 0 : 1);
        Put24(m, 0);  // pas d'images B : décalage d'affichage nul
        m.insert(m.end(), data.begin(), data.end());
        return sendMessage(6, time, 9, m_stream, m);
    }

    bool sendAudio(uint32_t time, bool header, const std::vector<uint8_t>& data) {
        std::vector<uint8_t>& m = m_scratch;
        m.clear();
        m.push_back(0xAF);  // AAC, 44 kHz (valeur fixe pour l'AAC), 16 bits, stéréo
        m.push_back(header ? 0 : 1);
        m.insert(m.end(), data.begin(), data.end());
        return sendMessage(4, time, 8, m_stream, m);
    }

    // Messages du serveur en attente (ping, accusés) ; false si la connexion est fermée.
    bool service() {
        while (m_socket != kNoSocket && WaitSocket(m_socket, true, 0)) {
            if (!receiveSome()) return false;
            Reply ignored;
            while (parseOne(ignored)) {
            }
        }
        return m_socket != kNoSocket;
    }

    uint64_t sentBytes() const { return m_sent; }

  private:
    static constexpr uint32_t kChunkSize = 4096;

    struct Reply {
        std::string name;
        double transaction = 0;
        std::vector<double> numbers;
        std::string code, description;
    };
    struct InChunk {
        uint32_t timestamp = 0, length = 0, stream = 0;
        uint8_t type = 0;
        std::vector<uint8_t> payload;
    };

    static bool fail(std::string& error, const std::string& text) {
        error = text;
        return false;
    }

    static bool parseUrl(const std::string& url, std::string& host, std::string& port, std::string& app) {
        const std::string prefix = "rtmp://";
        if (url.compare(0, prefix.size(), prefix) != 0) return false;
        const std::string rest = url.substr(prefix.size());
        const size_t slash = rest.find('/');
        if (slash == std::string::npos || slash == 0) return false;
        std::string authority = rest.substr(0, slash);
        app = rest.substr(slash + 1);
        while (!app.empty() && app.back() == '/') app.pop_back();
        const size_t colon = authority.rfind(':');
        if (colon != std::string::npos) {
            port = authority.substr(colon + 1);
            authority = authority.substr(0, colon);
        }
        host = authority;
        return !host.empty() && !app.empty() && !port.empty();
    }

    bool connectTcp(const std::string& host, const std::string& port, int timeoutMs, std::string& error) {
        addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* list = nullptr;
        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &list) != 0 || !list) return fail(error, "serveur introuvable (" + host + ")");
        for (addrinfo* a = list; a; a = a->ai_next) {
            SocketHandle s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (s == kNoSocket) continue;
            SetBlocking(s, false);
            const int r = ::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen));
            bool ok = r == 0;
            if (!ok && WaitSocket(s, false, timeoutMs)) {
                int soError = 0;
                socklen_t length = sizeof(soError);
                ok = getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &length) == 0 && soError == 0;
            }
            if (!ok) {
                CloseSocket(s);
                continue;
            }
            SetBlocking(s, true);
            int one = 1;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
            int buffer = 1 << 20;
            setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
#ifdef _WIN32
            DWORD sendTimeout = 10000;
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeout), sizeof(sendTimeout));
#else
            timeval sendTimeout = {10, 0};
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));
#endif
            m_socket = s;
            break;
        }
        freeaddrinfo(list);
        if (m_socket == kNoSocket) return fail(error, "connexion impossible a " + host + ":" + port);
        return true;
    }

    bool sendAll(const uint8_t* data, size_t size) {
        while (size > 0) {
            const int n = ::send(m_socket, reinterpret_cast<const char*>(data), static_cast<int>(std::min<size_t>(size, 1 << 20)), 0);
            if (n <= 0) {
                close();
                return false;
            }
            data += n;
            size -= static_cast<size_t>(n);
            m_sent += static_cast<uint64_t>(n);
        }
        return true;
    }

    bool readExact(uint8_t* data, size_t size, uint64_t deadline) {
        while (size > 0) {
            const uint64_t now = NowMs();
            if (now >= deadline || !WaitSocket(m_socket, true, static_cast<int>(deadline - now))) return false;
            const int n = ::recv(m_socket, reinterpret_cast<char*>(data), static_cast<int>(size), 0);
            if (n <= 0) return false;
            data += n;
            size -= static_cast<size_t>(n);
        }
        return true;
    }

    bool handshake(uint64_t deadline, std::string& error) {
        std::vector<uint8_t> c(1 + 1536, 0);
        c[0] = 3;
        std::mt19937 random(static_cast<uint32_t>(NowMs()));
        for (size_t i = 9; i < c.size(); ++i) c[i] = static_cast<uint8_t>(random());
        if (!sendAll(c.data(), c.size())) return fail(error, "envoi impossible");
        std::vector<uint8_t> s(1 + 1536 + 1536);
        if (!readExact(s.data(), s.size(), deadline)) return fail(error, "pas de reponse du serveur (poignee de main)");
        if (s[0] != 3) return fail(error, "serveur RTMP inattendu");
        if (!sendAll(s.data() + 1, 1536)) return fail(error, "envoi impossible");  // C2 = S1
        return true;
    }

    // Un message en morceaux : en-tête complet (type 0) puis suites (type 3).
    bool sendMessage(uint32_t chunkStream, uint32_t timestamp, uint8_t type, uint32_t stream, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t>& out = m_out;
        out.clear();
        const bool extended = timestamp >= 0xFFFFFF;
        const size_t length = payload.size();
        size_t offset = 0;
        bool first = true;
        do {
            out.push_back(static_cast<uint8_t>((first ? 0x00 : 0xC0) | chunkStream));
            if (first) {
                Put24(out, extended ? 0xFFFFFF : timestamp);
                Put24(out, static_cast<uint32_t>(length));
                out.push_back(type);
                out.push_back(static_cast<uint8_t>(stream));
                out.push_back(static_cast<uint8_t>(stream >> 8));
                out.push_back(static_cast<uint8_t>(stream >> 16));
                out.push_back(static_cast<uint8_t>(stream >> 24));
            }
            if (extended) Put32(out, timestamp);
            const size_t n = std::min<size_t>(kChunkSize, length - offset);
            out.insert(out.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.begin() + static_cast<std::ptrdiff_t>(offset + n));
            offset += n;
            first = false;
        } while (offset < length);
        return sendAll(out.data(), out.size());
    }

    void sendCommand(const char* name, double transaction, const std::string* argument) {
        std::vector<uint8_t> m;
        AmfString(m, name);
        AmfNumber(m, transaction);
        AmfNull(m);
        if (argument) AmfString(m, *argument);
        sendMessage(3, 0, 20, 0, m);
    }

    bool receiveSome() {
        uint8_t buffer[4096];
        const int n = ::recv(m_socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
        if (n <= 0) {
            m_closedByPeer = true;
            close();
            return false;
        }
        m_in.insert(m_in.end(), buffer, buffer + n);
        m_received += static_cast<uint64_t>(n);
        // Accusé de réception demandé par le serveur.
        if (m_ackWindow > 0 && m_received - m_acked >= m_ackWindow) {
            std::vector<uint8_t> m;
            Put32(m, static_cast<uint32_t>(m_received));
            sendMessage(2, 0, 3, 0, m);
            m_acked = m_received;
        }
        return true;
    }

    // Réponse à la commande `transaction` (0 : prochaine commande « onStatus »...).
    bool waitReply(double transaction, uint64_t deadline, Reply& reply) {
        while (true) {
            Reply r;
            while (parseOne(r)) {
                if (!r.name.empty() && (transaction == 0 ? r.name == "onStatus" : (r.transaction == transaction && (r.name == "_result" || r.name == "_error")))) {
                    reply = r;
                    return true;
                }
                r = Reply();
            }
            const uint64_t now = NowMs();
            if (m_socket == kNoSocket || now >= deadline || !WaitSocket(m_socket, true, static_cast<int>(deadline - now))) return false;
            if (!receiveSome()) return false;
        }
    }

    // Un message complet du serveur, s'il y en a un dans m_in : traité ; true si un message a été lu.
    bool parseOne(Reply& reply) {
        while (true) {
            size_t p = 0;
            if (m_in.size() < 1) return false;
            const uint8_t fmt = m_in[0] >> 6;
            uint32_t csid = m_in[0] & 0x3F;
            p = 1;
            if (csid == 0) {
                if (m_in.size() < 2) return false;
                csid = 64u + m_in[1];
                p = 2;
            } else if (csid == 1) {
                if (m_in.size() < 3) return false;
                csid = 64u + m_in[1] + 256u * m_in[2];
                p = 3;
            }
            InChunk& c = m_chunks[csid % 64];
            const size_t headerSize = fmt == 0 ? 11 : fmt == 1 ? 7 : fmt == 2 ? 3 : 0;
            if (m_in.size() < p + headerSize) return false;
            uint32_t ts = c.timestamp;
            if (fmt <= 2) ts = Get24(&m_in[p]);
            if (fmt <= 1) {
                c.length = Get24(&m_in[p + 3]);
                c.type = m_in[p + 6];
            }
            if (fmt == 0) c.stream = m_in[p + 7] | (m_in[p + 8] << 8) | (m_in[p + 9] << 16) | (uint32_t(m_in[p + 10]) << 24);
            p += headerSize;
            if ((fmt <= 2 && ts == 0xFFFFFF) || (fmt == 3 && c.timestamp >= 0xFFFFFF)) {
                if (m_in.size() < p + 4) return false;
                ts = Get32(&m_in[p]);
                p += 4;
            }
            if (c.length > (16u << 20)) {  // message absurde : connexion abandonnée
                close();
                return false;
            }
            const size_t remaining = c.length - c.payload.size();
            const size_t n = std::min<size_t>(remaining, m_inChunkSize);
            if (m_in.size() < p + n) return false;
            c.timestamp = ts;
            c.payload.insert(c.payload.end(), m_in.begin() + static_cast<std::ptrdiff_t>(p), m_in.begin() + static_cast<std::ptrdiff_t>(p + n));
            m_in.erase(m_in.begin(), m_in.begin() + static_cast<std::ptrdiff_t>(p + n));
            if (c.payload.size() < c.length) continue;  // suite au prochain morceau
            std::vector<uint8_t> payload;
            payload.swap(c.payload);
            handle(c.type, c.timestamp, payload, reply);
            return true;
        }
    }

    void handle(uint8_t type, uint32_t, const std::vector<uint8_t>& m, Reply& reply) {
        if (type == 1 && m.size() >= 4) {
            m_inChunkSize = std::max<uint32_t>(1, Get32(m.data()) & 0x7FFFFFFF);
        } else if (type == 5 && m.size() >= 4) {
            m_ackWindow = Get32(m.data());
        } else if (type == 4 && m.size() >= 6 && m[0] == 0 && m[1] == 6) {
            std::vector<uint8_t> pong = {0, 7, m[2], m[3], m[4], m[5]};  // réponse au ping
            sendMessage(2, 0, 4, 0, pong);
        } else if (type == 20 && !m.empty()) {
            AmfReader r(m.data(), m.data() + m.size());
            while (r.p < r.end && r.readValue()) {
            }
            if (!r.strings.empty()) reply.name = r.strings[0];
            if (!r.numbers.empty()) reply.transaction = r.numbers[0];
            reply.numbers = r.numbers;
            reply.code = r.code;
            reply.description = r.description;
        }
    }

    SocketHandle m_socket = kNoSocket;
    uint32_t m_stream = 1;
    std::vector<uint8_t> m_in, m_out, m_scratch;
    InChunk m_chunks[64];
    uint32_t m_inChunkSize = 128;
    uint32_t m_ackWindow = 0;
    uint64_t m_received = 0, m_acked = 0, m_sent = 0;
    bool m_closedByPeer = false;
};

// --- Fil d'envoi --------------------------------------------------------------------------------------------

RtmpStreamer::RtmpStreamer() {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

RtmpStreamer::~RtmpStreamer() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool RtmpStreamer::start(const StreamConfig& config, int timeoutMs, std::string& error, bool& refused) {
    stop();
    m_config = config;
    // File : quatre secondes du débit prévu (au moins 2 Mo) ; au-delà, le réseau ne suit pas.
    m_budget = std::max<size_t>(size_t(2) << 20, static_cast<size_t>(config.videoKbps + config.audioKbps) * 1000 / 8 * 4);
    m_stats = StreamStats();
    m_stats.state = kStreamConnecting;
    m_connection = new RtmpConnection();
    if (!m_connection->open(config, timeoutMs, error, refused)) {
        delete m_connection;
        m_connection = nullptr;
        m_stats.state = kStreamFailed;
        m_stats.error = error;
        m_stats.refused = refused;
        return false;
    }
    m_stats.state = kStreamLive;
    m_stopping = false;
    m_waitKey = true;  // le direct commence à une image clé
    m_rateStartMs = NowMs();
    m_rateBytes = 0;
    m_connectionSent = m_connection->sentBytes();
    m_thread = std::thread([this] { run(); });
    return true;
}

void RtmpStreamer::setVideoConfig(std::vector<uint8_t> avcConfig) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_avcConfig = std::move(avcConfig);
}

void RtmpStreamer::setAudioConfig(std::vector<uint8_t> aacConfig) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_aacConfig = std::move(aacConfig);
}

void RtmpStreamer::pushVideo(uint32_t timeMs, bool keyframe, std::vector<uint8_t>&& avcc) {
    Packet p;
    p.video = true;
    p.key = keyframe;
    p.time = timeMs;
    p.data = std::move(avcc);
    push(std::move(p));
}

void RtmpStreamer::pushAudio(uint32_t timeMs, std::vector<uint8_t>&& aac) {
    Packet p;
    p.time = timeMs;
    p.data = std::move(aac);
    push(std::move(p));
}

void RtmpStreamer::push(Packet&& packet) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_thread.joinable() || m_stopping) return;
    // Réseau trop lent ou reconnexion : on perd des images (jusqu'à la prochaine image clé) plutôt que du
    // retard ; le son suit tant qu'il reste de la place.
    if (packet.video) {
        if (m_waitKey && !packet.key) {
            ++m_stats.dropped;
            return;
        }
        if (m_queued + packet.data.size() > m_budget || m_stats.state != kStreamLive) {
            ++m_stats.dropped;
            m_waitKey = true;
            return;
        }
        m_waitKey = false;
    } else if (m_queued + packet.data.size() > m_budget + (size_t(256) << 10) || m_stats.state != kStreamLive) {
        return;
    }
    m_queued += packet.data.size();
    m_queue.push_back(std::move(packet));
    m_wake.notify_one();
}

void RtmpStreamer::run() {
    bool headers = true;  // en-têtes à envoyer (début, reconnexion)
    bool started = false;
    uint32_t base = 0;    // temps du premier paquet de la connexion (le direct y commence à 0)
    while (true) {
        Packet packet;
        std::vector<uint8_t> avc, aac;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_for(lock, std::chrono::milliseconds(200), [this] { return m_stopping || !m_queue.empty(); });
            if (m_stopping) break;
            if (m_queue.empty()) {
                lock.unlock();
                if (m_connection && !m_connection->service() && !reconnect()) break;
                continue;
            }
            packet = std::move(m_queue.front());
            m_queue.pop_front();
            m_queued -= packet.data.size();
            avc = m_avcConfig;
            aac = m_aacConfig;
        }
        if (!started) {
            if (!packet.video || !packet.key) continue;  // le direct commence à une image clé
            started = true;
            base = packet.time;
        }
        const uint32_t time = packet.time >= base ? packet.time - base : 0;
        bool ok = m_connection && m_connection->service();
        if (ok && headers && !avc.empty()) {
            ok = m_connection->sendVideo(time, true, true, avc) && (aac.empty() || m_connection->sendAudio(time, true, aac));
            headers = !ok;
        }
        if (ok && !headers) ok = packet.video ? m_connection->sendVideo(time, packet.key, false, packet.data) : m_connection->sendAudio(time, false, packet.data);
        if (!ok) {
            if (!reconnect()) break;
            headers = true;
            started = false;
            continue;
        }
        const uint64_t now = NowMs();
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint64_t sent = m_connection->sentBytes();  // depuis cette connexion
        m_stats.bytes += sent - m_connectionSent;
        m_connectionSent = sent;
        if (now - m_rateStartMs >= 2000) {
            m_stats.kbps = static_cast<uint32_t>((m_stats.bytes - m_rateBytes) * 8 / (now - m_rateStartMs));
            m_rateBytes = m_stats.bytes;
            m_rateStartMs = now;
        }
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_connection) {
        m_connection->close();
        delete m_connection;
        m_connection = nullptr;
    }
}

// Coupure : nouveaux essais (toutes les 2 s) pendant reconnectSeconds ; false si c'est fini.
bool RtmpStreamer::reconnect() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.state = kStreamReconnecting;
        m_queue.clear();
        m_queued = 0;
        m_waitKey = true;
        ++m_stats.reconnects;
    }
    const uint64_t until = NowMs() + static_cast<uint64_t>(m_config.reconnectSeconds) * 1000;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_stopping) return false;
        }
        if (m_connection) {
            m_connection->close();
            delete m_connection;
        }
        m_connection = new RtmpConnection();
        std::string error;
        bool refused = false;
        if (m_connection->open(m_config, 10000, error, refused)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stats.state = kStreamLive;
            m_connectionSent = m_connection->sentBytes();
            return true;
        }
        if (refused || NowMs() >= until) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stats.state = kStreamFailed;
            m_stats.error = "connexion perdue : " + error;
            m_stats.refused = refused;
            return false;
        }
        std::unique_lock<std::mutex> lock(m_mutex);
        m_wake.wait_for(lock, std::chrono::seconds(2), [this] { return m_stopping; });
    }
}

void RtmpStreamer::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        m_wake.notify_all();
    }
    if (m_thread.joinable()) m_thread.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_connection) {
        m_connection->close();
        delete m_connection;
        m_connection = nullptr;
    }
    m_queue.clear();
    m_queued = 0;
    if (m_stats.state != kStreamFailed) m_stats.state = kStreamIdle;
}

StreamStats RtmpStreamer::stats() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

}  // namespace xrpo
