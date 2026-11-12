// Test de l'envoi RTMP (layer/src/rtmp.cpp) contre ffmpeg en serveur :
//   rtmp_test <video.h264 (Annex B, délimiteurs d'images)> <son.aac (ADTS)> <rtmp://127.0.0.1:port/app> <clé> <i/s>
// Envoie l'image et le son au rythme réel (accéléré x4), puis affiche ce que dit l'envoi. Lancé par
// tests/rtmp_test.sh, qui vérifie le fichier reçu par ffmpeg (ffprobe).

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#include "../layer/src/rtmp.h"

using namespace xrpo;

static std::vector<uint8_t> ReadFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    if (argc < 6) return 2;
    const std::vector<uint8_t> video = ReadFile(argv[1]), audio = ReadFile(argv[2]);
    const int fps = std::atoi(argv[5]);
    // Images : découpées aux délimiteurs (NAL 9).
    std::vector<std::vector<uint8_t>> frames;
    {
        const auto units = SplitAnnexB(video.data(), video.size());
        std::vector<uint8_t> current;
        for (const auto& u : units) {
            if ((u.first[0] & 0x1F) == 9 && !current.empty()) {
                frames.push_back(current);
                current.clear();
            }
            const uint8_t prefix[] = {0, 0, 0, 1};
            current.insert(current.end(), prefix, prefix + 4);
            current.insert(current.end(), u.first, u.first + u.second);
        }
        if (!current.empty()) frames.push_back(current);
    }
    // Son : trames ADTS (en-tête de 7 octets sans contrôle d'erreur).
    std::vector<std::vector<uint8_t>> aac;
    for (size_t p = 0; p + 7 <= audio.size();) {
        if (audio[p] != 0xFF || (audio[p + 1] & 0xF0) != 0xF0) return 3;
        const size_t length = ((audio[p + 3] & 3) << 11) | (audio[p + 4] << 3) | (audio[p + 5] >> 5);
        const size_t header = (audio[p + 1] & 1) ? 7 : 9;
        aac.emplace_back(audio.begin() + p + header, audio.begin() + p + length);
        p += length;
    }
    std::printf("%zu images, %zu trames AAC\n", frames.size(), aac.size());

    StreamConfig config;
    config.url = argv[3];
    config.key = argv[4];
    config.width = 640;
    config.height = 360;
    config.fps = fps;
    config.videoKbps = 20000;
    config.audioKbps = 160;
    RtmpStreamer streamer;
    std::string error;
    bool refused = false;
    if (!streamer.start(config, 5000, error, refused)) {
        std::printf("ECHEC : %s (refus %d)\n", error.c_str(), refused ? 1 : 0);
        return 1;
    }
    std::vector<uint8_t> avc;
    if (!AvcConfig(frames[0].data(), frames[0].size(), avc)) {
        std::printf("pas de SPS / PPS\n");
        return 1;
    }
    streamer.setVideoConfig(avc);
    streamer.setAudioConfig(AacConfig(48000, 2));
    size_t a = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        const uint32_t t = static_cast<uint32_t>(i * 1000 / fps);
        while (a < aac.size() && a * 1024 * 1000 / 48000 <= t) {
            std::vector<uint8_t> copy = aac[a];
            streamer.pushAudio(static_cast<uint32_t>(a * 1024 * 1000 / 48000), std::move(copy));
            ++a;
        }
        bool key = false;
        for (const auto& u : SplitAnnexB(frames[i].data(), frames[i].size())) key = key || (u.first[0] & 0x1F) == 5;
        std::vector<uint8_t> avcc;
        if (AnnexBToAvcc(frames[i].data(), frames[i].size(), avcc)) streamer.pushVideo(t, key, std::move(avcc));
        std::this_thread::sleep_for(std::chrono::microseconds(250000 / fps));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const StreamStats s = streamer.stats();
    std::printf("etat %u, %llu octets, %u kbit/s, %u perdues, %u reconnexions, erreur \"%s\"\n", s.state, static_cast<unsigned long long>(s.bytes), s.kbps,
                s.dropped, s.reconnects, s.error.c_str());
    streamer.stop();
    return s.state == kStreamLive && s.dropped == 0 ? 0 : 1;
}
