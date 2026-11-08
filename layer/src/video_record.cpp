// XRPerfRecord.dll : enregistreur vidéo du casque, chargé par XRPerfSensors (sensors/VideoRecorder.cs).
//
// 1. Images : la layer (jeu OpenXR) ou XRPerfVR (jeu SteamVR) copie l'image d'un œil dans des textures
//    Direct3D partagées (video_producer.cpp) et les décrit dans le bloc Local\XRPerfOverlay.Video. Ici, la
//    dernière image est recopiée dans une texture à nous, puis cadrée, réduite et convertie en NV12 (YUV
//    4:2:0, BT.709) par deux petits shaders, sans jamais passer par le processeur.
// 2. Encodage : Media Foundation (« sink writer ») avec l'encodeur matériel de la carte graphique
//    (NVENC, AMF, Quick Sync) : H.264, H.265 (HEVC) ou AV1, dans un fichier MP4.
// 3. Son : WASAPI, son du PC en boucle (« loopback ») et micro, chacun en option, remis sur une même
//    ligne de temps (video_math.h, AudioTimeline) puis encodés en AAC.
//
// Horloge commune : QueryPerformanceCounter. La vidéo a une cadence fixe : à chaque instant prévu, la
// dernière image reçue est encodée (répétée si le jeu n'en a pas fourni de nouvelle).

#include <windows.h>

#include <audioclient.h>
#include <codecapi.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <timeapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "shared_block.h"
#include "video_math.h"

namespace {

using namespace xrpo;

// --- Interface exportée (même disposition que VideoRecorder.cs) ----------------------------------------

struct XrprOptions {
    uint32_t size;
    uint32_t codec;         // 0 = H.264, 1 = H.265 (HEVC), 2 = AV1
    uint32_t height;        // hauteur de la vidéo (720 à 2160)
    uint32_t fps;           // 30, 60...
    uint32_t bitrateKbps;   // débit visé
    uint32_t eye;           // 0 = gauche, 1 = droit
    float aspect;           // largeur / hauteur
    float field;            // part de l'image de l'œil gardée (0.4 à 1)
    uint32_t gameAudio;     // 1 : son du PC
    uint32_t micAudio;      // 1 : micro
    wchar_t folder[260];
    wchar_t renderDevice[256];   // sortie dont on enregistre le son ("" : celle par défaut)
    wchar_t captureDevice[256];  // micro ("" : celui par défaut)
};

struct XrprStatus {
    int32_t state;          // 0 arrêté, 1 en attente du jeu, 2 enregistrement, 3 finalisation
    int32_t reason;         // dernière fin ou erreur (codes ci-dessous)
    double seconds;         // durée enregistrée
    uint64_t bytes;         // taille du fichier
    uint32_t frames;        // images écrites
    uint32_t repeated;      // ... dont répétées (pas d'image neuve du jeu)
    uint32_t dropped;       // instants sautés (encodeur ou programme en retard)
    uint32_t audio;         // 1 son du PC actif, 2 micro actif, 4 son du PC en échec, 8 micro en échec
    int32_t hresult;        // détail de la dernière erreur
    wchar_t file[260];
    wchar_t game[64];
    wchar_t encoder[64];
};

enum State : int32_t { kStopped = 0, kWaiting = 1, kRecording = 2, kFinishing = 3 };

// Fins et erreurs (XrprStatus.reason, valeur de retour de XrprStart).
enum Reason : int32_t {
    kOk = 0,
    kBusy = 1,             // un enregistrement est déjà en cours
    kBadOptions = 2,
    kNoMediaFoundation = 3,
    kNoDevice = 10,        // carte graphique du jeu introuvable ou Direct3D indisponible
    kNoConversion = 11,    // conversion NV12 impossible (shaders, format)
    kNoTextures = 12,      // textures du jeu illisibles
    kNoEncoder = 20,       // pas d'encodeur matériel pour ce codec (ou refus du format)
    kNoFile = 21,          // fichier impossible à créer
    kWriteFailed = 22,     // écriture impossible (disque plein)
    kVideoRejected = 23,   // l'encodeur ou le fichier MP4 a refusé une image
    kAudioRejected = 24,   // ... un morceau de son
    kGameEnded = 30,       // le jeu s'est arrêté : fichier terminé
    kStoppedByUser = 31,
};

template <typename T>
class Ptr {
  public:
    Ptr() = default;
    Ptr(const Ptr&) = delete;
    Ptr& operator=(const Ptr&) = delete;
    ~Ptr() { reset(); }
    void reset() {
        if (m_p) m_p->Release();
        m_p = nullptr;
    }
    T* get() const { return m_p; }
    T* operator->() const { return m_p; }
    T** put() {
        reset();
        return &m_p;
    }
    void** putVoid() { return reinterpret_cast<void**>(put()); }
    explicit operator bool() const { return m_p != nullptr; }

  private:
    T* m_p = nullptr;
};

// GUID définis ici plutôt que tirés de bibliothèques qui varient d'un SDK à l'autre.
const GUID kVideoAv1 = {0x31305641, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};  // 'AV01'
constexpr GUID MakeGuid(unsigned long a, unsigned short b, unsigned short c, unsigned char d0, unsigned char d1, unsigned char d2, unsigned char d3,
                        unsigned char d4, unsigned char d5, unsigned char d6, unsigned char d7) {
    return GUID{a, b, c, {d0, d1, d2, d3, d4, d5, d6, d7}};
}
const GUID kRateControl = MakeGuid(STATIC_CODECAPI_AVEncCommonRateControlMode);
const GUID kMeanBitRate = MakeGuid(STATIC_CODECAPI_AVEncCommonMeanBitRate);
const GUID kGopSize = MakeGuid(STATIC_CODECAPI_AVEncMPVGOPSize);
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};  // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
const GUID kSubtypePcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};    // KSDATAFORMAT_SUBTYPE_PCM
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
const CLSID kMMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID kIMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const IID kIAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4c32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const IID kIAudioCaptureClient = {0xC8ADBD64, 0xE71E, 0x48a0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};

const GUID& CodecSubtype(uint32_t codec) { return codec == 1 ? MFVideoFormat_HEVC : codec == 2 ? kVideoAv1 : MFVideoFormat_H264; }

constexpr int kAudioRate = 48000;
constexpr int64_t kAudioLatency100ns = 1'500'000;  // le son est écrit avec 150 ms de retard (paquets en route)

int64_t QpcFrequency() {
    static int64_t frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<int64_t>(f.QuadPart);
    }();
    return frequency;
}

// Instant présent en unités de 100 ns (même échelle que les positions des paquets WASAPI).
int64_t Now100ns() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    const int64_t f = QpcFrequency();
    return (c.QuadPart / f) * 10'000'000 + (c.QuadPart % f) * 10'000'000 / f;
}

#ifndef XRPO_VERSION_TEXT
#define XRPO_VERSION_TEXT "dev"
#endif

// Journal : %LOCALAPPDATA%\\XRPerfOverlay\\XRPerfRecord.log (le dernier enregistrement et ses éventuelles erreurs).
void RecordLog(const char* format, ...) {
    wchar_t folder[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", folder, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring path = std::wstring(folder) + L"\\XRPerfOverlay";
    CreateDirectoryW(path.c_str(), nullptr);
    path += L"\\XRPerfRecord.log";
    // Gardé court : recommencé au-delà de 256 Ko.
    WIN32_FILE_ATTRIBUTE_DATA data;
    const bool big = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) && (data.nFileSizeHigh != 0 || data.nFileSizeLow > 256 * 1024);
    FILE* f = _wfopen(path.c_str(), big ? L"wb" : L"ab");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u.%03u  ", static_cast<unsigned>(t.wYear), static_cast<unsigned>(t.wMonth), static_cast<unsigned>(t.wDay),
                 static_cast<unsigned>(t.wHour), static_cast<unsigned>(t.wMinute), static_cast<unsigned>(t.wSecond), static_cast<unsigned>(t.wMilliseconds));
    va_list args;
    va_start(args, format);
    std::vfprintf(f, format, args);
    va_end(args);
    std::fputs("\r\n", f);
    std::fclose(f);
}

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], n, nullptr, nullptr);
    return out;
}

const char* kCodecNames[] = {"H.264", "HEVC", "AV1"};

// Formats : texture à nous (même famille, sans type) et vue lue par le shader (valeurs brutes : un format
// _SRGB est lu sans conversion, le shader sait s'il faut encoder).
DXGI_FORMAT TypelessOf(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8X8_TYPELESS;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM:
            return DXGI_FORMAT_R10G10B10A2_TYPELESS;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM:
            return DXGI_FORMAT_R16G16B16A16_TYPELESS;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return DXGI_FORMAT_R32G32B32A32_TYPELESS;
        default:
            return f;
    }
}

DXGI_FORMAT ViewOf(DXGI_FORMAT f) {
    switch (TypelessOf(f)) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return f == DXGI_FORMAT_R16G16B16A16_UNORM ? DXGI_FORMAT_R16G16B16A16_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default: return f;
    }
}

// --- Shaders : cadrage, réduction, conversion RVB -> YUV 4:2:0 (BT.709, plage limitée) --------------------

const char kShaders[] = R"(
cbuffer Params : register(b0) {
    float4 crop;    // origine et taille du cadre dans l'image de l'œil (coordonnées 0 à 1)
    float4 taps;    // xy : écart des quatre échantillons (coordonnées de l'image) ; z : 1 si valeurs linéaires
};
Texture2D eyeImage : register(t0);
SamplerState linearClamp : register(s0);

struct VsOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };

VsOut VsMain(uint id : SV_VertexID) {
    VsOut o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.position = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = t;
    return o;
}

float3 fetch(float2 uv) {
    float2 p = crop.xy + uv * crop.zw;
    float3 c = 0.25 * (eyeImage.SampleLevel(linearClamp, p + float2(taps.x, taps.y), 0).rgb +
                       eyeImage.SampleLevel(linearClamp, p + float2(-taps.x, taps.y), 0).rgb +
                       eyeImage.SampleLevel(linearClamp, p + float2(taps.x, -taps.y), 0).rgb +
                       eyeImage.SampleLevel(linearClamp, p + float2(-taps.x, -taps.y), 0).rgb);
    c = saturate(c);
    if (taps.z > 0.5) {
        float3 low = c * 12.92;
        float3 high = 1.055 * pow(max(c, 1e-6), 1.0 / 2.4) - 0.055;
        c = lerp(high, low, step(c, 0.0031308));
    }
    return c;
}

float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

float PsLuma(VsOut i) : SV_Target {
    return (16.0 + 219.0 * luma(fetch(i.uv))) / 255.0;
}

float2 PsChroma(VsOut i) : SV_Target {
    float3 c = fetch(i.uv);
    float y = luma(c);
    float cb = (c.b - y) / 1.8556;
    float cr = (c.r - y) / 1.5748;
    return float2((128.0 + 224.0 * cb) / 255.0, (128.0 + 224.0 * cr) / 255.0);
}
)";

struct ShaderParams {
    float crop[4];
    float taps[4];
};

typedef HRESULT(WINAPI* PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**,
                                     ID3DBlob**);

// --- Son ---------------------------------------------------------------------------------------------------

class AudioSource {
  public:
    ~AudioSource() { stop(); }

    // `loopback` : son joué par une sortie ; sinon une entrée (micro). `device` vide : celle par défaut.
    bool start(bool loopback, const std::wstring& device, int64_t origin100ns) {
        m_loopback = loopback;
        m_device = device;
        m_origin = origin100ns;
        m_timeline.reset(0);
        m_ready = false;
        m_failed = false;
        m_stop = false;
        m_thread = std::thread([this] { run(); });
        // Démarrage : on attend le résultat (trois secondes au plus : casques Bluetooth lents) pour l'annoncer.
        for (int i = 0; i < 300 && !m_ready && !m_failed; ++i) Sleep(10);
        return m_ready;
    }

    void stop() {
        m_stop = true;
        if (m_thread.joinable()) m_thread.join();
    }

    bool active() const { return m_ready && !m_failed; }

    void mixInto(int64_t from, float* out, int64_t frames) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_timeline.mixInto(from, out, frames);
    }

  private:
    void run() {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            if (!open()) {
                m_failed = true;
            } else {
                m_ready = true;
                capture();
            }
        } catch (...) {
            m_failed = true;  // le son s'arrête, la vidéo continue
        }
        m_captureClient.reset();
        if (m_client) m_client->Stop();
        m_client.reset();
        if (SUCCEEDED(co)) CoUninitialize();
    }

    bool open() {
        Ptr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(kMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIMMDeviceEnumerator, enumerator.putVoid()))) return false;
        Ptr<IMMDevice> device;
        HRESULT hr = E_FAIL;
        if (!m_device.empty()) hr = enumerator->GetDevice(m_device.c_str(), device.put());
        if (FAILED(hr)) hr = enumerator->GetDefaultAudioEndpoint(m_loopback ? eRender : eCapture, eConsole, device.put());
        if (FAILED(hr) || !device) return false;
        if (FAILED(device->Activate(kIAudioClient, CLSCTX_ALL, nullptr, m_client.putVoid()))) return false;

        // D'abord 48 kHz stéréo en virgule flottante, converti par Windows ; sinon le format du périphérique,
        // converti ici.
        WAVEFORMATEXTENSIBLE wanted = {};
        wanted.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wanted.Format.nChannels = 2;
        wanted.Format.nSamplesPerSec = kAudioRate;
        wanted.Format.wBitsPerSample = 32;
        wanted.Format.nBlockAlign = 8;
        wanted.Format.nAvgBytesPerSec = kAudioRate * 8;
        wanted.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wanted.Samples.wValidBitsPerSample = 32;
        wanted.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        wanted.SubFormat = kSubtypeFloat;
        const DWORD loop = m_loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
        hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, loop | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                 2'000'000, 0, &wanted.Format, nullptr);
        if (SUCCEEDED(hr)) {
            m_channels = 2;
            m_rate = kAudioRate;
            m_float = true;
            m_bits = 32;
        } else {
            // Nouveau client (un échec d'Initialize ne permet pas de réessayer sur le même).
            m_client.reset();
            if (FAILED(device->Activate(kIAudioClient, CLSCTX_ALL, nullptr, m_client.putVoid()))) return false;
            WAVEFORMATEX* mix = nullptr;
            if (FAILED(m_client->GetMixFormat(&mix)) || !mix) return false;
            m_channels = mix->nChannels;
            m_rate = static_cast<int>(mix->nSamplesPerSec);
            m_bits = mix->wBitsPerSample;
            m_float = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
            if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                const WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix);
                m_float = IsEqualGUID(ext->SubFormat, kSubtypeFloat) != 0;
                if (!m_float && !IsEqualGUID(ext->SubFormat, kSubtypePcm)) m_bits = 0;
            }
            hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, loop, 2'000'000, 0, mix, nullptr);
            CoTaskMemFree(mix);
            if (FAILED(hr) || m_channels <= 0 || m_rate <= 0 || (m_float && m_bits != 32) || (!m_float && m_bits != 16 && m_bits != 24 && m_bits != 32))
                return false;
        }
        if (FAILED(m_client->GetService(kIAudioCaptureClient, m_captureClient.putVoid()))) return false;
        return SUCCEEDED(m_client->Start());
    }

    float sampleAt(const BYTE* data, int64_t index) const {
        if (m_float) return reinterpret_cast<const float*>(data)[index];
        if (m_bits == 16) return reinterpret_cast<const int16_t*>(data)[index] / 32768.f;
        if (m_bits == 24) {
            const BYTE* p = data + index * 3;
            const int32_t v = static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 8) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 24));
            return static_cast<float>(v) / 2147483648.f;
        }
        return static_cast<float>(reinterpret_cast<const int32_t*>(data)[index]) / 2147483648.f;
    }

    void capture() {
        std::vector<float> interleaved, stereo, resampled;
        while (!m_stop) {
            Sleep(10);
            UINT32 packet = 0;
            while (!m_stop && SUCCEEDED(m_captureClient->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                UINT64 devicePosition = 0, qpcPosition = 0;
                if (FAILED(m_captureClient->GetBuffer(&data, &frames, &flags, &devicePosition, &qpcPosition))) break;
                if (frames > 0) {
                    const int64_t n = frames;
                    interleaved.resize(static_cast<size_t>(n * m_channels));
                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data)
                        std::fill(interleaved.begin(), interleaved.end(), 0.f);
                    else
                        for (int64_t i = 0; i < n * m_channels; ++i) interleaved[static_cast<size_t>(i)] = sampleAt(data, i);
                    stereo.resize(static_cast<size_t>(n * 2));
                    ToStereo(interleaved.data(), m_channels, n, stereo.data());
                    const float* out = stereo.data();
                    int64_t outFrames = n;
                    if (m_rate != kAudioRate) {
                        resample(stereo, n, resampled);
                        out = resampled.data();
                        outFrames = static_cast<int64_t>(resampled.size() / 2);
                    }
                    // Position sur la ligne de temps : instant du premier échantillon (unités de 100 ns).
                    // (Avant le début de l'enregistrement : position négative, ignorée par la ligne de temps.)
                    std::lock_guard<std::mutex> lock(m_mutex);
                    // Instant du paquet inconnu ou douteux : collé à la suite du précédent.
                    const bool timed = qpcPosition > 0 && !(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR);
                    const double seconds = (static_cast<double>(static_cast<int64_t>(qpcPosition) - m_origin)) / 1e7;
                    // Un paquet daté dans le futur (horloge folle) est collé à la suite, sans décaler la ligne de temps.
                    const double nowSeconds = static_cast<double>(Now100ns() - m_origin) / 1e7;
                    const bool plausible = timed && seconds > -60.0 && seconds < nowSeconds + 0.5;
                    const int64_t position = plausible ? static_cast<int64_t>(std::llround(seconds * kAudioRate)) : m_timeline.end();
                    m_timeline.write(position, out, outFrames);
                }
                m_captureClient->ReleaseBuffer(frames);
            }
        }
    }

    // Changement de fréquence par interpolation linéaire, avec reprise d'un paquet à l'autre.
    void resample(const std::vector<float>& in, int64_t frames, std::vector<float>& out) {
        out.clear();
        // Position en images d'entrée : de -1 (entre la dernière du paquet précédent et la première de
        // celui-ci) à frames - 1.
        const double step = static_cast<double>(m_rate) / kAudioRate;
        while (m_phase < static_cast<double>(frames - 1)) {
            const int64_t i = static_cast<int64_t>(std::floor(m_phase));
            const double t = m_phase - static_cast<double>(i);
            for (int c = 0; c < 2; ++c) {
                const float a = i < 0 ? m_last[c] : in[static_cast<size_t>(i * 2 + c)];
                const float b = in[static_cast<size_t>((i + 1) * 2 + c)];
                out.push_back(static_cast<float>(a + (b - a) * t));
            }
            m_phase += step;
        }
        m_phase -= static_cast<double>(frames);
        if (frames > 0) {
            m_last[0] = in[static_cast<size_t>((frames - 1) * 2)];
            m_last[1] = in[static_cast<size_t>((frames - 1) * 2 + 1)];
        }
    }

    bool m_loopback = true;
    std::wstring m_device;
    int64_t m_origin = 0;
    std::thread m_thread;
    std::atomic<bool> m_stop{false}, m_ready{false}, m_failed{false};
    Ptr<IAudioClient> m_client;
    Ptr<IAudioCaptureClient> m_captureClient;
    int m_channels = 2, m_rate = kAudioRate, m_bits = 32;
    bool m_float = true;
    double m_phase = -1.0;
    float m_last[2] = {0.f, 0.f};
    std::mutex m_mutex;
    AudioTimeline m_timeline{480};
};

// --- Enregistreur ----------------------------------------------------------------------------------------

// Appareil Direct3D partagé avec les fils de l'encodeur : une suite d'appels (états puis dessin) ne doit
// pas être coupée par les siens.
class DeviceLock {
  public:
    explicit DeviceLock(ID3D11Multithread* m) : m_m(m) {
        if (m_m) m_m->Enter();
    }
    ~DeviceLock() { unlock(); }
    void unlock() {
        if (m_m) m_m->Leave();
        m_m = nullptr;
    }

  private:
    ID3D11Multithread* m_m;
};

class Recorder {
  public:
    // Fin du programme sans arrêt préalable : le fil est abandonné (on ne peut pas l'attendre ici).
    ~Recorder() {
        if (m_thread.joinable()) m_thread.detach();
    }

    int start(void* share, uint64_t bytes, const XrprOptions& options) {
        if (m_thread.joinable()) {
            if (m_state.load() != kStopped) return kBusy;
            m_thread.join();
        }
        if (!share || bytes < sizeof(VideoShareHeader)) return kBadOptions;
        if (options.size != sizeof(XrprOptions) || options.codec > 2 || options.fps < 10 || options.fps > 240 || options.height < 240 ||
            options.height > 4320 || options.bitrateKbps < 500 || options.bitrateKbps > 500000 || !(options.aspect > 0.2f && options.aspect < 5.f))
            return kBadOptions;
        m_options = options;
        m_options.folder[259] = m_options.renderDevice[255] = m_options.captureDevice[255] = 0;
        m_block = static_cast<volatile VideoShareHeader*>(share);
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_status = XrprStatus();
        }
        m_stop = false;
        m_state = kWaiting;
        // Demande d'images : œil et cadence, nouvel enregistrement.
        volatile VideoShareHeader* b = m_block;
        b->eye = options.eye == 1 ? 1 : 0;
        b->fps = static_cast<float>(options.fps);
        b->recordStartTickMs = 0;
        b->recorderTickMs = GetTickCount64();
        b->session = b->session + 1;
        for (int i = 0; i < kVideoSlots; ++i) b->slotBusy[i] = 0;
        MemoryBarrier();
        b->wanted = 1;
        try {
            m_thread = std::thread([this] { run(); });
        } catch (...) {
            b->wanted = 0;
            m_state = kStopped;
            return kBadOptions;
        }
        return kOk;
    }

    void stop() {
        m_stop = true;
        if (m_thread.joinable()) m_thread.join();
    }

    void status(XrprStatus& out) {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        out = m_status;
        out.state = m_state.load();
    }

  private:
    // --- Boucle ----------------------------------------------------------------------------------------
    void run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        timeBeginPeriod(1);  // attentes à la milliseconde près (sinon Windows arrondit à 15,6 ms)
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool mf = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
        int reason = mf ? kOk : kNoMediaFoundation;
        RecordLog("Demarrage (XRPerfRecord %s).", XRPO_VERSION_TEXT);
        try {
            if (mf) reason = record();
        } catch (...) {
            reason = kNoConversion;  // mémoire épuisée... : le fichier est terminé quand même
        }
        try {
            finish();
        } catch (...) {
        }
        timeEndPeriod(1);
        if (mf) MFShutdown();
        if (SUCCEEDED(co)) CoUninitialize();
        volatile VideoShareHeader* b = m_block;
        b->wanted = 0;
        b->recordStartTickMs = 0;
        for (int i = 0; i < kVideoSlots; ++i) b->slotBusy[i] = 0;
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_status.reason = reason;
            RecordLog("Fin (raison %d) : %.1f s, %u image(s) dont %u repetee(s), %u sautee(s), %llu octets, son %u.", reason, m_status.seconds, m_status.frames,
                      m_status.repeated, m_status.dropped, static_cast<unsigned long long>(m_status.bytes), m_status.audio);
        }
        m_state = kStopped;
    }

    void alive() { m_block->recorderTickMs = GetTickCount64(); }

    bool producerAlive() {
        const unsigned long long tick = GetTickCount64();
        return m_block->producerPid != 0 && VideoAge(tick, m_block->producerTickMs) < 2000;
    }

    int record() {
        m_videoEnd = 0;
        // 1. Attente de la première image d'un jeu.
        while (!m_stop) {
            alive();
            if (producerAlive() && m_block->latest != 0) break;
            Sleep(10);
        }
        if (m_stop) return kStoppedByUser;
        m_pid = m_block->producerPid;
        int reason = setupDevice(m_block->adapterLuid);
        if (reason != kOk) return reason;
        // Première image (quelques essais : les textures viennent d'être créées).
        bool got = false;
        for (int i = 0; i < 100 && !m_stop && !got; ++i) {
            alive();
            const int r = fetch(got);
            if (r != kOk) return r;
            if (!got) Sleep(10);
        }
        if (m_stop) return kStoppedByUser;
        if (!got) return kNoTextures;
        reason = setupWriter();
        if (reason != kOk) return reason;

        // 2. Son : chacun en option ; un échec n'empêche pas la vidéo. L'horloge commune part d'ici.
        m_origin = Now100ns();
        uint32_t audio = 0;
        if (m_options.gameAudio) {
            m_gameSound.reset(new AudioSource());
            audio |= m_gameSound->start(true, m_options.renderDevice, m_origin) ? 1u : 4u;
        }
        if (m_options.micAudio) {
            m_micSound.reset(new AudioSource());
            audio |= m_micSound->start(false, m_options.captureDevice, m_origin) ? 2u : 8u;
        }
        if (!(audio & 1u)) m_gameSound.reset();
        if (!(audio & 2u)) m_micSound.reset();
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_status.audio = audio;
        }
        m_block->recordStartTickMs = GetTickCount64();
        m_state = kRecording;

        // 3. Une image à chaque instant prévu.
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
        if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        const int64_t period = 10'000'000 / static_cast<int64_t>(m_options.fps);
        int64_t tick = 0;
        double lastFresh = static_cast<double>(m_origin) / 1e7;
        unsigned long long lastSizeCheck = 0;
        reason = kStoppedByUser;
        while (!m_stop) {
            const int64_t due = m_origin + tick * period;
            const int64_t wait = due - Now100ns();
            if (wait > 0) {
                if (timer) {
                    LARGE_INTEGER t;
                    t.QuadPart = -wait;
                    SetWaitableTimer(timer, &t, 0, nullptr, nullptr, FALSE);
                    WaitForSingleObject(timer, 100);
                } else {
                    Sleep(static_cast<DWORD>(wait / 10'000));
                }
            }
            alive();
            const int64_t now = Now100ns();
            // Très en retard (machine surchargée) : les instants manqués sont sautés.
            if (now - due > 3 * period) {
                const int64_t skip = (now - m_origin) / period - tick;
                if (skip > 0) {
                    tick += skip;
                    std::lock_guard<std::mutex> lock(m_statusMutex);
                    m_status.dropped += static_cast<uint32_t>(skip);
                }
            }
            bool fresh = false;
            const int r = fetch(fresh);
            if (r != kOk) {
                reason = r;
                break;
            }
            const double nowSeconds = static_cast<double>(now) / 1e7;
            if (fresh) lastFresh = nowSeconds;
            // Jeu arrêté (ou un autre jeu) : le fichier est terminé.
            if (!fresh && nowSeconds - lastFresh > 10.0 && (!producerAlive() || m_block->producerPid != m_pid)) {
                reason = kGameEnded;
                break;
            }
            const int w = writeVideo(tick * period, period, fresh);
            if (w != kOk) {
                reason = w;
                break;
            }
            const int a = writeAudio((now - m_origin) - kAudioLatency100ns);
            if (a != kOk) {
                reason = a;
                break;
            }
            ++tick;
            const unsigned long long ms = GetTickCount64();
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_status.seconds = static_cast<double>(tick * period) / 1e7;
            if (ms - lastSizeCheck >= 1000) {
                lastSizeCheck = ms;
                WIN32_FILE_ATTRIBUTE_DATA data;
                if (GetFileAttributesExW(m_path.c_str(), GetFileExInfoStandard, &data))
                    m_status.bytes = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            }
        }
        if (timer) CloseHandle(timer);
        m_videoEnd = tick * period;
        return reason;
    }

    // Fin : son jusqu'à la fin de la vidéo, fichier terminé, tout libéré.
    void finish() {
        m_state = kFinishing;
        if (m_writer && m_writing && m_videoSamples == 0) m_writing = false;  // arrêt aussitôt : pas de fichier vide
        if (m_writer && m_writing) {
            if (m_gameSound) m_gameSound->stop();
            if (m_micSound) m_micSound->stop();
            writeAudio(m_videoEnd);
            const HRESULT hr = m_writer->Finalize();
            if (FAILED(hr)) {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_status.hresult = hr;
            }
            WIN32_FILE_ATTRIBUTE_DATA data;
            if (GetFileAttributesExW(m_path.c_str(), GetFileExInfoStandard, &data)) {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_status.bytes = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            }
        }
        const bool unfinished = m_writer && !m_writing;  // fichier créé mais jamais commencé (encodeur refusé...)
        m_writing = false;
        m_gameSound.reset();
        m_micSound.reset();
        m_writer.reset();
        if (unfinished && !m_path.empty() && DeleteFileW(m_path.c_str())) {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_status.file[0] = 0;
            m_status.bytes = 0;
        }
        m_allocator.reset();
        m_manager.reset();
        closeTextures();
        m_private.reset();
        m_privateView.reset();
        m_vs.reset();
        m_psY.reset();
        m_psUV.reset();
        m_params.reset();
        m_sampler.reset();
        m_copyFence.reset();
        m_context4.reset();
        m_multithread.reset();
        m_context.reset();
        m_device1.reset();
        m_device5.reset();
        m_device.reset();
        m_d3d12.reset();
        m_lastFrame = 0;
        m_openPid = 0;
        m_openGeneration = 0;
        m_luid = 0;
        m_stuckSlot = -1;
        m_hasFrame = false;
        m_privateWidth = m_privateHeight = 0;
        m_privateFormat = DXGI_FORMAT_UNKNOWN;
    }

    // --- Carte graphique --------------------------------------------------------------------------------
    int setupDevice(uint64_t luidValue) {
        LUID luid;
        luid.LowPart = static_cast<DWORD>(luidValue & 0xFFFFFFFFu);
        luid.HighPart = static_cast<LONG>(luidValue >> 32);
        Ptr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.putVoid()))) return fail(kNoDevice, E_FAIL);
        Ptr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, adapter.put()) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.AdapterLuid.LowPart == luid.LowPart && desc.AdapterLuid.HighPart == luid.HighPart) break;
            adapter.reset();
        }
        if (!adapter) return fail(kNoDevice, DXGI_ERROR_NOT_FOUND);
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        HRESULT hr = D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       levels, 2, D3D11_SDK_VERSION, m_device.put(), nullptr, m_context.put());
        if (FAILED(hr)) return fail(kNoDevice, hr);
        m_device->QueryInterface(__uuidof(ID3D11Device1), m_device1.putVoid());
        m_device->QueryInterface(__uuidof(ID3D11Device5), m_device5.putVoid());
        if (!m_device1 || !m_device5) return fail(kNoDevice, E_NOINTERFACE);
        // L'encodeur utilise le même appareil depuis ses propres fils d'exécution.
        if (SUCCEEDED(m_device->QueryInterface(__uuidof(ID3D11Multithread), m_multithread.putVoid()))) m_multithread->SetMultithreadProtected(TRUE);
        m_context->QueryInterface(__uuidof(ID3D11DeviceContext4), m_context4.putVoid());
        if (!m_context4) return fail(kNoDevice, E_NOINTERFACE);
        hr = m_device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), m_copyFence.putVoid());
        if (FAILED(hr)) return fail(kNoDevice, hr);
        m_copyValue = 0;
        if (!m_fenceEvent) m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_copyEvent) m_copyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent || !m_copyEvent) return fail(kNoDevice, HRESULT_FROM_WIN32(GetLastError()));
        m_luid = luidValue;
        // Direct3D 12, seulement pour ouvrir les objets partagés par leur nom (textures et fence).
        hr = D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), m_d3d12.putVoid());
        if (FAILED(hr)) return fail(kNoDevice, hr);

        // NV12 en cible de rendu (plan Y en R8, plan UV en R8G8).
        UINT support = 0;
        if (FAILED(m_device->CheckFormatSupport(DXGI_FORMAT_NV12, &support)) || !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET))
            return fail(kNoConversion, E_NOTIMPL);
        HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
        PFN_Compile compile = compiler ? reinterpret_cast<PFN_Compile>(reinterpret_cast<void*>(GetProcAddress(compiler, "D3DCompile"))) : nullptr;
        if (!compile) return fail(kNoConversion, E_NOINTERFACE);
        Ptr<ID3DBlob> vs, psY, psUV, errors;
        hr = compile(kShaders, sizeof(kShaders) - 1, "video", nullptr, nullptr, "VsMain", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, vs.put(), errors.put());
        if (SUCCEEDED(hr)) hr = compile(kShaders, sizeof(kShaders) - 1, "video", nullptr, nullptr, "PsLuma", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, psY.put(), errors.put());
        if (SUCCEEDED(hr)) hr = compile(kShaders, sizeof(kShaders) - 1, "video", nullptr, nullptr, "PsChroma", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, psUV.put(), errors.put());
        if (SUCCEEDED(hr)) hr = m_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_vs.put());
        if (SUCCEEDED(hr)) hr = m_device->CreatePixelShader(psY->GetBufferPointer(), psY->GetBufferSize(), nullptr, m_psY.put());
        if (SUCCEEDED(hr)) hr = m_device->CreatePixelShader(psUV->GetBufferPointer(), psUV->GetBufferSize(), nullptr, m_psUV.put());
        if (FAILED(hr)) return fail(kNoConversion, hr);
        D3D11_BUFFER_DESC cb = {};
        cb.ByteWidth = sizeof(ShaderParams);
        cb.Usage = D3D11_USAGE_DYNAMIC;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(hr = m_device->CreateBuffer(&cb, nullptr, m_params.put()))) return fail(kNoConversion, hr);
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(hr = m_device->CreateSamplerState(&sd, m_sampler.put()))) return fail(kNoConversion, hr);
        VideoSize(static_cast<int>(m_options.height), m_options.aspect, m_width, m_height);
        return kOk;
    }

    int fail(int reason, HRESULT hr, const char* step = "") {
        uint32_t frames = 0;
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_status.hresult = hr;
            frames = m_status.frames;
        }
        RecordLog("Erreur %d (%s) : 0x%08lx, apres %u image(s) ecrite(s).", reason, step, static_cast<unsigned long>(hr), frames);
        return reason;
    }

    void closeTextures() {
        for (Ptr<ID3D11Texture2D>& t : m_shared) t.reset();
        m_fence.reset();
    }

    bool openTextures(uint32_t pid, uint32_t generation) {
        closeTextures();
        m_openPid = 0;
        wchar_t name[96];
        for (int i = 0; i <= kVideoSlots; ++i) {
            const int slot = i < kVideoSlots ? i : -1;
            VideoTextureName(name, 96, pid, generation, slot);
            HANDLE handle = nullptr;
            if (FAILED(m_d3d12->OpenSharedHandleByName(name, GENERIC_ALL, &handle)) || !handle) return false;
            HRESULT hr;
            if (slot >= 0)
                hr = m_device1->OpenSharedResource1(handle, __uuidof(ID3D11Texture2D), m_shared[slot].putVoid());
            else
                hr = m_device5->OpenSharedFence(handle, __uuidof(ID3D11Fence), m_fence.putVoid());
            CloseHandle(handle);
            if (FAILED(hr)) return false;
        }
        m_openPid = pid;
        m_openGeneration = generation;
        return true;
    }

    // Copie la dernière image du jeu dans notre texture, si elle est nouvelle. `fresh` : une image a été copiée.
    int fetch(bool& fresh) {
        fresh = false;
        volatile VideoShareHeader* b = m_block;
        if (m_stuckSlot >= 0) {
            if (m_copyFence && m_copyFence->GetCompletedValue() < m_stuckValue) return kOk;
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&b->slotBusy[m_stuckSlot]), 0);
            m_stuckSlot = -1;
        }
        for (int attempt = 0; attempt < 3; ++attempt) {
            const uint64_t latest = b->latest;  // numéro et texture lus ensemble
            const uint64_t frame = VideoLatestFrame(latest);
            const int slot = VideoLatestSlot(latest);
            if (latest == 0 || slot >= kVideoSlots) return kOk;
            MemoryBarrier();
            const uint32_t generation = b->generation, pid = b->producerPid;
            const uint64_t luid = b->adapterLuid;
            if (frame == m_lastFrame && generation == m_openGeneration && pid == m_openPid) return kOk;
            if (luid != m_luid) return kOk;  // autre carte graphique : on garde l'image d'avant
            // Réservation de la texture, puis vérification qu'elle est toujours la dernière publiée : le
            // fournisseur n'écrit jamais ni la dernière, ni une texture réservée.
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&b->slotBusy[slot]), 1);
            if (b->latest != latest || b->generation != generation) {
                InterlockedExchange(reinterpret_cast<volatile LONG*>(&b->slotBusy[slot]), 0);
                continue;
            }
            // Ce qui accompagne l'image : protégé comme elle tant que la texture est réservée.
            const volatile VideoSlotInfo& info = b->slots[slot];
            const uint32_t rx = info.rectX, ry = info.rectY, rw = info.rectWidth, rh = info.rectHeight, linear = info.linear;
            const float tl = info.tanLeft, tr = info.tanRight, tu = info.tanUp, td = info.tanDown, cx = info.centerX, cy = info.centerY;
            bool ok = true;
            if (b->producerPid != pid) {  // relève entre deux fournisseurs : on réessaie
                InterlockedExchange(reinterpret_cast<volatile LONG*>(&b->slotBusy[slot]), 0);
                continue;
            }
            if (pid != m_openPid || generation != m_openGeneration) ok = openTextures(pid, generation);
            if (ok && m_fence && m_shared[slot]) {
                // Copie faite côté jeu (fence) : quelques millisecondes au plus, sinon l'image est laissée.
                ok = WaitFence(m_fence.get(), frame, m_fenceEvent, 30);
                if (ok) {
                    const int copied = copyToPrivate(m_shared[slot].get());
                    if (copied < 0) {
                        // Copie pas finie (carte graphique très en retard) : la texture reste réservée jusqu'à
                        // la fin de la copie, pour que le jeu ne la réécrive pas pendant.
                        m_stuckSlot = slot;
                        m_stuckValue = m_copyValue;
                        return kOk;
                    }
                    ok = copied > 0;
                }
            } else {
                ok = false;
            }
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&b->slotBusy[slot]), 0);
            if (!ok) return kOk;  // textures pas encore prêtes : nouvel essai au prochain instant
            m_lastFrame = frame;
            m_linear = linear != 0;
            m_rect[0] = rx;
            m_rect[1] = ry;
            m_rect[2] = rw;
            m_rect[3] = rh;
            m_fov[0] = tl;
            m_fov[1] = tr;
            m_fov[2] = tu;
            m_fov[3] = td;
            m_center[0] = cx;
            m_center[1] = cy;
            m_hasFrame = true;
            fresh = true;
            return kOk;
        }
        return kOk;
    }

    // Attend que `fence` atteigne `value` (au plus `ms` millisecondes) ; true si c'est fait.
    static bool WaitFence(ID3D11Fence* fence, uint64_t value, HANDLE event, DWORD ms) {
        const ULONGLONG deadline = GetTickCount64() + ms;
        while (fence->GetCompletedValue() < value) {
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline || FAILED(fence->SetEventOnCompletion(value, event))) return fence->GetCompletedValue() >= value;
            WaitForSingleObject(event, static_cast<DWORD>(deadline - now));
        }
        return true;
    }

    // 1 : copiée ; 0 : pas copiée ; -1 : copie lancée mais pas finie (la texture partagée ne doit pas être rendue).
    int copyToPrivate(ID3D11Texture2D* shared) {
        DeviceLock lock(m_multithread.get());
        D3D11_TEXTURE2D_DESC desc;
        shared->GetDesc(&desc);
        if (!m_private || desc.Width != m_privateWidth || desc.Height != m_privateHeight || desc.Format != m_privateFormat) {
            m_private.reset();
            m_privateView.reset();
            D3D11_TEXTURE2D_DESC own = desc;
            own.Format = TypelessOf(desc.Format);
            own.MipLevels = 1;
            own.ArraySize = 1;
            own.SampleDesc.Count = 1;
            own.Usage = D3D11_USAGE_DEFAULT;
            own.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            own.CPUAccessFlags = 0;
            own.MiscFlags = 0;
            m_hasFrame = false;  // l'ancienne image n'existe plus : rien à répéter tant que la copie n'est pas faite
            if (FAILED(m_device->CreateTexture2D(&own, nullptr, m_private.put()))) return 0;
            D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
            view.Format = ViewOf(desc.Format);
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
            if (FAILED(m_device->CreateShaderResourceView(m_private.get(), &view, m_privateView.put()))) {
                m_private.reset();
                return 0;
            }
            m_privateWidth = desc.Width;
            m_privateHeight = desc.Height;
            m_privateFormat = desc.Format;
        }
        m_context->CopyResource(m_private.get(), shared);
        // La texture partagée est rendue au jeu seulement une fois la copie faite par la carte graphique.
        const uint64_t value = ++m_copyValue;
        if (FAILED(m_context4->Signal(m_copyFence.get(), value))) return -1;
        m_context->Flush();
        lock.unlock();
        return WaitFence(m_copyFence.get(), value, m_copyEvent, 2000) ? 1 : -1;
    }

    // --- Encodage ---------------------------------------------------------------------------------------
    int setupWriter() {
        // Nom du fichier : jeu et date, dans le dossier choisi (créé au besoin).
        std::wstring game;
        for (int i = 0; i < 63 && m_block->game[i]; ++i) game += static_cast<wchar_t>(m_block->game[i]);
        for (wchar_t& c : game)
            if (c < 32 || std::wcschr(L"<>:\"/\\|?*", c)) c = L'_';
        if (game.empty()) game = L"VR";
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[64];
        std::swprintf(stamp, 64, L" %04u-%02u-%02u %02u-%02u-%02u", static_cast<unsigned>(t.wYear), static_cast<unsigned>(t.wMonth),
                      static_cast<unsigned>(t.wDay), static_cast<unsigned>(t.wHour), static_cast<unsigned>(t.wMinute), static_cast<unsigned>(t.wSecond));
        std::wstring folder = m_options.folder;
        while (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/')) folder.pop_back();
        if (folder.empty()) return fail(kNoFile, E_INVALIDARG);
        const int created = SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
        if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS && created != ERROR_FILE_EXISTS) return fail(kNoFile, HRESULT_FROM_WIN32(created));
        m_path = folder + L"\\" + game + stamp + L".mp4";
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            wcsncpy_s(m_status.file, m_path.c_str(), _TRUNCATE);
            wcsncpy_s(m_status.game, game.c_str(), _TRUNCATE);
            wcsncpy_s(m_status.encoder, EncoderName(m_options.codec).c_str(), _TRUNCATE);
        }

        UINT token = 0;
        HRESULT hr = MFCreateDXGIDeviceManager(&token, m_manager.put());
        if (SUCCEEDED(hr)) hr = m_manager->ResetDevice(m_device.get(), token);
        if (FAILED(hr)) return fail(kNoDevice, hr);
        Ptr<IMFAttributes> attributes;
        hr = MFCreateAttributes(attributes.put(), 4);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, m_manager.get());
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
        if (FAILED(hr)) return fail(kNoEncoder, hr, "attributs du fichier");
        hr = MFCreateSinkWriterFromURL(m_path.c_str(), nullptr, attributes.get(), m_writer.put());
        if (FAILED(hr)) return fail(kNoFile, hr, "creation du fichier");

        // Vidéo.
        const UINT64 size = (static_cast<UINT64>(m_width) << 32) | static_cast<UINT32>(m_height);
        const UINT64 rate = (static_cast<UINT64>(m_options.fps) << 32) | 1u;
        const UINT64 aspect = (1ull << 32) | 1u;
        Ptr<IMFMediaType> out, in;
        hr = MFCreateMediaType(out.put());
        if (SUCCEEDED(hr)) hr = out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = out->SetGUID(MF_MT_SUBTYPE, CodecSubtype(m_options.codec));
        if (SUCCEEDED(hr)) hr = out->SetUINT32(MF_MT_AVG_BITRATE, m_options.bitrateKbps * 1000u);
        if (SUCCEEDED(hr)) hr = out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = out->SetUINT64(MF_MT_FRAME_SIZE, size);
        if (SUCCEEDED(hr)) hr = out->SetUINT64(MF_MT_FRAME_RATE, rate);
        if (SUCCEEDED(hr)) hr = out->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, aspect);
        if (SUCCEEDED(hr) && m_options.codec == 0) hr = out->SetUINT32(MF_MT_MPEG2_PROFILE, 100);  // H.264 High
        if (SUCCEEDED(hr) && m_options.codec == 1) hr = out->SetUINT32(MF_MT_MPEG2_PROFILE, 1);    // HEVC Main
        if (SUCCEEDED(hr)) hr = m_writer->AddStream(out.get(), &m_videoStream);
        if (FAILED(hr)) return fail(kNoEncoder, hr, "type video de sortie");
        hr = MFCreateMediaType(in.put());
        if (SUCCEEDED(hr)) hr = in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (SUCCEEDED(hr)) hr = in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = in->SetUINT64(MF_MT_FRAME_SIZE, size);
        if (SUCCEEDED(hr)) hr = in->SetUINT64(MF_MT_FRAME_RATE, rate);
        if (SUCCEEDED(hr)) hr = in->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, aspect);
        if (SUCCEEDED(hr)) hr = in->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
        if (SUCCEEDED(hr)) hr = in->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
        if (SUCCEEDED(hr)) hr = in->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
        if (SUCCEEDED(hr)) hr = in->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
        Ptr<IMFAttributes> encoding;
        if (SUCCEEDED(hr)) hr = MFCreateAttributes(encoding.put(), 3);
        if (SUCCEEDED(hr)) {
            encoding->SetUINT32(kRateControl, eAVEncCommonRateControlMode_UnconstrainedVBR);
            encoding->SetUINT32(kMeanBitRate, m_options.bitrateKbps * 1000u);
            encoding->SetUINT32(kGopSize, m_options.fps * 2);  // une image clé toutes les 2 s : montage et reprise faciles
            hr = m_writer->SetInputMediaType(m_videoStream, in.get(), encoding.get());
        }
        if (FAILED(hr)) return fail(kNoEncoder, hr, "type video d'entree (encodeur)");

        // Son (toujours une piste, silencieuse si rien n'est enregistré : lecteurs et montage plus simples).
        Ptr<IMFMediaType> aacType, pcmType;
        hr = MFCreateMediaType(aacType.put());
        if (SUCCEEDED(hr)) hr = aacType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = aacType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        if (SUCCEEDED(hr)) hr = aacType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(hr)) hr = aacType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
        if (SUCCEEDED(hr)) hr = aacType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        if (SUCCEEDED(hr)) hr = aacType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);  // 192 kbit/s
        if (SUCCEEDED(hr)) hr = m_writer->AddStream(aacType.get(), &m_audioStream);
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(pcmType.put());
        if (SUCCEEDED(hr)) hr = pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
        if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioRate * 4);
        if (SUCCEEDED(hr)) hr = m_writer->SetInputMediaType(m_audioStream, pcmType.get(), nullptr);
        if (FAILED(hr)) return fail(kNoEncoder, hr, "son AAC");

        // Images NV12 réservées à l'encodeur : rendues à la réserve quand il n'en a plus besoin.
        Ptr<IMFAttributes> pool;
        hr = MFCreateVideoSampleAllocatorEx(__uuidof(IMFVideoSampleAllocatorEx), m_allocator.putVoid());
        if (SUCCEEDED(hr)) hr = m_allocator->SetDirectXManager(m_manager.get());
        if (SUCCEEDED(hr)) hr = MFCreateAttributes(pool.put(), 2);
        if (SUCCEEDED(hr)) hr = pool->SetUINT32(MF_SA_D3D11_BINDFLAGS, D3D11_BIND_RENDER_TARGET);
        if (SUCCEEDED(hr)) hr = pool->SetUINT32(MF_SA_D3D11_USAGE, D3D11_USAGE_DEFAULT);
        if (SUCCEEDED(hr)) hr = m_allocator->InitializeSampleAllocatorEx(4, 16, pool.get(), in.get());
        if (FAILED(hr)) return fail(kNoConversion, hr, "reserve d'images NV12");

        hr = m_writer->BeginWriting();
        if (FAILED(hr)) return fail(kNoEncoder, hr, "debut d'ecriture");
        m_writing = true;
        m_audioWritten = 0;
        m_videoSamples = 0;
        RecordLog("Enregistrement : %s %dx%d %u i/s %u kbit/s, oeil %u, encodeur \"%s\", image du jeu %ux%u (format DXGI %u), fichier \"%s\".",
                  kCodecNames[m_options.codec], m_width, m_height, m_options.fps, m_options.bitrateKbps, m_options.eye,
                  Utf8(EncoderName(m_options.codec)).c_str(), m_privateWidth, m_privateHeight, static_cast<unsigned>(m_privateFormat), Utf8(m_path).c_str());
        return kOk;
    }

    static bool DiskFull(HRESULT hr) { return hr == HRESULT_FROM_WIN32(ERROR_DISK_FULL) || hr == HRESULT_FROM_WIN32(ERROR_HANDLE_DISK_FULL); }

    static std::wstring EncoderName(uint32_t codec) {
        MFT_REGISTER_TYPE_INFO info = {MFMediaType_Video, CodecSubtype(codec)};
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        std::wstring name;
        if (SUCCEEDED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, nullptr, &info, &activates, &count))) {
            for (UINT32 i = 0; i < count; ++i) {
                if (i == 0) {
                    wchar_t* text = nullptr;
                    UINT32 length = 0;
                    if (SUCCEEDED(activates[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &text, &length)) && text) {
                        name = text;
                        CoTaskMemFree(text);
                    }
                }
                activates[i]->Release();
            }
            CoTaskMemFree(activates);
        }
        return name;
    }

    // Image NV12 de l'instant `time` (100 ns), à partir de la dernière image reçue.
    int writeVideo(int64_t time, int64_t duration, bool fresh) {
        if (!m_hasFrame || !m_privateView) return kOk;
        Ptr<IMFSample> sample;
        HRESULT hr = m_allocator->AllocateSample(sample.put());
        if (hr == MF_E_SAMPLEALLOCATOR_EMPTY) {  // encodeur en retard : instant sauté
            std::lock_guard<std::mutex> lock(m_statusMutex);
            ++m_status.dropped;
            return kOk;
        }
        if (FAILED(hr)) return fail(kNoConversion, hr);
        Ptr<IMFMediaBuffer> buffer;
        Ptr<IMFDXGIBuffer> dxgi;
        Ptr<ID3D11Texture2D> texture;
        UINT subresource = 0;
        hr = sample->GetBufferByIndex(0, buffer.put());
        if (SUCCEEDED(hr)) hr = buffer->QueryInterface(__uuidof(IMFDXGIBuffer), dxgi.putVoid());
        if (SUCCEEDED(hr)) hr = dxgi->GetResource(__uuidof(ID3D11Texture2D), texture.putVoid());
        if (SUCCEEDED(hr)) hr = dxgi->GetSubresourceIndex(&subresource);
        if (FAILED(hr)) return fail(kNoConversion, hr);
        if (!convert(texture.get(), subresource)) return fail(kNoConversion, E_FAIL);
        sample->SetSampleTime(time);
        sample->SetSampleDuration(duration);
        hr = m_writer->WriteSample(m_videoStream, sample.get());
        if (FAILED(hr)) return fail(DiskFull(hr) ? kWriteFailed : kVideoRejected, hr, "image video");
        ++m_videoSamples;
        std::lock_guard<std::mutex> lock(m_statusMutex);
        ++m_status.frames;
        if (!fresh) ++m_status.repeated;
        return kOk;
    }

    bool convert(ID3D11Texture2D* target, UINT subresource) {
        DeviceLock lock(m_multithread.get());
        D3D11_TEXTURE2D_DESC desc;
        target->GetDesc(&desc);
        Ptr<ID3D11RenderTargetView> luma, chroma;
        D3D11_RENDER_TARGET_VIEW_DESC rtv = {};
        if (desc.ArraySize > 1) {
            rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rtv.Texture2DArray.FirstArraySlice = subresource;
            rtv.Texture2DArray.ArraySize = 1;
        } else {
            rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        }
        rtv.Format = DXGI_FORMAT_R8_UNORM;
        if (FAILED(m_device->CreateRenderTargetView(target, &rtv, luma.put()))) return false;
        rtv.Format = DXGI_FORMAT_R8G8_UNORM;
        if (FAILED(m_device->CreateRenderTargetView(target, &rtv, chroma.put()))) return false;

        // Cadre dans la partie qui contient l'œil, puis ramené aux coordonnées de toute la texture.
        const uint32_t rw = m_rect[2] ? m_rect[2] : m_privateWidth, rh = m_rect[3] ? m_rect[3] : m_privateHeight;
        CropBox box = ComputeCrop(static_cast<int>(rw), static_cast<int>(rh), m_fov[0], m_fov[1], m_fov[2], m_fov[3], m_center[0], m_center[1],
                                  static_cast<float>(m_width) / static_cast<float>(m_height), m_options.field);
        const float tw = static_cast<float>(m_privateWidth), th = static_cast<float>(m_privateHeight);
        box.x = (static_cast<float>(m_rect[0]) + box.x * static_cast<float>(rw)) / tw;
        box.y = (static_cast<float>(m_rect[1]) + box.y * static_cast<float>(rh)) / th;
        box.w = box.w * static_cast<float>(rw) / tw;
        box.h = box.h * static_cast<float>(rh) / th;
        ID3D11DeviceContext* c = m_context.get();
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->IASetInputLayout(nullptr);
        c->VSSetShader(m_vs.get(), nullptr, 0);
        ID3D11ShaderResourceView* views[] = {m_privateView.get()};
        ID3D11SamplerState* samplers[] = {m_sampler.get()};
        ID3D11Buffer* buffers[] = {m_params.get()};
        c->PSSetShaderResources(0, 1, views);
        c->PSSetSamplers(0, 1, samplers);
        c->PSSetConstantBuffers(0, 1, buffers);
        c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        c->RSSetState(nullptr);
        for (int pass = 0; pass < 2; ++pass) {
            const int w = pass == 0 ? m_width : m_width / 2, h = pass == 0 ? m_height : m_height / 2;
            // Quatre échantillons par pixel de sortie, à un quart de pixel du centre : moyenne d'une zone
            // d'environ deux pixels, assez pour réduire une image d'œil de 3 ou 4 K sans scintillement.
            ShaderParams p = {{box.x, box.y, box.w, box.h}, {box.w / static_cast<float>(w) * 0.25f, box.h / static_cast<float>(h) * 0.25f, m_linear ? 1.f : 0.f, 0.f}};
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (FAILED(c->Map(m_params.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
            std::memcpy(mapped.pData, &p, sizeof(p));
            c->Unmap(m_params.get(), 0);
            ID3D11RenderTargetView* target2[] = {pass == 0 ? luma.get() : chroma.get()};
            c->OMSetRenderTargets(1, target2, nullptr);
            D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f};
            c->RSSetViewports(1, &viewport);
            c->PSSetShader(pass == 0 ? m_psY.get() : m_psUV.get(), nullptr, 0);
            c->Draw(3, 0);
        }
        ID3D11RenderTargetView* none[] = {nullptr};
        c->OMSetRenderTargets(1, none, nullptr);
        ID3D11ShaderResourceView* noView[] = {nullptr};
        c->PSSetShaderResources(0, 1, noView);
        c->Flush();
        return true;
    }

    // Son mélangé jusqu'à la position `until` (100 ns depuis le début), par paquets de 20 ms au plus.
    int writeAudio(int64_t until) {
        if (!m_writing) return kOk;
        const int64_t target = until > 0 ? until * kAudioRate / 10'000'000 : 0;
        while (m_audioWritten < target) {
            const int64_t frames = std::min<int64_t>(target - m_audioWritten, kAudioRate / 50);
            m_mix.assign(static_cast<size_t>(frames * 2), 0.f);
            if (m_gameSound) m_gameSound->mixInto(m_audioWritten, m_mix.data(), frames);
            if (m_micSound) m_micSound->mixInto(m_audioWritten, m_mix.data(), frames);
            Ptr<IMFMediaBuffer> buffer;
            Ptr<IMFSample> sample;
            const DWORD bytes = static_cast<DWORD>(frames * 4);
            HRESULT hr = MFCreateMemoryBuffer(bytes, buffer.put());
            BYTE* data = nullptr;
            if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
            if (SUCCEEDED(hr)) {
                int16_t* pcm = reinterpret_cast<int16_t*>(data);
                for (int64_t i = 0; i < frames * 2; ++i) pcm[i] = ToPcm16(m_mix[static_cast<size_t>(i)]);
                buffer->Unlock();
                hr = buffer->SetCurrentLength(bytes);
            }
            if (SUCCEEDED(hr)) hr = MFCreateSample(sample.put());
            if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.get());
            if (SUCCEEDED(hr)) hr = sample->SetSampleTime(m_audioWritten * 10'000'000 / kAudioRate);
            if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(frames * 10'000'000 / kAudioRate);
            if (SUCCEEDED(hr)) hr = m_writer->WriteSample(m_audioStream, sample.get());
            if (FAILED(hr)) return fail(DiskFull(hr) ? kWriteFailed : kAudioRejected, hr, "son");
            m_audioWritten += frames;
        }
        return kOk;
    }

    XrprOptions m_options = {};
    volatile VideoShareHeader* m_block = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<int32_t> m_state{kStopped};
    std::mutex m_statusMutex;
    XrprStatus m_status = {};

    uint32_t m_pid = 0;
    Ptr<ID3D11Device> m_device;
    Ptr<ID3D11Device1> m_device1;
    Ptr<ID3D11Device5> m_device5;
    Ptr<ID3D11DeviceContext> m_context;
    Ptr<ID3D11DeviceContext4> m_context4;
    Ptr<ID3D11Multithread> m_multithread;
    Ptr<ID3D11Fence> m_copyFence;  // fin de la copie vers notre texture
    uint64_t m_copyValue = 0;
    HANDLE m_fenceEvent = nullptr;  // un événement par fence : une attente expirée ne réveille pas la suivante
    HANDLE m_copyEvent = nullptr;
    uint32_t m_rect[4] = {0, 0, 0, 0};  // partie de la texture qui contient l'œil
    uint64_t m_videoSamples = 0;
    int m_stuckSlot = -1;  // texture partagée encore réservée (copie pas finie)
    uint64_t m_stuckValue = 0;
    Ptr<ID3D12Device> m_d3d12;
    Ptr<ID3D11VertexShader> m_vs;
    Ptr<ID3D11PixelShader> m_psY, m_psUV;
    Ptr<ID3D11Buffer> m_params;
    Ptr<ID3D11SamplerState> m_sampler;
    int m_width = 0, m_height = 0;

    Ptr<ID3D11Texture2D> m_shared[kVideoSlots];
    Ptr<ID3D11Fence> m_fence;
    uint32_t m_openPid = 0, m_openGeneration = 0;
    uint64_t m_luid = 0;
    uint64_t m_lastFrame = 0;
    Ptr<ID3D11Texture2D> m_private;
    Ptr<ID3D11ShaderResourceView> m_privateView;
    UINT m_privateWidth = 0, m_privateHeight = 0;
    DXGI_FORMAT m_privateFormat = DXGI_FORMAT_UNKNOWN;
    bool m_hasFrame = false;
    bool m_linear = false;
    float m_fov[4] = {-1.f, 1.f, 1.f, -1.f};
    float m_center[2] = {0.f, 0.f};

    std::wstring m_path;
    Ptr<IMFDXGIDeviceManager> m_manager;
    Ptr<IMFSinkWriter> m_writer;
    Ptr<IMFVideoSampleAllocatorEx> m_allocator;
    DWORD m_videoStream = 0, m_audioStream = 0;
    bool m_writing = false;
    int64_t m_origin = 0;
    int64_t m_videoEnd = 0;
    int64_t m_audioWritten = 0;
    std::vector<float> m_mix;
    std::unique_ptr<AudioSource> m_gameSound, m_micSound;
};

Recorder g_recorder;

}  // namespace

extern "C" {

// Codecs dont un encodeur matériel est présent : 1 = H.264, 2 = H.265 (HEVC), 4 = AV1.
__declspec(dllexport) int XrprProbe() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int mask = 0;
    if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        for (uint32_t codec = 0; codec < 3; ++codec) {
            MFT_REGISTER_TYPE_INFO info = {MFMediaType_Video, CodecSubtype(codec)};
            IMFActivate** activates = nullptr;
            UINT32 count = 0;
            if (SUCCEEDED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, nullptr, &info, &activates, &count))) {
                if (count > 0) mask |= 1 << codec;
                for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
                CoTaskMemFree(activates);
            }
        }
        MFShutdown();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return mask;
}

__declspec(dllexport) int XrprStart(void* share, uint64_t shareBytes, const XrprOptions* options) {
    if (!options) return kBadOptions;
    return g_recorder.start(share, shareBytes, *options);
}

__declspec(dllexport) void XrprStop() { g_recorder.stop(); }

__declspec(dllexport) void XrprGetStatus(XrprStatus* status) {
    if (status) g_recorder.status(*status);
}

// Sorties (capture = 0) ou entrées (capture = 1) audio actives : lignes « identifiant<TAB>nom ». Retourne
// le nombre de caractères écrits (texte terminé par un zéro), ou la taille nécessaire si `chars` est trop petit.
__declspec(dllexport) int XrprListDevices(int capture, wchar_t* buffer, int chars) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::wstring text;
    Ptr<IMMDeviceEnumerator> enumerator;
    Ptr<IMMDeviceCollection> devices;
    if (SUCCEEDED(CoCreateInstance(kMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIMMDeviceEnumerator, enumerator.putVoid())) &&
        SUCCEEDED(enumerator->EnumAudioEndpoints(capture ? eCapture : eRender, DEVICE_STATE_ACTIVE, devices.put()))) {
        UINT count = 0;
        devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            Ptr<IMMDevice> device;
            Ptr<IPropertyStore> store;
            wchar_t* id = nullptr;
            if (FAILED(devices->Item(i, device.put())) || FAILED(device->GetId(&id)) || !id) continue;
            std::wstring name;
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, store.put())) && SUCCEEDED(store->GetValue(kFriendlyName, &value)) && value.vt == VT_LPWSTR)
                name = value.pwszVal;
            PropVariantClear(&value);
            for (wchar_t& c : name)
                if (c == L'\t' || c == L'\n') c = L' ';
            text += id;
            text += L'\t';
            text += name.empty() ? std::wstring(id) : name;
            text += L'\n';
            CoTaskMemFree(id);
        }
    }
    if (SUCCEEDED(co)) CoUninitialize();
    const int needed = static_cast<int>(text.size()) + 1;
    if (!buffer || chars < needed) return needed;
    std::memcpy(buffer, text.c_str(), static_cast<size_t>(needed) * sizeof(wchar_t));
    return needed;
}

}  // extern "C"
