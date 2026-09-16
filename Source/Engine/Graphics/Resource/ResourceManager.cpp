#include "ResourceManager.h"
#include <filesystem>
#include <fstream>
#include <cereal/archives/json.hpp>

#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/DebugName.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"

#include "Engine/Platform/Core/FileSystem.h" // ユーザー指定のパス
#include <sstream>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::Utils;

namespace
{
    // アセットのキャッシュキー用に、パス文字列の表記ゆれ(区切り文字・大文字小文字)を吸収する。
    // Windowsのファイルアクセス自体は大小無視・スラッシュ混在OKだが、HashStringは
    // バイト単位の完全一致でしか同じ値を返さない。Sequencer(保存)とAnimGraph(参照)が
    // それぞれ別のダイアログ操作で得た文字列は、見た目が同じファイルでも
    // "Assets/Anims/Attack.json" と "Assets\Anims\Attack.json" のようにズレることがあり、
    // これがキャッシュ更新を素通りさせて「保存しても反映されない」不具合の主因になる。
    // ※ 実際のファイル読み込み(FileSystem::ReadAllText等)には元の filename をそのまま使い、
    //   ハッシュ計算のときだけこの正規化版を使う。
    std::string NormalizeAssetKey(const char *path)
    {
        std::string s = path ? path : "";
        for (char &c : s)
        {
            if (c == '\\') c = '/';
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }
} // namespace

void ResourceManager::ClearAll()
{
    // マルチスレッド対応の場合はロックをかける
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. キャッシュ（Registry）のクリア
    m_textureRegistry.clear();
    m_modelRegistry.clear();
    m_animSequenceRegistry.clear();
    m_assetPathRegistry.clear();
    m_animCurveRegistry.clear();

    // 2. 実体（Pool）のクリア
    // ※ 内部の ComPtr がここで一斉に Release() されます
    m_texturePool.Clear();
    m_modelPool.Clear();
    m_modelInstancePool.Clear();
    m_materialPool.Clear();
    m_animSequencePool.Clear();
    m_renderTargetPool.Clear();
    m_particleBufferPool.Clear();
    m_animCurvePool.Clear();
}

// ============================================================================
// [1] Texture
// ============================================================================
TextureHandle ResourceManager::LoadTexture(const char *filename)
{
    if (!filename) return TextureHandle{};
    uint32_t hash = HashString(filename);

    // 1. まずReadロック（またはミューテックス）でキャッシュを確認
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto                        it = m_textureRegistry.find(hash);
        if (it != m_textureRegistry.end())
        {
            if (m_texturePool.Get(it->second) != nullptr) return it->second;
        }
    }

    // 2. IO処理（重いファイル読み込み）はロックの外で行う！（超重要）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    GpuResourceUtils::LoadTexture(Graphics::Instance().GetDevice(), filename, srv.GetAddressOf());

    if (srv)
    {
        SetDebugName(srv.Get(), std::string("Tex_") + filename);

        std::lock_guard<std::mutex> lock(m_mutex);

        // 1. プールに空き部屋を作り、互換コンストラクタ経由で登録
        TextureHandle    handle = m_texturePool.Create(srv);
        TextureResource *asset  = m_texturePool.Get(handle);

        if (asset)
        {
            // 2. メタデータの自動解析とインライン・キャッシュの実行
            asset->debugPath = filename;

            Microsoft::WRL::ComPtr<ID3D11Resource> resource;
            srv->GetResource(resource.GetAddressOf());

            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2D;
            if (SUCCEEDED(resource.As(&texture2D)))
            {
                D3D11_TEXTURE2D_DESC desc;
                texture2D->GetDesc(&desc);

                asset->width  = desc.Width;
                asset->height = desc.Height;

                // ミップマップの容量も含めて算出 (2Dテクスチャは全ミップ合計で約1.33倍(4/3)のサイズになります)
                size_t mipMultiplier = (desc.MipLevels == 0 || desc.MipLevels > 1) ? 4 : 3;

                // 1ピクセルあたり4バイト(RGBA)としてVRAMアロケーションサイズを擬似計算
                asset->byteSize = (desc.Width * desc.Height * 4 * mipMultiplier) / 3;
            }
        }

        m_textureRegistry[hash] = handle;
        return handle;
    }
    return TextureHandle{};
}

TextureHandle ResourceManager::RegisterTexture(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv)
{
    if (!srv) return TextureHandle{};

    // ==========================================================
    // 抜け道(裏口)から登録された SRV にも名札を付ける！
    // ==========================================================
    SetDebugName(srv.Get(), "RegisteredTexture_UnknownSource");

    return m_texturePool.Create(srv);
}

// --- ResourceManager.cpp ---

size_t ResourceManager::CalculateTotalVramUsage() const
{
    size_t totalBytes = 0;

    // テクスチャプールを一直線に走査（DOD配置なので爆速）
    m_texturePool.ForEachActive([&](const TextureResource &tex) {
        totalBytes += tex.byteSize;
    });

    // レンダーターゲット（RT）のプールも同様に加算
    m_renderTargetPool.ForEachActive([&](const RenderTarget &rt) {
        // 例: width * height * 4バイト (RGBA) などの計算値を加算
        // totalBytes += rt.GetMemorySize();
    });

    return totalBytes;
}

// アクティブな詳細アセットリストをエディタUIに引き渡すための関数
std::vector<std::pair<std::string, size_t>> ResourceManager::GetSortedTextureList() const
{
    std::vector<std::pair<std::string, size_t>> list;

    m_texturePool.ForEachActive([&](const TextureResource &tex) {
        list.push_back({tex.debugPath, tex.byteSize});
    });

    // 重い順（降順）にソートして、エディタで「メモリ泥棒」を一瞬で見つけられるようにする
    std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });

    return list;
}

ID3D11ShaderResourceView *ResourceManager::GetTexture(TextureHandle handle)
{
    TextureResource *tex = m_texturePool.Get(handle);
    return tex ? tex->srv.Get() : nullptr;
}

void ResourceManager::UnloadTexture(TextureHandle handle)
{
    m_texturePool.Destroy(handle);
}

// ============================================================================
// [2] Model Resource
// ============================================================================
ModelHandle ResourceManager::LoadModelResource(const char *filename)
{
    if (!filename) return ModelHandle{};

    uint32_t hash = HashString(filename);

    // ============================================================
    // 非同期シーンロード対応：ワーカースレッドから呼ばれ得るため、
    // 共有レジストリ m_modelRegistry への参照/更新を m_mutex で保護する。
    // ただし model->Load() は内部で LoadTexture()（同じ m_mutex をロック）を
    // 呼ぶ可能性があり、ロックを跨いだまま呼ぶと自己デッドロックする。
    // → 「登録操作だけロック・重いロード本体はロック外」のダブルチェックロック。
    // ============================================================

    // 1) キャッシュ命中確認（ロック下）
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto                        it = m_modelRegistry.find(hash);
        if (it != m_modelRegistry.end() && m_modelPool.Get(it->second) != nullptr)
            return it->second;
    }

    // 2) 空きスロット確保（プール操作のためロック下）
    ModelHandle handle;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        handle = m_modelPool.Create();
    }

    // 3) 実データのロード（重い・LoadTexture 経由で再入し得るためロック外で実行）
    if (ModelResource *model = m_modelPool.Get(handle))
        model->Load(Graphics::Instance().GetDevice(), filename);

    // 4) レジストリ登録（ロック下・二重ロードは「先着を採用し自分は破棄」で解決）
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto                        it = m_modelRegistry.find(hash);
        if (it != m_modelRegistry.end() && m_modelPool.Get(it->second) != nullptr)
        {
            // 別スレッドが先に同じモデルを載せていた → 自分の分は捨てて既存を使う
            m_modelPool.Destroy(handle);
            return it->second;
        }
        m_modelRegistry[hash] = handle;
#ifdef _DEBUG
        m_debugNameRegistry[hash] = filename;
#endif
    }
    return handle;
}

ModelHandle ResourceManager::AllocateEmptyModelResource()
{
    return m_modelPool.Create();
}

ModelResource *ResourceManager::GetModel(ModelHandle handle)
{
    return m_modelPool.Get(handle);
}

void ResourceManager::UnloadModel(ModelHandle handle)
{
    m_modelPool.Destroy(handle);
}

std::string ResourceManager::GetAssetPath(uint32_t hash)
{
    if (hash == 0) return "";

    std::lock_guard<std::mutex> lock(m_mutex);
    auto                        it = m_assetPathRegistry.find(hash);
    if (it != m_assetPathRegistry.end())
    {
        return it->second;
    }
    return "";
}

// ============================================================================
// [3] Model Instance
// ============================================================================
ModelInstanceHandle ResourceManager::CreateModelInstance(const char *filename)
{
    if (!filename || filename[0] == '\0') return ModelInstanceHandle{};

    uint32_t hash = CCL::Utils::HashString(filename);

    // 文字列実体を中央レジストリに記憶しておく
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_assetPathRegistry[hash] = filename;
    }

    return m_modelInstancePool.Create(Graphics::Instance().GetDevice(), filename, 60.0f);
}

// コンポーネントのコピー/ムーブ時に呼ばれる復元関数
ModelInstanceHandle ResourceManager::CreateModelInstanceFromHash(uint32_t hash)
{
    if (hash == 0) return ModelInstanceHandle{};

    std::string path;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto                        it = m_assetPathRegistry.find(hash);
        if (it != m_assetPathRegistry.end())
        {
            path = it->second;
        }
    }

    // 登録されたパスが見つかれば、そこから生成する
    if (!path.empty())
    {
        return m_modelInstancePool.Create(Graphics::Instance().GetDevice(), path.c_str(), 60.0f);
    }

    return ModelInstanceHandle{};
}

Model *ResourceManager::GetModelInstance(ModelInstanceHandle handle)
{
    return m_modelInstancePool.Get(handle);
}

void ResourceManager::UnloadModelInstance(ModelInstanceHandle handle)
{
    m_modelInstancePool.Destroy(handle);
}

// ============================================================================
// [4] Material
// ============================================================================
MaterialHandle ResourceManager::CreateMaterial()
{
    return m_materialPool.Create();
}

MaterialHandle ResourceManager::CloneMaterial(MaterialHandle source)
{
    MaterialData *srcData = m_materialPool.Get(source);
    if (srcData) return m_materialPool.Create(*srcData);
    return m_materialPool.Create();
}

MaterialData *ResourceManager::GetMaterial(MaterialHandle handle)
{
    return m_materialPool.Get(handle);
}

void ResourceManager::UnloadMaterial(MaterialHandle handle)
{
    m_materialPool.Destroy(handle);
}

// ============================================================================
// [5] Animation Sequence
// ============================================================================
const AnimSequence *ResourceManager::LoadAnimSequence(const char *filename)
{
    if (!filename || filename[0] == '\0')
        return nullptr;

    // LoadAnimCurve と同様にスレッドセーフにする(レジストリ/プールの競合防止)。
    std::lock_guard<std::mutex> lock(m_mutex);

    uint32_t hash = HashString(NormalizeAssetKey(filename).c_str());
    auto     it   = m_animSequenceRegistry.find(hash);
    if (it != m_animSequenceRegistry.end())
    {
        AnimSequence *seq = m_animSequencePool.Get(it->second);
        if (seq)
            return seq;
    }

    // Cereal を廃止し、nlohmann::json でロードする
    std::string jsonStr = FileSystem::ReadAllText(filename);
    if (jsonStr.empty())
        return nullptr;

    AnimSequenceHandle handle = m_animSequencePool.Create();
    AnimSequence      *seq    = m_animSequencePool.Get(handle);

    try
    {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        *seq             = j.get<AnimSequence>();
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to parse AnimSequence json: %s (%s)", filename, e.what());
        m_animSequencePool.Destroy(handle);
        return nullptr;
    }

    // =====================================================================
    // ロード後処理: 不変条件の保証と派生データの計算
    // =====================================================================

    // (1) startTime 昇順ソートを保証する。
    //     AnimationSystem の O(1) 栞ロジックは events が昇順である前提。
    //     Sequencer 保存は SortAndResync 済みだが、手書き/外部生成 JSON は
    //     未ソートの可能性があるため、ロード経路で必ず保証する。
    std::sort(seq->events.begin(), seq->events.end(),
              [](const AnimNotifyEvent &a, const AnimNotifyEvent &b) {
                  return a.startTime < b.startTime;
              });

    // (2) HitBox イベントの tagHash をここで1回だけ計算してキャッシュする。
    //     CombatAnimationSyncSystem が毎フレーム HashString する必要をなくす。
    //     tagHash は派生データのため JSON には保存せず、必ずこのロード時に再計算する
    //     (stringParam を手編集しても不整合が起きない)。
    for (auto &ev : seq->events)
    {
        if (ev.type == AnimEventType::HitBox && !ev.stringParam.empty())
            ev.tagHash = HashString(ev.stringParam.c_str());
        else
            ev.tagHash = 0;
    }

    m_animSequenceRegistry[hash] = handle;
#ifdef _DEBUG
    m_debugNameRegistry[hash] = filename;
#endif

    return seq;
}

// 既存のキャッシュインスタンスへディスクの内容を「その場で上書き」する。
// currentSequence 等のポインタを無効化しないため、エディタ保存直後でも安全に
// ランタイムへ即時反映できる。未キャッシュなら通常ロードにフォールバックする。
const AnimSequence *ResourceManager::ReloadAnimSequence(const char *filename)
{
    if (!filename || filename[0] == '\0')
        return nullptr;

    uint32_t hash = HashString(NormalizeAssetKey(filename).c_str());

    AnimSequence *seq = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto                        it = m_animSequenceRegistry.find(hash);
        if (it == m_animSequenceRegistry.end())
            seq = nullptr; // 未キャッシュ
        else
            seq = m_animSequencePool.Get(it->second);
    }

    // まだ一度もロードされていなければ、通常ロードに任せる（新規キャッシュ生成）。
    if (!seq)
    {
        // ここに来る＝「キャッシュキーが見つからなかった」ということ。
        //   保存前に既に同じファイルがどこかで読み込まれているはずなのに毎回ここに来る場合、
        //   Sequencerの保存パスとゲーム側が参照しているパス文字列がズレている（別ファイル
        //   として扱われている）可能性が高い。m_debugNameRegistry と見比べて確認できる。
        CCL_LOG_WARN(LogCategory::Core,
                     "ReloadAnimSequence: cache miss for '%s' (key='%s') -> 新規ロードとして扱います。"
                     "既存キャッシュに反映されない場合はパス表記のズレを疑ってください。",
                     filename, NormalizeAssetKey(filename).c_str());
        return LoadAnimSequence(filename);
    }

    std::string jsonStr = FileSystem::ReadAllText(filename);
    if (jsonStr.empty())
        return seq; // 読めなければ既存内容を維持

    std::lock_guard<std::mutex> lock(m_mutex);
    try
    {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        *seq             = j.get<AnimSequence>(); // 同一インスタンスへ上書き
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to reparse AnimSequence json: %s (%s)", filename, e.what());
        return seq;
    }

    // ロード後処理（LoadAnimSequence と同一）
    std::sort(seq->events.begin(), seq->events.end(),
              [](const AnimNotifyEvent &a, const AnimNotifyEvent &b) { return a.startTime < b.startTime; });
    for (auto &ev : seq->events)
    {
        if (ev.type == AnimEventType::HitBox && !ev.stringParam.empty())
            ev.tagHash = HashString(ev.stringParam.c_str());
        else
            ev.tagHash = 0;
    }

    CCL_LOG_SUCCESS(LogCategory::Core, "Reloaded AnimSequence: %s", filename);
    return seq;
}

const CurveData *ResourceManager::LoadAnimCurve(const char *filepath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    uint32_t                    hash = CCL::Utils::HashString(NormalizeAssetKey(filepath).c_str());

    auto it = m_animCurveRegistry.find(hash);
    if (it != m_animCurveRegistry.end())
        return m_animCurvePool.Get(it->second);

    std::string jsonStr = FileSystem::ReadAllText(filepath);
    if (jsonStr.empty())
    {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to open curve file: %s", filepath);
        return nullptr;
    }

    CCL::Handle<CurveData> handle   = m_animCurvePool.Create();
    CurveData             *newCurve = m_animCurvePool.Get(handle);
    try
    {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        // 複数カーブ文書なら先頭、単一/旧AnimationCurve形式もそのまま読める
        if (!LoadCurveDataFromJson(j, *newCurve))
        {
            CCL_LOG_ERROR(LogCategory::Core, "Curve has no keys: %s", filepath);
            m_animCurvePool.Destroy(handle);
            return nullptr;
        }
        m_animCurveRegistry[hash] = handle;
        m_assetPathRegistry[hash] = filepath;
        CCL_LOG_SUCCESS(LogCategory::Core, "Loaded Anim Curve: %s", filepath);
        return newCurve;
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to parse curve JSON: %s (%s)", filepath, e.what());
        m_animCurvePool.Destroy(handle);
        return nullptr;
    }
}

// ============================================================================
// [6] Render Target
// ============================================================================
RenderTargetHandle ResourceManager::CreateRenderTarget(int width, int height, DXGI_FORMAT format)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    RenderTargetHandle          handle = m_renderTargetPool.Create();
    RenderTarget               *rt     = m_renderTargetPool.Get(handle);
    if (rt)
    {
        rt->Create(Graphics::Instance().GetDevice(), width, height, format);
    }
    return handle;
}

RenderTargetHandle ResourceManager::LoadRenderTarget(const std::string &name, ID3D11Device *device, UINT width, UINT height, DXGI_FORMAT format)
{
    // 1. プールから空き枠を確保
    RenderTargetHandle handle = m_renderTargetPool.Create();
    RenderTarget      *rt     = m_renderTargetPool.Get(handle);

    // 2. RenderTarget 自体の Create 処理（GPU リソースの生成）
    if (rt && rt->Create(device, width, height, format))
    {
        //#ifdef _DEBUG
        //        // デバッグ名が登録できればしておく（PixやRenderDocで便利）
        //        m_debugNameRegistry[handle.GetIndex()] = name;
        //#endif
        return handle;
    }

    // 失敗した場合は即座に破棄して無効なハンドルを返す
    m_renderTargetPool.Destroy(handle);
    CCL_LOG_ERROR(LogCategory::Render, "Failed to create RenderTarget: %s", name.c_str());
    return RenderTargetHandle{};
}

RenderTarget *ResourceManager::GetRenderTarget(RenderTargetHandle handle)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_renderTargetPool.Get(handle);
}

void ResourceManager::UnloadRenderTarget(RenderTargetHandle handle)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    RenderTarget               *rt = m_renderTargetPool.Get(handle);
    if (rt)
    {
        rt->Release();
    }
    m_renderTargetPool.Destroy(handle);
}

// ============================================================================
// [7] GPU Particle Buffer
// ============================================================================
ParticleBufferHandle ResourceManager::CreateParticleBuffer(ID3D11Device *device, int maxParticles)
{
    ParticleBufferHandle  handle  = m_particleBufferPool.Create();
    GPUParticleBufferSet *buffers = m_particleBufferPool.Get(handle);
    if (!buffers) return handle;

    HRESULT hr;

    // ① 構造化バッファ (Structured Buffer)
    D3D11_BUFFER_DESC bufDesc   = {};
    bufDesc.ByteWidth           = sizeof(GpuData::GpuParticle) * maxParticles;
    bufDesc.Usage               = D3D11_USAGE_DEFAULT;
    bufDesc.BindFlags           = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufDesc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufDesc.StructureByteStride = sizeof(GpuData::GpuParticle);
    hr                          = device->CreateBuffer(&bufDesc, nullptr, buffers->particleBuffer.ReleaseAndGetAddressOf());

    // ② UAV (Compute Shader 書き込み用)
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format                           = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension                    = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements               = maxParticles;
    hr                                       = device->CreateUnorderedAccessView(buffers->particleBuffer.Get(), &uavDesc, buffers->particleUAV.ReleaseAndGetAddressOf());

    // ③ SRV (Vertex/Geometry Shader 読み取り用)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                          = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements              = maxParticles;
    hr                                      = device->CreateShaderResourceView(buffers->particleBuffer.Get(), &srvDesc, buffers->particleSRV.ReleaseAndGetAddressOf());

    // ④ 定数バッファ (staticCB & dynamicCB)
    // ※DirectXのルールで定数バッファのサイズは16の倍数である必要があるため、アライメント計算を入れる
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage             = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;

    // -- staticCB の作成 --
    cbDesc.ByteWidth = sizeof(GpuData::CbParticleStatic);
    cbDesc.ByteWidth = (cbDesc.ByteWidth + 15) / 16 * 16;
    hr               = device->CreateBuffer(&cbDesc, nullptr, buffers->staticCB.ReleaseAndGetAddressOf());

    // -- dynamicCB の作成 --
    cbDesc.ByteWidth = sizeof(GpuData::CbParticleDynamic);
    cbDesc.ByteWidth = (cbDesc.ByteWidth + 15) / 16 * 16;
    hr               = device->CreateBuffer(&cbDesc, nullptr, buffers->dynamicCB.ReleaseAndGetAddressOf());

    // ⑤ デバッグ名の付与（D3D11 のリークレポートに型名付きで出る）
    SetDebugName(buffers->particleBuffer.Get(), "Particle_StructuredBuffer");
    SetDebugName(buffers->particleUAV.Get(), "Particle_UAV");
    SetDebugName(buffers->particleSRV.Get(), "Particle_SRV");
    SetDebugName(buffers->staticCB.Get(), "Particle_StaticCB");
    SetDebugName(buffers->dynamicCB.Get(), "Particle_DynamicCB");

    return handle;
}

GPUParticleBufferSet *ResourceManager::GetParticleBuffer(ParticleBufferHandle handle)
{
    return m_particleBufferPool.Get(handle);
}

void ResourceManager::UnloadParticleBuffer(ParticleBufferHandle handle)
{
    m_particleBufferPool.Destroy(handle);
}

// ============================================================================
// Debug
// ============================================================================
void ResourceManager::DrawDebugGUI()
{
#ifdef ENABLE_EDITOR
    if (ImGui::CollapsingHeader("Resource", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Models:");
        for (auto &pair : m_modelRegistry)
        {
            bool isValid = (m_modelPool.Get(pair.second) != nullptr);
#ifdef _DEBUG
            std::string           name = m_debugNameRegistry[pair.first];
            std::filesystem::path filepath(name);
            ImGui::Text("  [%s] : %s (Hash:%u)", isValid ? "Active" : "Dead", filepath.filename().u8string().c_str(), pair.first);
#else
            ImGui::Text("  [%s] : Hash:%u", isValid ? "Active" : "Dead", pair.first);
#endif
        }

        ImGui::Separator();

        ImGui::Text("Textures:");
        for (auto &pair : m_textureRegistry)
        {
            bool isValid = (m_texturePool.Get(pair.second) != nullptr);
#ifdef _DEBUG
            std::string           name = m_debugNameRegistry[pair.first];
            std::filesystem::path filepath(name);
            ImGui::Text("  [%s] : %s", isValid ? "Active" : "Dead", filepath.filename().u8string().c_str());
#else
            ImGui::Text("  [%s] : Hash:%u", isValid ? "Active" : "Dead", pair.first);
#endif
        }
    }

#endif
}

MaterialHandle ResourceManager::LoadMaterialAsset(const std::string &virtualPath)
{
    std::string jsonStr = FileSystem::ReadAllText(virtualPath);
    if (jsonStr.empty())
        return MaterialHandle{}; // ロード失敗

    MaterialHandle handle = m_materialPool.Create();
    MaterialData  *mat    = m_materialPool.Get(handle);

    try
    {
        nlohmann::json j = nlohmann::json::parse(jsonStr);

        // 古いCereal形式の "Material" ネスト構造をサポート
        if (j.contains("Material"))
        {
            *mat = j["Material"].get<MaterialData>();
        }
        else
        {
            *mat = j.get<MaterialData>();
        }

        for (auto &texRef : mat->textures)
        {
            if (!texRef.assetPath.empty())
            {
                texRef.handle = LoadTexture(texRef.assetPath.c_str());
            }
        }
        return handle;
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to parse material json: %s (%s)", virtualPath.c_str(), e.what());
        m_materialPool.Destroy(handle);
        return MaterialHandle{};
    }
}

bool ResourceManager::SaveMaterialAsset(MaterialHandle handle, const std::string &virtualPath)
{
    MaterialData *mat = m_materialPool.Get(handle);
    if (!mat) return false;

    try
    {
        nlohmann::json j;
        j["Material"] = *mat;
        return FileSystem::WriteAllText(virtualPath, j.dump(4));
    }
    catch (...)
    {
        return false;
    }
}

//  目的:
//    Model::CreateFromData() が返す shared_ptr<Model>（ランタイム生成メッシュ）を
//    インスタンスプールに登録し、ModelComponent が持てる ModelInstanceHandle を返す。
//
//  背景:
//    現プロジェクトの ModelComponent は ModelInstanceHandle で Model を参照する。
//    既存の CreateModelInstance(filename) はファイルパスからしか生成できないため、
//    ランタイム生成 Model をプールに預ける口をここで足す。
//
//  なぜムーブで安全か:
//    Model::CreateFromData は ModelResource 側にメッシュを構築し、Model::meshes
//    （Mesh::node / material といった自己参照ポインタを持つ vector）は空のまま返す。
//    描画も MeshBatcher も model->GetResource()->GetMeshes() を参照するため、
//    Model 自体をムーブしても自己参照ポインタ問題は描画経路に影響しない。
//    （resource は CCL::Handle<ModelResource> = ただの index/gen 値なのでムーブ安全）
//
//  組み込み:
//    ResourceManager.h の [3] Model Instance に下記を追加:
//
//        // 構築済み Model をプール登録してハンドル化（ランタイム生成メッシュ用）
//        ModelInstanceHandle RegisterModelInstance(std::shared_ptr<Model> model);
//============================================================================
ModelInstanceHandle ResourceManager::RegisterModelInstance(std::shared_ptr<Model> model)
{
    if (!model) return ModelInstanceHandle{};

    std::lock_guard<std::mutex> lock(m_mutex);
    // ResourcePool<Model>::Create(Args&&...) は内部で placement-new Model(args...)。
    // Model のムーブコンストラクタ（暗黙 or 既存）でプールへ収める。
    return m_modelInstancePool.Create(std::move(*model));
}