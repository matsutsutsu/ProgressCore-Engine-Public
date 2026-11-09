#include "ResourceManager.h"
#include <filesystem>
#include <fstream>
#include <cereal/archives/json.hpp>

#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/DebugName.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"

#include "Engine/Platform/Core/FileSystem.h"
#include <sstream>

using namespace CCL::Utils;

namespace
{
    // キャッシュのキーにするため、パスの区切り文字と大文字小文字をそろえる。ファイルを読むときは元のパスを使う
    // HashString はバイトが完全に一致しないと同じ値にならないので、"Assets/A.json" と "Assets\A.json" が別のキーになり、
    // 保存しても反映されない
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



    // AnimSequence を読んだ後に行う処理（LoadAnimSequence と ReloadAnimSequence で共通）
    void FinalizeAnimSequence(AnimSequence &seq)
    {
        // 1. startTime の小さい順に並べる
        //    AnimationSystem は前回の位置から先へ進めるだけでイベントを探す（O(1)）ので、
        //    events が昇順になっている必要がある。Sequencer の保存も手書きの JSON も
        //    並びを保証しないので、読み込むたびにここで並べる
        std::sort(seq.events.begin(), seq.events.end(),
                  [](const AnimNotifyEvent &a, const AnimNotifyEvent &b) {
                      return a.startTime < b.startTime;
                  });

        // 2. HitBox イベントの tagHash をここで1回だけ求める
        //    CombatAnimationSyncSystem が毎フレーム HashString せずに済む
        //    tagHash は stringParam から求める値なので JSON には保存せず、読み込むたびに求め直す
        //    （stringParam を手で書き換えても食い違わない）
        for (auto &ev : seq.events)
        {
            if (ev.type == AnimEventType::HitBox && !ev.stringParam.empty())
                ev.tagHash = HashString(ev.stringParam.c_str());
            else
                ev.tagHash = 0;
        }
    }
} // namespace

void ResourceManager::ClearAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. キャッシュを空にする
    m_textureRegistry.clear();
    m_modelRegistry.clear();
    m_animSequenceRegistry.clear();
    m_assetPathRegistry.clear();

    // 2. プールを空にする（中の ComPtr がここで Release される）
    m_texturePool.Clear();
    m_modelPool.Clear();
    m_modelInstancePool.Clear();
    m_materialPool.Clear();
    m_animSequencePool.Clear();
    m_renderTargetPool.Clear();
    m_particleBufferPool.Clear();
}



// ---------------------------------------------------------------------------
// テクスチャ
// ---------------------------------------------------------------------------
TextureHandle ResourceManager::LoadTexture(const char *filename)
{
    if (!filename) return TextureHandle{};
    uint32_t hash = HashString(filename);

    // 1. キャッシュを見る
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto                        it = m_textureRegistry.find(hash);
        if (it != m_textureRegistry.end())
        {
            if (m_texturePool.Get(it->second) != nullptr) return it->second;
        }
    }

    // 2. ファイルの読み込みはロックの外で行う（読み込み中に他のスレッドを止めないため）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    GpuResourceUtils::LoadTexture(Graphics::Instance().GetDevice(), filename, srv.GetAddressOf());

    if (srv)
    {
        SetDebugName(srv.Get(), std::string("Tex_") + filename);

        std::lock_guard<std::mutex> lock(m_mutex);

        TextureHandle    handle = m_texturePool.Create(srv);
        TextureResource *asset  = m_texturePool.Get(handle);

        if (asset)
        {
            // メモリ分析ウィンドウ用に、パス・サイズ・VRAM の見積もりを持っておく
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

                // ミップを含めて見積もる（全ミップの合計は元の約 4/3 倍）
                size_t mipMultiplier = (desc.MipLevels == 0 || desc.MipLevels > 1) ? 4 : 3;

                // 1ピクセル 4 バイト（RGBA8）として見積もる。圧縮形式でも同じ式なので、実際より大きく出る
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

    // ファイルを通さずに作った SRV にもデバッグ名を付けておく
    SetDebugName(srv.Get(), "RegisteredTexture_UnknownSource");

    return m_texturePool.Create(srv);
}



ID3D11ShaderResourceView *ResourceManager::GetTexture(TextureHandle handle)
{
    TextureResource *tex = m_texturePool.Get(handle);
    return tex ? tex->srv.Get() : nullptr;
}



void ResourceManager::UnloadTexture(TextureHandle handle)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_texturePool.Destroy(handle);
}



// ---------------------------------------------------------------------------
// モデルの共有データ
// ---------------------------------------------------------------------------
ModelHandle ResourceManager::LoadModelResource(const char *filename)
{
    if (!filename) return ModelHandle{};

    uint32_t hash = HashString(filename);

    // シーンの非同期読み込みでワーカースレッドから呼ばれるので、m_modelRegistry は m_mutex で守る
    // ただし model->Load() は中で LoadTexture()（同じ m_mutex を取る）を呼ぶので、ロックを持ったまま
    // 呼ぶとデッドロックする。そこで、登録の操作だけをロックし、重い読み込みはロックの外で行い、
    // 登録の直前にもう一度キャッシュを確かめる（ダブルチェック）

    // 1) キャッシュにあるか（ロック中）
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto                        it = m_modelRegistry.find(hash);
        if (it != m_modelRegistry.end() && m_modelPool.Get(it->second) != nullptr)
            return it->second;
    }

    // 2) プールにスロットを取る（ロック中）
    ModelHandle handle;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        handle = m_modelPool.Create();
    }

    // 3) 読み込む（重く、中で LoadTexture を呼ぶのでロックの外）
    if (ModelResource *model = m_modelPool.Get(handle))
    {
        model->Load(Graphics::Instance().GetDevice(), filename);

        // LOD を予約されたモデルなら、ここで作る
        // レジストリに登録する前なので、ほかのスレッドや描画からはまだ見えておらず、書き換えても安全
        bool wantsLod = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            wantsLod = (m_lodRequests.count(hash) != 0);
        }
        if (wantsLod)
        {
            model->GenerateLods(Graphics::Instance().GetDevice());
        }
    }

    // 4) レジストリに登録する（ロック中）。同時に同じモデルを読んでいたら、先に登録した方を使う
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto                        it = m_modelRegistry.find(hash);
        if (it != m_modelRegistry.end() && m_modelPool.Get(it->second) != nullptr)
        {
            // 他のスレッドが先に登録していたので、自分の分は捨てる
            m_modelPool.Destroy(handle);
            return it->second;
        }
        m_modelRegistry[hash] = handle;
    }
    return handle;
}



ModelResource *ResourceManager::GetModel(ModelHandle handle)
{
    return m_modelPool.Get(handle);
}



void ResourceManager::RequestModelLod(const char *filename)
{
    if (!filename || filename[0] == '\0') return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_lodRequests.insert(HashString(filename));
}



void ResourceManager::UnloadModel(ModelHandle handle)
{
    std::lock_guard<std::mutex> lock(m_mutex);
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



// ---------------------------------------------------------------------------
// モデルのインスタンス
// ---------------------------------------------------------------------------
ModelInstanceHandle ResourceManager::CreateModelInstance(const char *filename)
{
    if (!filename || filename[0] == '\0') return ModelInstanceHandle{};

    uint32_t hash = CCL::Utils::HashString(filename);

    // ハッシュからパスを引けるように覚えておく
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_assetPathRegistry[hash] = filename;
    }

    return m_modelInstancePool.Create(Graphics::Instance().GetDevice(), filename, 60.0f);
}



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



// ---------------------------------------------------------------------------
// マテリアル
// ---------------------------------------------------------------------------
MaterialHandle ResourceManager::CreateMaterial()
{
    return m_materialPool.Create();
}



MaterialHandle ResourceManager::CloneMaterial(MaterialHandle source)
{
    if (MaterialData *src = m_materialPool.Get(source)) return m_materialPool.Create(*src);
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



// ---------------------------------------------------------------------------
// アニメーション
// ---------------------------------------------------------------------------
const AnimSequence *ResourceManager::LoadAnimSequence(const char *filename)
{
    if (!filename || filename[0] == '\0')
        return nullptr;

    // レジストリとプールをロックで守る
    std::lock_guard<std::mutex> lock(m_mutex);

    uint32_t hash = HashString(NormalizeAssetKey(filename).c_str());
    auto     it   = m_animSequenceRegistry.find(hash);
    if (it != m_animSequenceRegistry.end())
    {
        AnimSequence *seq = m_animSequencePool.Get(it->second);
        if (seq)
            return seq;
    }

    // アニメーションのシーケンスは JSON（モデルの .cereal キャッシュとは別の形式）
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

    FinalizeAnimSequence(*seq);

    m_animSequenceRegistry[hash] = handle;
    return seq;
}



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
            seq = nullptr;
        else
            seq = m_animSequencePool.Get(it->second);
    }

    if (!seq)
    {
        // 保存前に同じファイルを読み込んでいるはずなのに毎回ここに来るなら、Sequencer が保存した
        // パスとゲームが参照しているパスが違っている（別のファイルとして扱われている）可能性が高い
        // ログの key と、ゲーム側が読んだパスを見比べると確かめられる
        CCL_LOG_WARN(LogCategory::Core,
                     "ReloadAnimSequence: cache miss for '%s' (key='%s') -> 新規ロードとして扱います。"
                     "既存キャッシュに反映されない場合はパス表記のズレを疑ってください。",
                     filename, NormalizeAssetKey(filename).c_str());
        return LoadAnimSequence(filename);
    }

    std::string jsonStr = FileSystem::ReadAllText(filename);
    if (jsonStr.empty())
        return seq; // 読めなければ今の内容のまま

    std::lock_guard<std::mutex> lock(m_mutex);
    try
    {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        *seq             = j.get<AnimSequence>(); // 同じインスタンスに上書きする
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to reparse AnimSequence json: %s (%s)", filename, e.what());
        return seq;
    }

    FinalizeAnimSequence(*seq);

    CCL_LOG_SUCCESS(LogCategory::Core, "Reloaded AnimSequence: %s", filename);
    return seq;
}



// ---------------------------------------------------------------------------
// レンダーターゲット
// ---------------------------------------------------------------------------
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
    RenderTargetHandle handle = m_renderTargetPool.Create();
    RenderTarget      *rt     = m_renderTargetPool.Get(handle);

    if (rt && rt->Create(device, width, height, format))
    {
        return handle;
    }

    // 作れなかったらスロットを返して、無効なハンドルを返す
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



// ---------------------------------------------------------------------------
// GPU パーティクルのバッファ
// ---------------------------------------------------------------------------
ParticleBufferHandle ResourceManager::CreateParticleBuffer(ID3D11Device *device, int maxParticles)
{
    ParticleBufferHandle  handle  = m_particleBufferPool.Create();
    GPUParticleBufferSet *buffers = m_particleBufferPool.Get(handle);
    if (!buffers) return handle;

    HRESULT hr;

    // 1. 粒子配列の StructuredBuffer
    D3D11_BUFFER_DESC bufDesc   = {};
    bufDesc.ByteWidth           = sizeof(GpuData::GpuParticle) * maxParticles;
    bufDesc.Usage               = D3D11_USAGE_DEFAULT;
    bufDesc.BindFlags           = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufDesc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufDesc.StructureByteStride = sizeof(GpuData::GpuParticle);
    hr                          = device->CreateBuffer(&bufDesc, nullptr, buffers->particleBuffer.ReleaseAndGetAddressOf());

    // 2. UAV（コンピュートシェーダーが書く）
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format                           = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension                    = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements               = maxParticles;
    hr                                       = device->CreateUnorderedAccessView(buffers->particleBuffer.Get(), &uavDesc, buffers->particleUAV.ReleaseAndGetAddressOf());

    // 3. SRV（頂点・ジオメトリシェーダーが読む）
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                          = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements              = maxParticles;
    hr                                      = device->CreateShaderResourceView(buffers->particleBuffer.Get(), &srvDesc, buffers->particleSRV.ReleaseAndGetAddressOf());

    // 4. 定数バッファ（staticCB と dynamicCB）。大きさは 16 の倍数にする必要があるので切り上げる
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

    // 5. デバッグ名を付ける（D3D11 の解放漏れレポートに名前付きで出る）
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



MaterialHandle ResourceManager::LoadMaterialAsset(const std::string &virtualPath)
{
    std::string jsonStr = FileSystem::ReadAllText(virtualPath);
    if (jsonStr.empty())
        return MaterialHandle{};

    MaterialHandle handle = m_materialPool.Create();
    MaterialData  *mat    = m_materialPool.Get(handle);

    try
    {
        nlohmann::json j = nlohmann::json::parse(jsonStr);

        // "Material" の下に入れた形式（SaveMaterialAsset が書く形）と、直接書いた形式の両方を読む
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
