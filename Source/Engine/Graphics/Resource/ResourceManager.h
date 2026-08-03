#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <mutex>
#include <unordered_map>

#include "Engine/Core/Math/StringHash.h"
#include "ResourcePool.h"
// 新しく作った型定義専用ヘッダーを読み込む
#include "ResourceTypes.h"

// --- 依存するリソースの先行宣言 & インクルード ---
#include "ModelResource.h"
#include "Model.h"
#include "MaterialData.h"
#include "Engine/Graphics/Components/Particle/GPUParticleBuffer.h"
#include "Engine/Graphics/RHI/RenderTarget.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Core/Math/CurveData.h"

// ============================================================================
// [ResourceManager]
// ============================================================================
class ResourceManager
{
  private:
    ResourceManager()  = default;
    ~ResourceManager() = default;

  public:
    static ResourceManager &Instance()
    {
        static ResourceManager instance;
        return instance;
    }

    // 全リソースの強制解放
    void ClearAll();

    // ---------------------------------------------------------
    // [1] Texture (Static)
    // ---------------------------------------------------------
    TextureHandle                               LoadTexture(const char *filename);
    TextureHandle                               RegisterTexture(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv);
    ID3D11ShaderResourceView                   *GetTexture(TextureHandle handle);
    size_t                                      CalculateTotalVramUsage() const;
    std::vector<std::pair<std::string, size_t>> GetSortedTextureList() const;
    void                                        UnloadTexture(TextureHandle handle);
    const CCL::ResourcePool<TextureResource>   &GetTexturePool() const
    {
        return m_texturePool;
    }

    // ---------------------------------------------------------
    // [2] Model Resource (Static Data)
    // ---------------------------------------------------------
    ModelHandle    LoadModelResource(const char *filename);
    ModelHandle    AllocateEmptyModelResource();
    ModelResource *GetModel(ModelHandle handle);
    void           UnloadModel(ModelHandle handle);

    // ハッシュからパス文字列を逆引きする機能（GUI/シリアライズ用）
    std::string GetAssetPath(uint32_t hash);

    // ---------------------------------------------------------
    // [3] Model Instance (Dynamic Data)
    // ---------------------------------------------------------
    ModelInstanceHandle CreateModelInstance(const char *filename);
    ModelInstanceHandle CreateModelInstanceFromHash(uint32_t hash);
    Model              *GetModelInstance(ModelInstanceHandle handle);
    void                UnloadModelInstance(ModelInstanceHandle handle);

    // ---------------------------------------------------------
    // [4] Material
    // ---------------------------------------------------------
    MaterialHandle      CreateMaterial();
    MaterialHandle      CloneMaterial(MaterialHandle source);
    MaterialData       *GetMaterial(MaterialHandle handle);
    void                UnloadMaterial(MaterialHandle handle);
    MaterialHandle      LoadMaterialAsset(const std::string &virtualPath);
    bool                SaveMaterialAsset(MaterialHandle handle, const std::string &virtualPath);
    ModelInstanceHandle RegisterModelInstance(std::shared_ptr<Model> model);

    // ---------------------------------------------------------
    // [5] Animation Sequence
    // ---------------------------------------------------------
    const AnimSequence *LoadAnimSequence(const char *filename);
    // 既にキャッシュ済みのシーケンスをディスクから読み直し、同じインスタンスへ上書きする。
    // 破棄→再生成しないため、これを指している currentSequence ポインタは有効なまま
    // 新しい内容を参照できる（エディタ保存直後の即時反映用）。
    const AnimSequence *ReloadAnimSequence(const char *filename);
    const CurveData    *LoadAnimCurve(const char *filepath);

    // ---------------------------------------------------------
    // [6] Render Target (GPU Resource)
    // ---------------------------------------------------------
    RenderTargetHandle CreateRenderTarget(int width, int height, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);
    // 新しい RenderTarget を生成し、プールに登録してハンドルを返す
    RenderTargetHandle LoadRenderTarget(const std::string &name, ID3D11Device *device, UINT width, UINT height, DXGI_FORMAT format);
    RenderTarget      *GetRenderTarget(RenderTargetHandle handle);
    void               UnloadRenderTarget(RenderTargetHandle handle);

    // ---------------------------------------------------------
    // [7] GPU Particle Buffer
    // ---------------------------------------------------------
    ParticleBufferHandle  CreateParticleBuffer(ID3D11Device *device, int maxParticles);
    GPUParticleBufferSet *GetParticleBuffer(ParticleBufferHandle handle);
    void                  UnloadParticleBuffer(ParticleBufferHandle handle);

    // ---------------------------------------------------------
    // [Debug]
    // ---------------------------------------------------------
    void DrawDebugGUI();

  private:
    // --- Resource Pools (連続メモリ配置) ---
    CCL::ResourcePool<TextureResource>      m_texturePool;
    CCL::ResourcePool<ModelResource>        m_modelPool;
    CCL::ResourcePool<Model>                m_modelInstancePool;
    CCL::ResourcePool<MaterialData>         m_materialPool;
    CCL::ResourcePool<AnimSequence>         m_animSequencePool;
    CCL::ResourcePool<RenderTarget>         m_renderTargetPool;
    CCL::ResourcePool<GPUParticleBufferSet> m_particleBufferPool;
    CCL::ResourcePool<CurveData>            m_animCurvePool;

    // --- Registries (重複ロード防止のキャッシュ) ---
    std::unordered_map<uint32_t, TextureHandle>          m_textureRegistry;
    std::unordered_map<uint32_t, ModelHandle>            m_modelRegistry;
    std::unordered_map<uint32_t, AnimSequenceHandle>     m_animSequenceRegistry;
    std::unordered_map<uint32_t, std::string>            m_assetPathRegistry;
    std::unordered_map<uint32_t, CCL::Handle<CurveData>> m_animCurveRegistry;

    // スレッドセーフ用ミューテックス
    std::mutex m_mutex;

#ifdef _DEBUG
    std::unordered_map<uint32_t, std::string> m_debugNameRegistry;
#endif
};