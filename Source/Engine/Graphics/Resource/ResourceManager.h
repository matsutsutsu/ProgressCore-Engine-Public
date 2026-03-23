#pragma once

#include "ModelResource.h"
#include "MaterialData.h"
#include "ResourcePool.h"
#include "Engine/Graphics/Core/RenderTarget.h"
#include <d3d11.h>
#include <unordered_map> // mapの代わりにこれを使う
#include "Engine/Core/Math/StringHash.h"
#include <string>
#include <wrl/client.h>
#include <mutex>
#include "Engine/GamePlay/Animation/AnimSequence.h"

// コンピュートシェーダー用のバッファ群をまとめた構造体
struct GpuParticleBufferSet {
    ID3D11Buffer* particleBuffer = nullptr;
    ID3D11UnorderedAccessView* particleUAV    = nullptr;
    ID3D11ShaderResourceView* particleSRV    = nullptr;
    ID3D11Buffer* constantBuffer = nullptr;

    // プールがDestroyを呼んだ時に自動でReleaseさせる
    ~GpuParticleBufferSet() {
        if (particleBuffer) { particleBuffer->Release(); particleBuffer = nullptr; }
        if (particleUAV)    { particleUAV->Release(); particleUAV = nullptr; }
        if (particleSRV)    { particleSRV->Release(); particleSRV = nullptr; }
        if (constantBuffer) { constantBuffer->Release(); constantBuffer = nullptr; }
    }
};


// ComPtrをプールするための型エイリアス
using TextureResource = Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;

// 各種ハンドル定義
using ModelHandle   = CCL::Handle<ModelResource>;
using TextureHandle = CCL::Handle<TextureResource>;
using MaterialHandle = CCL::Handle<MaterialData>;
using ParticleBufferHandle = CCL::Handle<GpuParticleBufferSet>;

// レンダーターゲットのハンドル定義
using RenderTargetHandle = CCL::Handle<RenderTarget>;

// アニメーション用のハンドル
using AnimSequenceHandle = CCL::Handle<AnimSequence>;

class ResourceManager {
  private:
    ResourceManager() {}
    ~ResourceManager() {}

  public:
    static ResourceManager &Instance()
    {
        static ResourceManager instance;
        return instance;
    }

    // --- ロード窓口 ---
    // 戻り値は生ポインタやスマートポインタではなく、必ずハンドルを返す
    ModelHandle   LoadModelResource(const char *filename);
    TextureHandle LoadTexture(const char *filename);

    TextureHandle RegisterTexture(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv);

    // 動的生成モデル（PrimitiveやParticleなど）のための空リソース確保
    ModelHandle AllocateEmptyModelResource();

    // --- アクセス窓口 ---
    // 描画システム（System層）が、実際に描画コマンドを発行する瞬間のみ呼び出す
    ModelResource            *GetModel(ModelHandle handle);
    ID3D11ShaderResourceView *GetTexture(TextureHandle handle);

    // --- 破棄窓口 ---
    void UnloadModel(ModelHandle handle);
    void UnloadTexture(TextureHandle handle);

    // --- マテリアル窓口 ---
    MaterialHandle CreateMaterial();
    
    // 既存のマテリアルをコピーして新しいマテリアル（個別の色変更用など）を作る
    MaterialHandle CloneMaterial(MaterialHandle source);

    MaterialData *GetMaterial(MaterialHandle handle);

    void UnloadMaterial(MaterialHandle handle);

    // --- パーティクルバッファ窓口 ---
    ParticleBufferHandle CreateParticleBuffer() { return m_particleBufferPool.Create(); }
    GpuParticleBufferSet* GetParticleBuffer(ParticleBufferHandle handle) { return m_particleBufferPool.Get(handle); }
    void UnloadParticleBuffer(ParticleBufferHandle handle) { m_particleBufferPool.Destroy(handle); }


    // --- レンダーターゲット窓口 ---
    // 指定したサイズとフォーマットでRTを作成する
    RenderTargetHandle CreateRenderTarget(int width, int height, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);
    
    // ハンドルからポインタを取得
    RenderTarget* GetRenderTarget(RenderTargetHandle handle);
    

    // アニメーション台本のロード窓口
    // AnimatorComponent が直接ポインタを参照する設計のため、constポインタを返します
    const AnimSequence* LoadAnimSequence(const char* filename);

    // 不要になったら破棄
    void UnloadRenderTarget(RenderTargetHandle handle);




    void DrawDebugGUI();

  private:
    // メモリを連続配置で管理する実体プール
    CCL::ResourcePool<ModelResource>   m_modelPool;
    CCL::ResourcePool<TextureResource> m_texturePool;
    CCL::ResourcePool<MaterialData>    m_materialPool; // マテリアル専用アパート
    CCL::ResourcePool<GpuParticleBufferSet> m_particleBufferPool; // パーティクル専用アパート
    CCL::ResourcePool<RenderTarget> m_renderTargetPool;     // レンダーターゲット専用アパート

    // アニメーション台本専用のアパート（プール）
    CCL::ResourcePool<AnimSequence>    m_animSequencePool;

    // パスからハンドルを逆引きするためのレジストリ（キャッシュ）
    // unordered_map はハッシュテーブルなので、検索が O(1) で爆速です。
    std::unordered_map<uint32_t, ModelHandle>   m_modelRegistry;
    std::unordered_map<uint32_t, TextureHandle> m_textureRegistry;

    // アニメーション台本のキャッシュ
    std::unordered_map<uint32_t, AnimSequenceHandle> m_animSequenceRegistry;

    // 複数スレッドからの同時アクセスを防ぐ鍵
    std::mutex _mutex;

    // デバッグ表示用 (ハッシュ値 -> 元のファイルパス)
    // これがないとImGuiでファイル名が表示できなくなります
#ifdef _DEBUG
    std::unordered_map<uint32_t, std::string> m_debugNameRegistry;
#endif

};