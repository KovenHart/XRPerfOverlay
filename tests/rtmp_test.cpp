// Test de l'envoi RTMP (layer/src/rtmp.cpp) contre ffmpeg en serveur :
//   rtmp_test <vidéo> <son.aac (ADTS)> <rtmp://127.0.0.1:port/app> <clé> <i/s> [h264|hevc|av1]
// Vidéo : H.264 ou H.265 en Annex B avec délimiteurs d'images, ou AV1 en OBU (délimiteurs temporels).
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
    const std::string name = argc > 6 ? argv[6] : "h264";
    const int codec = name == "hevc" ? kCodecHevc : name == "av1" ? kCodecAv1 : kCodecH264;
    // Images : découpées aux délimiteurs (NAL 9 en H.264, 35 en H.265, OBU 2 en AV1).
    std::vector<std::vector<uint8_t>> frames;
    if (codec == kCodecAv1) {
        size_t pos = 0, start = 0;
        while (pos < video.size()) {
            const size_t begin = pos;
            const uint8_t header = video[pos++];
            if (header & 0x04) ++pos;
            uint64_t length = 0;
            for (int i = 0; i < 8; ++i) {
                const uint8_t b = video[pos++];
                length |= static_cast<uint64_t>(b & 0x7F) << (7 * i);
                if (!(b & 0x80)) break;
            }
            if (((header >> 3) & 0xF) == 2 && begin > start) {
                frames.emplace_back(video.begin() + start, video.begin() + begin);
                start = begin;
            }
            pos += length;
        }
        if (start < video.size()) frames.emplace_back(video.begin() + start, video.end());
    } else {
        const auto units = SplitAnnexB(video.data(), video.size());
        std::vector<uint8_t> current;
        for (const auto& u : units) {
            const bool aud = codec == kCodecHevc ? ((u.first[0] >> 1) & 0x3F) == 35 : (u.first[0] & 0x1F) == 9;
            if (aud && !current.empty()) {
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
    config.codec = codec;
    RtmpStreamer streamer;
    std::string error;
    bool refused = false;
    if (!streamer.start(config, 5000, error, refused)) {
        std::printf("ECHEC : %s (refus %d)\n", error.c_str(), refused ? 1 : 0);
        return 1;
    }
    std::vector<uint8_t> header;
    if (!VideoConfig(codec, frames[0].data(), frames[0].size(), header)) {
        std::printf("pas d'en-tete de flux dans la premiere image\n");
        return 1;
    }
    std::printf("en-tete %s : %zu octets\n", name.c_str(), header.size());
    streamer.setVideoConfig(header);
    streamer.setAudioConfig(AacConfig(48000, 2));
    size_t a = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        const uint32_t t = static_cast<uint32_t>(i * 1000 / fps);
        while (a < aac.size() && a * 1024 * 1000 / 48000 <= t) {
            std::vector<uint8_t> copy = aac[a];
            streamer.pushAudio(static_cast<uint32_t>(a * 1024 * 1000 / 48000), std::move(copy));
            ++a;
        }
        const bool key = VideoKeyframe(codec, frames[i].data(), frames[i].size());
        if (i == 0 && !key) {
            std::printf("premiere image non reconnue comme image cle\n");
            return 1;
        }
        std::vector<uint8_t> packet;
        if (VideoPacket(codec, frames[i].data(), frames[i].size(), packet)) streamer.pushVideo(t, key, std::move(packet));
        std::this_thread::sleep_for(std::chrono::microseconds(250000 / fps));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const StreamStats s = streamer.stats();
    std::printf("etat %u, %llu octets, %u kbit/s, %u perdues, %u reconnexions, erreur \"%s\"\n", s.state, static_cast<unsigned long long>(s.bytes), s.kbps,
                s.dropped, s.reconnects, s.error.c_str());
    streamer.stop();
    return s.state == kStreamLive && s.dropped == 0 ? 0 : 1;
}
