#include <filesystem>
#include <imgui.h>
#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/Graphics/Core/GpuResourceUtils.h"
#include "ResourceManager.h"

// Cereal の JSON アーカイブ用ヘッダー
#include <fstream>
#include <cereal/archives/json.hpp>

// 名前空間を省略して使いやすくする
using namespace CCL::Utils;

ModelHandle ResourceManager::LoadModelResource(const char* filename)
{
    if (!filename) return ModelHandle{};

    // 1. 文字列をハッシュ(数値)に変換
    uint32_t hash = HashString(filename);

    // 2. ハッシュで高速検索
    auto it = m_modelRegistry.find(hash);
    if (it != m_modelRegistry.end()) {
        // ハンドルが有効か確認
        if (m_modelPool.Get(it->second) != nullptr) {
            return it->second;
        }
    }

    // 3. 新規作成
    // プール上にメモリを確保し、ハンドルを取得
    ModelHandle handle = m_modelPool.Create();
    ModelResource* model = m_modelPool.Get(handle);

    // デバイスを使ってモデルをロード
    model->Load(Graphics::Instance().GetDevice(), filename);

    // 4. ハッシュをキーにして登録
    m_modelRegistry[hash] = handle;

    // デバッグ用に名前も覚えておく
#ifdef _DEBUG
    m_debugNameRegistry[hash] = filename;
#endif
    return handle;
}

TextureHandle ResourceManager::LoadTexture(const char* filename)
{
    if (!filename) return TextureHandle{};

    // 1. ハッシュ化
    uint32_t hash = HashString(filename);

    // 2. 高速検索
    auto it = m_textureRegistry.find(hash);
    if (it != m_textureRegistry.end()) {
        if (m_texturePool.Get(it->second) != nullptr) {
            return it->second;
        }
    }

    // 3. 新規読み込み
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    ID3D11Device                                    *device = Graphics::Instance().GetDevice();

    GpuResourceUtils::LoadTexture(device, filename, srv.GetAddressOf());

    if (srv) {
        TextureHandle handle = m_texturePool.Create(srv);
        // 4. 登録
        m_textureRegistry[hash] = handle;
#ifdef _DEBUG
        m_debugNameRegistry[hash] = filename;
#endif
        return handle;
    }

    return TextureHandle{}; // 失敗時は無効なハンドルを返す
}

TextureHandle ResourceManager::RegisterTexture(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv)
{
    if (!srv) return TextureHandle{}; // 無効なハンドルのまま返す

    // すでに存在するComPtrをプールに格納し、チケットを発行する
    return m_texturePool.Create(srv);
}

ModelHandle ResourceManager::AllocateEmptyModelResource()
{
    // キャッシュ(m_modelRegistry)の確認や登録は一切行わない。
    // Load() も呼ばない。
    // 純粋に新しいメモリ領域だけを確保して、独立したチケットを返す。
    return m_modelPool.Create();
}

ModelResource* ResourceManager::GetModel(ModelHandle handle)
{
    return m_modelPool.Get(handle);
}

ID3D11ShaderResourceView* ResourceManager::GetTexture(TextureHandle handle)
{
    TextureResource* tex = m_texturePool.Get(handle);
    return tex ? tex->Get() : nullptr;
}

void ResourceManager::UnloadModel(ModelHandle handle)
{
    m_modelPool.Destroy(handle);
}

void ResourceManager::UnloadTexture(TextureHandle handle)
{
    m_texturePool.Destroy(handle); }

MaterialHandle ResourceManager::CreateMaterial() 
{ 
    return m_materialPool.Create(); 
}

MaterialHandle ResourceManager::CloneMaterial(MaterialHandle source) 
{
    MaterialData *srcData = m_materialPool.Get(source);
    if (srcData) {
        // コピーコンストラクタを利用して複製
        return m_materialPool.Create(*srcData);
    }
    return m_materialPool.Create();
}

MaterialData *ResourceManager::GetMaterial(MaterialHandle handle)
{
    return m_materialPool.Get(handle);
}

void ResourceManager::UnloadMaterial(MaterialHandle handle) {
    m_materialPool.Destroy(handle); 
}


// レンダーターゲット作成
RenderTargetHandle ResourceManager::CreateRenderTarget(int width, int height, DXGI_FORMAT format)
{
    // この関数を抜けるまで、他のスレッドはここで待たされる
    std::lock_guard<std::mutex> lock(_mutex);

    // 1. プールから空き部屋をもらう
    RenderTargetHandle handle = m_renderTargetPool.Create();

    // 2. 実体を取得して初期化 (GPUリソース作成)
    RenderTarget *rt = m_renderTargetPool.Get(handle);
    if (rt) {
        // ※RenderTargetクラスに Create 関数がある前提です
        // もしコンストラクタでやる形式なら、ResourcePoolのCreate引数対応が必要ですが、
        // 通常は Create(device, w, h, format) のような形になっているはずです。
        rt->Create(Graphics::Instance().GetDevice(), width, height, format);
    }

    return handle;
}

RenderTarget *ResourceManager::GetRenderTarget(RenderTargetHandle handle)
{
    // 取得中も他のスレッドが配列を書き換えないようにロックする
    std::lock_guard<std::mutex> lock(_mutex);

    return m_renderTargetPool.Get(handle);
}

void ResourceManager::UnloadRenderTarget(RenderTargetHandle handle)
{
    // 破棄時もロックする
    std::lock_guard<std::mutex> lock(_mutex);

    RenderTarget *rt = m_renderTargetPool.Get(handle);
    if (rt) {
        // ★重要: プール（アパート）に部屋を返す前に、中に残っているGPUリソースを確実に破棄する
        // これをしないと、次に部屋が再利用された時にDirectXがバグを起こします
        rt->Release();
    }

    // 部屋を空室マークにする（デストラクタは呼ばれない）
    m_renderTargetPool.Destroy(handle);
}

// ============================================================================
// アニメーション台本（JSON）のロード処理
// ============================================================================
const AnimSequence* ResourceManager::LoadAnimSequence(const char* filename)
{
    if (!filename || filename[0] == '\0') return nullptr;

    // 1. パスをハッシュ化して高速検索
    uint32_t hash = CCL::Utils::HashString(filename);

    auto it = m_animSequenceRegistry.find(hash);
    if (it != m_animSequenceRegistry.end()) {
        AnimSequence* seq = m_animSequencePool.Get(it->second);
        if (seq) {
            return seq; // すでにロード済みならそれを返す（超高速）
        }
    }

    // 2. ファイルを開く
    std::ifstream is(filename);
    if (!is.is_open()) return nullptr;

    // 3. プールに新しい台本の部屋（メモリ）を確保
    AnimSequenceHandle handle = m_animSequencePool.Create();
    AnimSequence* seq = m_animSequencePool.Get(handle);

    // 4. JSONからデータをデシリアライズ
    try {
        cereal::JSONInputArchive archive(is);
        archive(*seq);
    }
    catch (...) {
        // ロードに失敗した場合は確保した部屋を破棄してエラー回避
        m_animSequencePool.Destroy(handle);
        return nullptr;
    }

    // 5. キャッシュに登録
    m_animSequenceRegistry[hash] = handle;
#ifdef _DEBUG
    m_debugNameRegistry[hash] = filename;
#endif

    return seq;
}


void ResourceManager::DrawDebugGUI()
{
    if (ImGui::CollapsingHeader("Resource", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Models:");
        for (auto &pair : m_modelRegistry) {
            bool isValid = (m_modelPool.Get(pair.second) != nullptr);
            // ハッシュ値と、もしあれば元の名前を表示
#ifdef _DEBUG
            std::string           name = m_debugNameRegistry[pair.first];
            std::filesystem::path filepath(name);
            ImGui::Text("  [%s] : %s (Hash:%u)",
                isValid ? "Active" : "Dead",
                filepath.filename().u8string().c_str(),
                pair.first);
#else
            ImGui::Text("  [%s] : Hash:%u", isValid ? "Active" : "Dead", pair.first);
#endif
        }

        ImGui::Separator();

        ImGui::Text("Textures:");
        for (auto &pair : m_textureRegistry) {
            bool isValid = (m_texturePool.Get(pair.second) != nullptr);
#ifdef _DEBUG
            std::string           name = m_debugNameRegistry[pair.first];
            std::filesystem::path filepath(name);
            ImGui::Text(
                "  [%s] : %s", isValid ? "Active" : "Dead", filepath.filename().u8string().c_str());
#else
            ImGui::Text("  [%s] : Hash:%u", isValid ? "Active" : "Dead", pair.first);
#endif
        }
    }
}