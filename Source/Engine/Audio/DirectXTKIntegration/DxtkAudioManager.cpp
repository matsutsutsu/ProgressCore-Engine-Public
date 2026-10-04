#include "DxtkAudioManager.h"

#include <Audio.h> // DirectXTK for Audio（DirectXTK.lib に実装が入っている / xaudio2.lib はリンク済み）

#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Math/Interpolation.h" // Damp(): フレームレート非依存の追従係数
#include "Engine/Platform/Core/FileSystem.h"

#include <json.hpp>
#include <windows.h> // MultiByteToWideChar

// Media Foundation: mp3/wma/aac 等をロード時に PCM へデコードするために使う（Windows 標準）
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Windows SDK 標準のライブラリなので、プロジェクト設定ではなくここでリンクする
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

using namespace DirectX;

namespace
{
    // スロー最大時のピッチ低下量（オクターブ単位。DXTK の SetPitch は [-1,1] = ±1 オクターブ）。
    // slow=1 で 2^-0.8 ≒ 0.57 倍の再生速度になる
    constexpr float kSlowMaxPitch = 0.8f;

    // スロー度を目標へ寄せる速さ（1 秒あたり）。Damp(rate, dt) 経由で使う。
    //   「呼ばれるたびに一定割合寄る」係数にすると、呼び出し回数で追従の速さが変わる。
    //   1 秒あたりの割合で持てばフレームレートに依存しない。
    //   17.26 = -60 * ln(1 - 0.25) で、60Hz で毎回 0.25 ずつ寄るのと同じ速さ
    constexpr float kSlowSmoothRate = 17.26f;

    // スロー中にリバーブを掛け始めるしきい値
    constexpr float kReverbSlowThreshold = 0.15f;

    // UTF-8 → ワイド文字列（SoundEffect が wchar_t* を要求するため）
    std::wstring ToWide(const std::string &s)
    {
        if (s.empty()) return std::wstring();
        const int    len = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(static_cast<size_t>(len), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
        return w;
    }

    float Clamp(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

    // パスの拡張子を小文字で返す（例: ".wav" / ".mp3"）
    std::string LowerExt(const std::string &path)
    {
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return std::string();
        std::string ext = path.substr(dot);
        for (char &c : ext)
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        return ext;
    }

    /**
     * @brief Media Foundation で圧縮音声（mp3/wma/aac 等）を PCM へデコードし、
     *        DXTK の SoundEffect にそのまま渡せる [WAVEFORMATEX][PCM...] の単一バッファに詰める。
     * @param outBuffer デコード結果（先頭に WAVEFORMATEX、続けて PCM 本体）。後で SoundEffect が所有する
     * @param outWfx    outBuffer の先頭を指すフォーマット
     * @param outStart  PCM 本体の開始位置
     * @param outBytes  PCM 本体の長さ
     */
    bool DecodeAudioFileToPcm(const std::wstring         &path,
                              std::unique_ptr<uint8_t[]> &outBuffer, WAVEFORMATEX **outWfx,
                              const uint8_t **outStart, size_t *outBytes)
    {
        using Microsoft::WRL::ComPtr;

        ComPtr<IMFSourceReader> reader;
        if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, reader.GetAddressOf())))
            return false;

        // 最初の音声ストリームだけを選択する
        reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

        // 出力を PCM に指定する（必要なデコーダは MF が自動で挿す）
        ComPtr<IMFMediaType> want;
        if (FAILED(MFCreateMediaType(want.GetAddressOf()))) return false;
        want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want.Get())))
            return false;

        // 実際に決まった出力フォーマットを WAVEFORMATEX として取得する
        ComPtr<IMFMediaType> actual;
        if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, actual.GetAddressOf())))
            return false;

        WAVEFORMATEX *wfx     = nullptr;
        UINT32        wfxSize = 0;
        if (FAILED(MFCreateWaveFormatExFromMFMediaType(actual.Get(), &wfx, &wfxSize)))
            return false;

        // 全サンプルを読み出して PCM を連結する
        std::vector<uint8_t> pcm;
        for (;;)
        {
            DWORD             flags = 0;
            ComPtr<IMFSample> sample;
            if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr,
                                          sample.GetAddressOf())))
            {
                CoTaskMemFree(wfx);
                return false;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (!sample) continue; // 一時的に空のサンプルはスキップ

            ComPtr<IMFMediaBuffer> buf;
            if (FAILED(sample->ConvertToContiguousBuffer(buf.GetAddressOf()))) continue;

            BYTE *data = nullptr;
            DWORD len  = 0;
            if (SUCCEEDED(buf->Lock(&data, nullptr, &len)))
            {
                pcm.insert(pcm.end(), data, data + len);
                buf->Unlock();
            }
        }

        if (pcm.empty())
        {
            CoTaskMemFree(wfx);
            return false;
        }

        // [WAVEFORMATEX][PCM...] を単一バッファへ詰める。DXTK がこのバッファごと所有する
        const size_t total = static_cast<size_t>(wfxSize) + pcm.size();
        outBuffer          = std::make_unique<uint8_t[]>(total);
        std::memcpy(outBuffer.get(), wfx, wfxSize);
        std::memcpy(outBuffer.get() + wfxSize, pcm.data(), pcm.size());
        *outWfx   = reinterpret_cast<WAVEFORMATEX *>(outBuffer.get());
        *outStart = outBuffer.get() + wfxSize;
        *outBytes = pcm.size();

        CoTaskMemFree(wfx);
        return true;
    }
} // namespace

// =============================================================================
// Impl（XAudio2 に依存する状態はここに隠す）
// =============================================================================
struct DxtkAudioManager::Impl
{
    // 継続再生、または 3D ワンショットの 1 インスタンス
    struct Playing
    {
        std::unique_ptr<SoundEffectInstance> inst;
        AudioEmitter                         emitter;          // 3D 用（is3D=false のときは未使用）
        float                                userPitch = 0.0f; // SetEventParameter("pitch") で設定したピッチ（スロー分は別途加算）
        float                                userVolume = 1.0f; // SetEventParameter("volume") などで設定した音量（種類の音量は別途掛ける）
        bool                                 is3D      = false;
        bool                                 isBgm     = false; // BGM か（種類の音量を掛け直すときに使う）
    };

    std::unique_ptr<AudioEngine> engine;
    bool                         ready     = false;
    bool                         mfStarted = false; // Media Foundation 初期化済み（mp3等のデコードに必要）

    // イベント名のハッシュ → ロード済みの音源
    std::unordered_map<uint32_t, std::unique_ptr<SoundEffect>> sounds;

    // ロードに成功したイベント名（インスペクタのドロップダウン用。読み込み順を保つため vector）
    std::vector<std::string> eventNames;

    // 継続再生（PlayEvent / PlayEvent3D）: playingId → インスタンス
    std::unordered_map<uint32_t, Playing> playing;
    uint32_t                              nextId = 1;

    // 3D ワンショット（PlayOneShot3D）: 再生が終わったら Update で回収する一時インスタンス
    std::vector<Playing> oneShot3D;

    AudioListener listener;

    // スロー演出
    float slow       = 0.0f; // 現在値（補間後）
    float slowTarget = 0.0f; // 目標値
    bool  reverbOn   = false;

    // 種類ごとの音量の倍率と、BGM のイベント（名前が "bgm:" で始まるもの）のハッシュ
    float                        bgmVolume = 1.0f;
    float                        sfxVolume = 1.0f;
    std::unordered_set<uint32_t> bgmHashes;

    std::mutex mtx; // sounds/playing/oneShot3D を保護（AudioSystem が並列に呼ぶため）

    // スロー分を足した実効ピッチを 1 インスタンスへ適用する
    void ApplyPitch(Playing &p, float slowOffset) const
    {
        if (p.inst) p.inst->SetPitch(Clamp(p.userPitch + slowOffset, -1.0f, 1.0f));
    }

    // そのイベントの種類の音量の倍率
    float CategoryVolume(uint32_t hash) const { return bgmHashes.count(hash) ? bgmVolume : sfxVolume; }

    // ハッシュから SoundEffect を引く（無ければ nullptr）
    SoundEffect *Find(uint32_t hash)
    {
        auto it = sounds.find(hash);
        return (it != sounds.end()) ? it->second.get() : nullptr;
    }
};

// =============================================================================
// ライフサイクル
// =============================================================================
DxtkAudioManager::DxtkAudioManager()
    : m_impl(std::make_unique<Impl>())
{
}

DxtkAudioManager::~DxtkAudioManager()
{
    // 再生インスタンスは AudioEngine に依存するので、先に破棄する
    if (m_impl)
    {
        {
            std::lock_guard<std::mutex> lk(m_impl->mtx);
            m_impl->playing.clear();
            m_impl->oneShot3D.clear();
            m_impl->sounds.clear();
        }
        if (m_impl->mfStarted)
            MFShutdown();
    }
}

void DxtkAudioManager::Initialize()
{
    AUDIO_ENGINE_FLAGS flags = AudioEngine_EnvironmentalReverb | AudioEngine_ReverbUseFilters;
#ifdef _DEBUG
    flags |= AudioEngine_Debug;
#endif

    try
    {
        m_impl->engine = std::make_unique<AudioEngine>(flags);
        m_impl->ready  = true;
        CCL_LOG_INFO(LogCategory::Core, "[Audio] DirectXTK Audio 初期化完了%s",
                     m_impl->engine->IsAudioDevicePresent() ? "" : "（音声デバイスなし→サイレント動作）");
    }
    catch (const std::exception &ex)
    {
        m_impl->ready = false;
        CCL_LOG_ERROR(LogCategory::Core, "[Audio] DirectXTK Audio 初期化失敗: %s", ex.what());
    }

    // Media Foundation を起動する。失敗しても WAV は再生でき、mp3 等がスキップされるだけなので続行する
    if (SUCCEEDED(MFStartup(MF_VERSION)))
    {
        m_impl->mfStarted = true;
    }
    else
    {
        CCL_LOG_WARN(LogCategory::Core,
                     "[Audio] Media Foundation 初期化失敗。mp3等のデコードは無効（WAVのみ再生可）");
    }
}

void DxtkAudioManager::Update(float dt)
{
    if (!m_impl->ready || !m_impl->engine) return;

    // スロー度を目標へ滑らかに寄せる。フレーム時間に依存しないよう係数は Damp() で求める
    m_impl->slow += (m_impl->slowTarget - m_impl->slow) * CCL::Math::Damp(kSlowSmoothRate, dt);
    const float slowOffset = -kSlowMaxPitch * m_impl->slow;

    {
        std::lock_guard<std::mutex> lk(m_impl->mtx);

        // 継続再生: スロー分のピッチを反映する
        for (auto &kv : m_impl->playing)
            m_impl->ApplyPitch(kv.second, slowOffset);

        // 3D ワンショット: 終了したものを末尾と入れ替えて削除し、残りにピッチを反映する
        for (size_t i = 0; i < m_impl->oneShot3D.size();)
        {
            auto &p = m_impl->oneShot3D[i];
            if (!p.inst || p.inst->GetState() == SoundState::STOPPED)
            {
                m_impl->oneShot3D[i] = std::move(m_impl->oneShot3D.back());
                m_impl->oneShot3D.pop_back();
            }
            else
            {
                m_impl->ApplyPitch(p, slowOffset);
                ++i;
            }
        }
    }

    // スロー中はリバーブを掛ける（3D 音にだけ効く）。切り替わったときだけ設定する
    const bool wantReverb = (m_impl->slow > kReverbSlowThreshold);
    if (wantReverb != m_impl->reverbOn)
    {
        m_impl->engine->SetReverb(wantReverb ? Reverb_Cave : Reverb_Off);
        m_impl->reverbOn = wantReverb;
    }

    // XAudio2 の毎フレーム処理。デバイスロスト等の致命的エラーは Reset で復帰を試みる
    if (!m_impl->engine->Update())
    {
        if (m_impl->engine->IsCriticalError())
            m_impl->engine->Reset();
    }
}

// =============================================================================
// データロード
// =============================================================================
void DxtkAudioManager::LoadConfig(const std::string &configFilePath)
{
    if (!m_impl->ready || !m_impl->engine) return;

    const std::string jsonStr = FileSystem::ReadAllText(configFilePath);
    if (jsonStr.empty())
    {
        CCL_LOG_WARN(LogCategory::Core, "[Audio] 設定ファイルが空か存在しません: %s", configFilePath.c_str());
        return;
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(jsonStr);
    }
    catch (const std::exception &ex)
    {
        CCL_LOG_ERROR(LogCategory::Core, "[Audio] AudioConfig の JSON parse 失敗 (%s): %s",
                      configFilePath.c_str(), ex.what());
        return;
    }

    if (!root.contains("sounds") || !root["sounds"].is_array())
    {
        CCL_LOG_WARN(LogCategory::Core, "[Audio] AudioConfig に \"sounds\" 配列がありません: %s", configFilePath.c_str());
        return;
    }

    int loaded = 0, skipped = 0;
    for (const auto &e : root["sounds"])
    {
        const std::string event = e.value("event", std::string{});
        const std::string file  = e.value("file", std::string{});
        if (event.empty() || file.empty()) continue;

        try
        {
            std::unique_ptr<SoundEffect> sfx;
            const std::string            ext = LowerExt(file);

            if (ext == ".wav")
            {
                // WAV は DXTK が直接読める
                sfx = std::make_unique<SoundEffect>(m_impl->engine.get(), ToWide(file).c_str());
            }
            else if (m_impl->mfStarted)
            {
                // mp3/wma/aac 等は Media Foundation で PCM にデコードしてから渡す
                std::unique_ptr<uint8_t[]> buf;
                WAVEFORMATEX              *wfx   = nullptr;
                const uint8_t             *start = nullptr;
                size_t                     bytes = 0;
                if (!DecodeAudioFileToPcm(ToWide(file), buf, &wfx, &start, &bytes))
                    throw std::runtime_error("Media Foundation デコード失敗");
                // buf（[WAVEFORMATEX][PCM]）の所有権は SoundEffect へ移る
                sfx = std::make_unique<SoundEffect>(m_impl->engine.get(), buf, wfx, start, bytes);
            }
            else
            {
                throw std::runtime_error("非WAVの再生には Media Foundation が必要（初期化失敗）");
            }

            const uint32_t hash  = CCL::Utils::HashString(event.c_str());
            m_impl->sounds[hash] = std::move(sfx);
            if (event.rfind("bgm:", 0) == 0) m_impl->bgmHashes.insert(hash);
            m_impl->eventNames.push_back(event);
            ++loaded;
        }
        catch (const std::exception &ex)
        {
            // ファイルが無い・壊れている等。ここで止めず、その音だけ無しで続行する
            ++skipped;
            (void)ex; // Shipping で WARN が引数を評価しない構成でも未使用警告を出さない
            CCL_LOG_WARN(LogCategory::Core, "[Audio] 読み込み失敗（スキップ）: %s (%s) : %s",
                         event.c_str(), file.c_str(), ex.what());
        }
    }
    CCL_LOG_INFO(LogCategory::Core, "[Audio] サウンド読み込み: 成功%d / スキップ%d", loaded, skipped);
}


// =============================================================================
// ワンショット
// =============================================================================
void DxtkAudioManager::PlayOneShot(uint32_t eventHash)
{
    if (!m_impl->ready) return;
    std::lock_guard<std::mutex> lk(m_impl->mtx);
    if (SoundEffect *sfx = m_impl->Find(eventHash))
        sfx->Play(m_impl->CategoryVolume(eventHash), 0.0f, 0.0f); // 2D ワンショットは DXTK がボイスをプールして自動で回収する
}

void DxtkAudioManager::PlayOneShot3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position, float volume, float pitch)
{
    if (!m_impl->ready) return;
    std::lock_guard<std::mutex> lk(m_impl->mtx);

    SoundEffect *sfx = m_impl->Find(eventHash);
    if (!sfx) return;

    Impl::Playing p;
    p.is3D      = true;
    p.userPitch = Clamp(pitch, -1.0f, 1.0f); // Update() でもスロー分と足して掛け直すので、ここで持たせる
    p.inst      = sfx->CreateInstance(SoundEffectInstance_Use3D);
    if (!p.inst) return;

    p.emitter.SetPosition(position);
    p.inst->Apply3D(m_impl->listener, p.emitter, /*rhcoords*/ false);
    p.userVolume = (std::max)(volume, 0.0f);
    p.isBgm      = m_impl->bgmHashes.count(eventHash) != 0;
    p.inst->SetVolume(p.userVolume * m_impl->CategoryVolume(eventHash));
    m_impl->ApplyPitch(p, -kSlowMaxPitch * m_impl->slow);
    p.inst->Play(false); // ループしない。終了後は Update() で回収する
    m_impl->oneShot3D.push_back(std::move(p));
}

// =============================================================================
// 継続再生と制御
// =============================================================================
uint32_t DxtkAudioManager::PlayEvent(uint32_t eventHash)
{
    if (!m_impl->ready) return 0;
    std::lock_guard<std::mutex> lk(m_impl->mtx);

    SoundEffect *sfx = m_impl->Find(eventHash);
    if (!sfx) return 0;

    Impl::Playing p;
    p.inst = sfx->CreateInstance();
    if (!p.inst) return 0;

    p.isBgm = m_impl->bgmHashes.count(eventHash) != 0;
    p.inst->SetVolume(m_impl->CategoryVolume(eventHash));
    m_impl->ApplyPitch(p, -kSlowMaxPitch * m_impl->slow);
    p.inst->Play(true); // BGM・環境音はループさせる

    const uint32_t id = m_impl->nextId++;
    m_impl->playing.emplace(id, std::move(p));
    return id;
}

uint32_t DxtkAudioManager::PlayEvent3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position)
{
    if (!m_impl->ready) return 0;
    std::lock_guard<std::mutex> lk(m_impl->mtx);

    SoundEffect *sfx = m_impl->Find(eventHash);
    if (!sfx) return 0;

    Impl::Playing p;
    p.is3D = true;
    p.inst = sfx->CreateInstance(SoundEffectInstance_Use3D);
    if (!p.inst) return 0;

    p.emitter.SetPosition(position);
    p.inst->Apply3D(m_impl->listener, p.emitter, false);
    p.isBgm = m_impl->bgmHashes.count(eventHash) != 0;
    p.inst->SetVolume(m_impl->CategoryVolume(eventHash));
    m_impl->ApplyPitch(p, -kSlowMaxPitch * m_impl->slow);
    p.inst->Play(true);

    const uint32_t id = m_impl->nextId++;
    m_impl->playing.emplace(id, std::move(p));
    return id;
}

void DxtkAudioManager::StopEvent(uint32_t playingId, bool allowFadeOut)
{
    std::lock_guard<std::mutex> lk(m_impl->mtx);
    auto                        it = m_impl->playing.find(playingId);
    if (it == m_impl->playing.end()) return;
    if (it->second.inst) it->second.inst->Stop(/*immediate*/ !allowFadeOut);
    m_impl->playing.erase(it);
}

void DxtkAudioManager::StopAll()
{
    std::lock_guard<std::mutex> lk(m_impl->mtx);

    // 継続再生（PlayEvent / PlayEvent3D）を即時停止して破棄する
    for (auto &kv : m_impl->playing)
        if (kv.second.inst) kv.second.inst->Stop(/*immediate*/ true);
    m_impl->playing.clear();

    // 再生途中の 3D ワンショットも即時停止して破棄する
    for (auto &p : m_impl->oneShot3D)
        if (p.inst) p.inst->Stop(/*immediate*/ true);
    m_impl->oneShot3D.clear();
}

void DxtkAudioManager::SetEventParameter(uint32_t playingId, const char *paramName, float value)
{
    if (!paramName) return;
    std::lock_guard<std::mutex> lk(m_impl->mtx);
    auto                        it = m_impl->playing.find(playingId);
    if (it == m_impl->playing.end() || !it->second.inst) return;

    // DXTK は任意のパラメータを持たないので、volume / pitch / pan の 3 つだけを解釈する
    if (std::strcmp(paramName, "volume") == 0)
    {
        it->second.userVolume = value;
        it->second.inst->SetVolume(value * (it->second.isBgm ? m_impl->bgmVolume : m_impl->sfxVolume));
    }
    else if (std::strcmp(paramName, "pitch") == 0)
        it->second.userPitch = Clamp(value, -1.0f, 1.0f); // 実際の適用は Update() でスロー分と足してから
    else if (std::strcmp(paramName, "pan") == 0)
        it->second.inst->SetPan(value);
}

void DxtkAudioManager::SetCategoryVolume(AudioCategory category, float volume01)
{
    std::lock_guard<std::mutex> lk(m_impl->mtx);
    const float v = Clamp(volume01, 0.0f, 1.0f);
    if (category == AudioCategory::BGM)
        m_impl->bgmVolume = v;
    else
        m_impl->sfxVolume = v;

    // 再生中の音にもすぐ効かせる（設定画面で動かしながら聞き比べられるように）
    auto apply = [&](Impl::Playing &p) {
        if (p.inst) p.inst->SetVolume(p.userVolume * (p.isBgm ? m_impl->bgmVolume : m_impl->sfxVolume));
    };
    for (auto &kv : m_impl->playing) apply(kv.second);
    for (auto &p : m_impl->oneShot3D) apply(p);
}

void DxtkAudioManager::SetEvent3DAttributes(uint32_t playingId, const DirectX::XMFLOAT3 &pos)
{
    std::lock_guard<std::mutex> lk(m_impl->mtx);
    auto                        it = m_impl->playing.find(playingId);
    if (it == m_impl->playing.end() || !it->second.inst || !it->second.is3D) return;
    it->second.emitter.SetPosition(pos);
    it->second.inst->Apply3D(m_impl->listener, it->second.emitter, false);
}

// =============================================================================
// 3D リスナー
// =============================================================================
void DxtkAudioManager::SetListenerPosition(const DirectX::XMFLOAT3 &pos,
                                           const DirectX::XMFLOAT3 &forward, const DirectX::XMFLOAT3 &up)
{
    std::lock_guard<std::mutex> lk(m_impl->mtx);
    m_impl->listener.SetPosition(pos);
    m_impl->listener.SetOrientation(forward, up);
}

// =============================================================================
// スロー演出
// =============================================================================
void DxtkAudioManager::SetGlobalSlow(float slow01)
{
    m_impl->slowTarget = Clamp(slow01, 0.0f, 1.0f);
}

std::vector<std::string> DxtkAudioManager::GetEventNames() const
{
    // ロックせずにコピーを返す。呼ぶのはエディタのメインスレッドだけで、ロード後は変化しないため
    return m_impl->eventNames;
}
