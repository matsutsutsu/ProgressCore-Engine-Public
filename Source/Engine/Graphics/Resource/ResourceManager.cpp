#include "ResourceManager.h"
#include <filesystem>
#include <fstream>
#include <cereal/archives/json.hpp>

#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"

#include "Engine/Platform/Core/FileSystem.h" // ユーザー指定のパス
#include <cereal/archives/json.hpp>
#include <sstream>

#if ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::Utils;

void ResourceManager::ClearAll()
{
    // マルチスレッド対応の場合はロックをかける
    std::lock_guard<std::mutex> lock(_mutex);

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
TextureHandle ResourceManager::LoadTexture(const char* filename) {
    if (!filename) return TextureHandle{};
    uint32_t hash = HashString(filename);

    // 1. まずReadロック（またはミューテックス）でキャッシュを確認
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = m_textureRegistry.find(hash);
        if (it != m_textureRegistry.end()) {
            if (m_texturePool.Get(it->second) != nullptr) return it->second;
        }
    }

    // 2. IO処理（重いファイル読み込み）はロックの外で行う！（超重要）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    GpuResourceUtils::LoadTexture(Graphics::Instance().GetDevice(), filename, srv.GetAddressOf());

    if (srv) {

        // ==========================================================
        // ★ 名札付けを追加：どのファイル名のテクスチャか一目でわかるようにする
        // ==========================================================
        SetDebugName(srv.Get(), std::string("Tex_") + filename);

        // 3. 登録の瞬間だけ再度ロックする
        std::lock_guard<std::mutex> lock(_mutex);

        // （※別スレッドが同じファイルを先にロード完了していた場合の二重登録チェックが理想的ですがここでは割愛）
        TextureHandle handle = m_texturePool.Create(srv);
        m_textureRegistry[hash] = handle;
        return handle;
    }
    return TextureHandle{};
}

TextureHandle ResourceManager::RegisterTexture(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv) {
    if (!srv) return TextureHandle{};

    // ==========================================================
    // ★ 抜け道(裏口)から登録された SRV にも名札を付ける！
    // ==========================================================
    SetDebugName(srv.Get(), "RegisteredTexture_UnknownSource");

    return m_texturePool.Create(srv);
}

ID3D11ShaderResourceView* ResourceManager::GetTexture(TextureHandle handle) {
    TextureResource* tex = m_texturePool.Get(handle);
    return tex ? tex->Get() : nullptr;
}

void ResourceManager::UnloadTexture(TextureHandle handle) {
    m_texturePool.Destroy(handle);
}

// ============================================================================
// [2] Model Resource
// ============================================================================
ModelHandle ResourceManager::LoadModelResource(const char* filename) {
    if (!filename) return ModelHandle{};

    uint32_t hash = HashString(filename);
    auto it = m_modelRegistry.find(hash);
    if (it != m_modelRegistry.end()) {
        if (m_modelPool.Get(it->second) != nullptr) return it->second;
    }

    ModelHandle handle = m_modelPool.Create();
    ModelResource* model = m_modelPool.Get(handle);
    model->Load(Graphics::Instance().GetDevice(), filename);

    m_modelRegistry[hash] = handle;
#ifdef _DEBUG
    m_debugNameRegistry[hash] = filename;
#endif
    return handle;
}

ModelHandle ResourceManager::AllocateEmptyModelResource() {
    return m_modelPool.Create();
}

ModelResource* ResourceManager::GetModel(ModelHandle handle) {
    return m_modelPool.Get(handle);
}

void ResourceManager::UnloadModel(ModelHandle handle) {
    m_modelPool.Destroy(handle);
}

std::string ResourceManager::GetAssetPath(uint32_t hash) {
    if (hash == 0) return "";

    std::lock_guard<std::mutex> lock(_mutex);
    auto it = m_assetPathRegistry.find(hash);
    if (it != m_assetPathRegistry.end()) {
        return it->second;
    }
    return "";
}

// ============================================================================
// [3] Model Instance
// ============================================================================
ModelInstanceHandle ResourceManager::CreateModelInstance(const char* filename) {
    if (!filename || filename[0] == '\0') return ModelInstanceHandle{};

    uint32_t hash = CCL::Utils::HashString(filename);

    // 文字列実体を中央レジストリに記憶しておく
    {
        std::lock_guard<std::mutex> lock(_mutex);
        m_assetPathRegistry[hash] = filename;
    }

    return m_modelInstancePool.Create(Graphics::Instance().GetDevice(), filename, 60.0f);
}

// コンポーネントのコピー/ムーブ時に呼ばれる復元関数
ModelInstanceHandle ResourceManager::CreateModelInstanceFromHash(uint32_t hash) {
    if (hash == 0) return ModelInstanceHandle{};

    std::string path;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = m_assetPathRegistry.find(hash);
        if (it != m_assetPathRegistry.end()) {
            path = it->second;
        }
    }

    // 登録されたパスが見つかれば、そこから生成する
    if (!path.empty()) {
        return m_modelInstancePool.Create(Graphics::Instance().GetDevice(), path.c_str(), 60.0f);
    }

    return ModelInstanceHandle{};
}

Model* ResourceManager::GetModelInstance(ModelInstanceHandle handle) {
    return m_modelInstancePool.Get(handle);
}

void ResourceManager::UnloadModelInstance(ModelInstanceHandle handle) {
    m_modelInstancePool.Destroy(handle);
}

// ============================================================================
// [4] Material
// ============================================================================
MaterialHandle ResourceManager::CreateMaterial() {
    return m_materialPool.Create();
}

MaterialHandle ResourceManager::CloneMaterial(MaterialHandle source) {
    MaterialData* srcData = m_materialPool.Get(source);
    if (srcData) return m_materialPool.Create(*srcData);
    return m_materialPool.Create();
}

MaterialData* ResourceManager::GetMaterial(MaterialHandle handle) {
    return m_materialPool.Get(handle);
}

void ResourceManager::UnloadMaterial(MaterialHandle handle) {
    m_materialPool.Destroy(handle);
}

// ============================================================================
// [5] Animation Sequence
// ============================================================================
const AnimSequence* ResourceManager::LoadAnimSequence(const char* filename) {
    if (!filename || filename[0] == '\0') return nullptr;

    uint32_t hash = HashString(filename);
    auto it = m_animSequenceRegistry.find(hash);
    if (it != m_animSequenceRegistry.end()) {
        AnimSequence* seq = m_animSequencePool.Get(it->second);
        if (seq) return seq;
    }

    std::ifstream is(filename);
    if (!is.is_open()) return nullptr;

    AnimSequenceHandle handle = m_animSequencePool.Create();
    AnimSequence* seq = m_animSequencePool.Get(handle);

    try {
        cereal::JSONInputArchive archive(is);
        archive(*seq);
    }
    catch (...) {
        m_animSequencePool.Destroy(handle);
        return nullptr;
    }

    m_animSequenceRegistry[hash] = handle;
#ifdef _DEBUG
    m_debugNameRegistry[hash] = filename;
#endif

    return seq;
}

const AnimationCurve* ResourceManager::LoadAnimCurve(const char* filepath) {
    std::lock_guard<std::mutex> lock(_mutex); // スレッドセーフ

    uint32_t hash = CCL::Utils::HashString(filepath);

    // すでにロードされていれば、そのポインタを返す（重複ロード防止）
    auto it = m_animCurveRegistry.find(hash);
    if (it != m_animCurveRegistry.end()) {
        return m_animCurvePool.Get(it->second);
    }

    // JSONファイルの読み込み
    std::ifstream is(filepath);
    if (!is.is_open()) {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to open curve file: %s", filepath);
        return nullptr;
    }

    // プールから空き部屋を確保
    CCL::Handle<AnimationCurve> handle = m_animCurvePool.Create();
    AnimationCurve* newCurve = m_animCurvePool.Get(handle);

    try {
        // ※ CurveEditorWindow で nlohmann/json を使って保存した場合は、ここも nlohmann でパースします
        nlohmann::json j;
        is >> j;
        *newCurve = j.get<AnimationCurve>();

        m_animCurveRegistry[hash] = handle;
        m_assetPathRegistry[hash] = filepath;

        CCL_LOG_SUCCESS(LogCategory::Core, "Loaded Anim Curve: %s", filepath);
        return newCurve;
    }
    catch (const std::exception& e) {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to parse curve JSON: %s (%s)", filepath, e.what());
        m_animCurvePool.Destroy(handle);
        return nullptr;
    }
}

// ============================================================================
// [6] Render Target
// ============================================================================
RenderTargetHandle ResourceManager::CreateRenderTarget(int width, int height, DXGI_FORMAT format) {
    std::lock_guard<std::mutex> lock(_mutex);
    RenderTargetHandle handle = m_renderTargetPool.Create();
    RenderTarget* rt = m_renderTargetPool.Get(handle);
    if (rt) {
        rt->Create(Graphics::Instance().GetDevice(), width, height, format);
    }
    return handle;
}

RenderTarget* ResourceManager::GetRenderTarget(RenderTargetHandle handle) {
    std::lock_guard<std::mutex> lock(_mutex);
    return m_renderTargetPool.Get(handle);
}

void ResourceManager::UnloadRenderTarget(RenderTargetHandle handle) {
    std::lock_guard<std::mutex> lock(_mutex);
    RenderTarget* rt = m_renderTargetPool.Get(handle);
    if (rt) {
        rt->Release();
    }
    m_renderTargetPool.Destroy(handle);
}

// ============================================================================
// [7] GPU Particle Buffer
// ============================================================================
ParticleBufferHandle ResourceManager::CreateParticleBuffer(ID3D11Device* device, int maxParticles) {
    ParticleBufferHandle handle = m_particleBufferPool.Create();
    GPUParticleBufferSet* buffers = m_particleBufferPool.Get(handle);
    if (!buffers) return handle;

    HRESULT hr;

    // ① 構造化バッファ (Structured Buffer)
    D3D11_BUFFER_DESC bufDesc = {};
    bufDesc.ByteWidth = sizeof(GpuData::GpuParticle) * maxParticles;
    bufDesc.Usage = D3D11_USAGE_DEFAULT;
    bufDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufDesc.StructureByteStride = sizeof(GpuData::GpuParticle);
    hr = device->CreateBuffer(&bufDesc, nullptr, buffers->particleBuffer.ReleaseAndGetAddressOf());

    // ② UAV (Compute Shader 書き込み用)
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = maxParticles;
    hr = device->CreateUnorderedAccessView(buffers->particleBuffer.Get(), &uavDesc, buffers->particleUAV.ReleaseAndGetAddressOf());

    // ③ SRV (Vertex/Geometry Shader 読み取り用)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = maxParticles;
    hr = device->CreateShaderResourceView(buffers->particleBuffer.Get(), &srvDesc, buffers->particleSRV.ReleaseAndGetAddressOf());

    // ④ 定数バッファ (staticCB & dynamicCB)
    // ※DirectXのルールで定数バッファのサイズは16の倍数である必要があるため、アライメント計算を入れる
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    // -- staticCB の作成 --
    cbDesc.ByteWidth = sizeof(GpuData::CbParticleStatic);
    cbDesc.ByteWidth = (cbDesc.ByteWidth + 15) / 16 * 16;
    hr = device->CreateBuffer(&cbDesc, nullptr, buffers->staticCB.ReleaseAndGetAddressOf());

    // -- dynamicCB の作成 --
    cbDesc.ByteWidth = sizeof(GpuData::CbParticleDynamic);
    cbDesc.ByteWidth = (cbDesc.ByteWidth + 15) / 16 * 16;
    hr = device->CreateBuffer(&cbDesc, nullptr, buffers->dynamicCB.ReleaseAndGetAddressOf());

    // ⑤ 究極のデバッグ武器：名札付け
    SetDebugName(buffers->particleBuffer.Get(), "Particle_StructuredBuffer");
    SetDebugName(buffers->particleUAV.Get(), "Particle_UAV");
    SetDebugName(buffers->particleSRV.Get(), "Particle_SRV");
    SetDebugName(buffers->staticCB.Get(), "Particle_StaticCB");
    SetDebugName(buffers->dynamicCB.Get(), "Particle_DynamicCB");

    return handle;
}

GPUParticleBufferSet* ResourceManager::GetParticleBuffer(ParticleBufferHandle handle) {
    return m_particleBufferPool.Get(handle);
}

void ResourceManager::UnloadParticleBuffer(ParticleBufferHandle handle) {
    m_particleBufferPool.Destroy(handle);
}

// ============================================================================
// Debug
// ============================================================================
void ResourceManager::DrawDebugGUI() {
#if ENABLE_EDITOR
    if (ImGui::CollapsingHeader("Resource", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Models:");
        for (auto& pair : m_modelRegistry) {
            bool isValid = (m_modelPool.Get(pair.second) != nullptr);
#ifdef _DEBUG
            std::string name = m_debugNameRegistry[pair.first];
            std::filesystem::path filepath(name);
            ImGui::Text("  [%s] : %s (Hash:%u)", isValid ? "Active" : "Dead", filepath.filename().u8string().c_str(), pair.first);
#else
            ImGui::Text("  [%s] : Hash:%u", isValid ? "Active" : "Dead", pair.first);
#endif
        }

        ImGui::Separator();

        ImGui::Text("Textures:");
        for (auto& pair : m_textureRegistry) {
            bool isValid = (m_texturePool.Get(pair.second) != nullptr);
#ifdef _DEBUG
            std::string name = m_debugNameRegistry[pair.first];
            std::filesystem::path filepath(name);
            ImGui::Text("  [%s] : %s", isValid ? "Active" : "Dead", filepath.filename().u8string().c_str());
#else
            ImGui::Text("  [%s] : Hash:%u", isValid ? "Active" : "Dead", pair.first);
#endif
        }
    }

#endif
}

MaterialHandle ResourceManager::LoadMaterialAsset(const std::string& virtualPath)
{
    std::string jsonStr = FileSystem::ReadAllText(virtualPath);
    if (jsonStr.empty()) return MaterialHandle{}; // ロード失敗

    MaterialHandle handle = m_materialPool.Create();
    MaterialData* mat = m_materialPool.Get(handle);

    std::istringstream is(jsonStr);
    try {
        cereal::JSONInputArchive archive(is);
        archive(cereal::make_nvp("Material", *mat));

        // ★重要: JSONから読み込んだファイルパスを使って、実際のテクスチャをロードしてHandleを復旧させる
        for (auto& texRef : mat->textures) {
            if (!texRef.assetPath.empty()) {
                texRef.handle = LoadTexture(texRef.assetPath.c_str());
            }
        }
        return handle;
    }
    catch (...) {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to parse material json: %s", virtualPath.c_str());
        m_materialPool.Destroy(handle);
        return MaterialHandle{};
    }
}

bool ResourceManager::SaveMaterialAsset(MaterialHandle handle, const std::string& virtualPath)
{
    MaterialData* mat = m_materialPool.Get(handle);
    if (!mat) return false;

    std::ostringstream os;
    try {
        cereal::JSONOutputArchive archive(os);
        archive(cereal::make_nvp("Material", *mat));
    }
    catch (...) {
        return false;
    }

    // FileSystemを経由して書き込み
    return FileSystem::WriteAllText(virtualPath, os.str());
}