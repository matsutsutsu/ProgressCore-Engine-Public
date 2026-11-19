#pragma once
// ===========================================================================
// 概要: テクスチャ・モデル・マテリアル・アニメーション・レンダーターゲット・粒子バッファを
//       種類ごとのプールで持ち、ハンドルで貸し出すシングルトン
//
// 責務:
//   - ファイルから読み込み、パスのハッシュでキャッシュして二重に読まない
//   - ハンドルから実体を返し、破棄する
//   - モデルのパスをハッシュから引けるようにする（シリアライズ・インスペクタ用）
//
// 備考: シーンの非同期読み込みでワーカースレッドからも呼ばれるので、レジストリとプールの操作は
//       m_mutex で守る。重いファイル読み込みはロックの外で行う（中で LoadTexture を呼ぶと
//       同じ m_mutex を取り直してデッドロックする）
// ===========================================================================
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "Engine/Core/Math/StringHash.h"
#include "ResourcePool.h"
#include "ResourceTypes.h"

#include "ModelResource.h"
#include "Model.h"
#include "MaterialData.h"
#include "Engine/Graphics/Components/Particle/GPUParticleBuffer.h"
#include "Engine/Graphics/RHI/RenderTarget.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Core/Math/CurveData.h"

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

    // 全リソースを解放する（シャットダウンとデバイスロストからの復帰で使う）
    void ClearAll();

    // ============================================================
    // テクスチャ
    // ============================================================
    // パスで読み込む。読み込み済みならキャッシュのハンドルを返す
    TextureHandle LoadTexture(const char *filename);
    // ファイルを通さずに作った SRV を登録する（キャッシュしない）
    TextureHandle             RegisterTexture(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv);
    ID3D11ShaderResourceView *GetTexture(TextureHandle handle);
    // 解放する。キャッシュの表は残るが、次の LoadTexture で世代の違いを見て読み直す
    void UnloadTexture(TextureHandle handle);
    // メモリ分析ウィンドウ用
    const CCL::ResourcePool<TextureResource> &GetTexturePool() const
    {
        return m_texturePool;
    }

    // ============================================================
    // モデルの共有データ（ModelResource）
    // ============================================================
    ModelHandle    LoadModelResource(const char *filename);
    ModelResource *GetModel(ModelHandle handle);
    // 解放する。このデータを参照している Model が残っていないときだけ呼ぶこと
    void UnloadModel(ModelHandle handle);

    // パスのハッシュからパスを引く（ModelComponent のシリアライズ・インスペクタ用）。なければ空文字
    std::string GetAssetPath(uint32_t hash);

    // filename のモデルを読み込むときに LOD も作るよう予約する（ModelComponent::SetModel がインスタンスを作る前に呼ぶ）
    // 読み込み済みのモデルには効かない。描画に使われているバッファを後から書き換えないため
    void RequestModelLod(const char *filename);

    // ============================================================
    // モデルのインスタンス（Model）
    // ============================================================
    ModelInstanceHandle CreateModelInstance(const char *filename);
    // GetAssetPath と同じ表でパスを引いて作る（ModelComponent のコピー用）
    ModelInstanceHandle CreateModelInstanceFromHash(uint32_t hash);
    Model              *GetModelInstance(ModelInstanceHandle handle);
    void                UnloadModelInstance(ModelInstanceHandle handle);

    // ============================================================
    // マテリアル
    // ============================================================
    MaterialHandle CreateMaterial();
    // source の値を写した新しいマテリアルを作る（source が無効なら既定値）
    MaterialHandle CloneMaterial(MaterialHandle source);
    MaterialData  *GetMaterial(MaterialHandle handle);
    void           UnloadMaterial(MaterialHandle handle);
    // .mat.json を読み、中のテクスチャも読み込む。毎回新しいハンドルを作る（キャッシュしない）
    MaterialHandle LoadMaterialAsset(const std::string &virtualPath);
    bool           SaveMaterialAsset(MaterialHandle handle, const std::string &virtualPath);

    // ============================================================
    // アニメーション
    // ============================================================
    const AnimSequence *LoadAnimSequence(const char *filename);
    // キャッシュ済みのシーケンスを読み直し、同じインスタンスに上書きする（エディタで保存した直後に反映するため）
    // 作り直さないので、指しているポインタは有効なまま。キャッシュになければ読み込み、読めなければ今の内容を返す
    const AnimSequence *ReloadAnimSequence(const char *filename);

    // アニメグラフを読み、中のステートが指すシーケンスも読み込んで結び付ける。同じファイルは 1 つの実体を共有する
    const AnimStateGraph *LoadAnimGraph(const char *filename);
    // キャッシュ済みのグラフを読み直し、同じ実体に上書きする（インスペクタの「グラフ再読込」用）
    // 作り直さないので、指しているポインタは有効なまま。キャッシュになければ読み込み、読めなければ今の内容を返す
    const AnimStateGraph *ReloadAnimGraph(const char *filename);

    // ============================================================
    // レンダーターゲット
    // ============================================================
    RenderTargetHandle CreateRenderTarget(int width, int height, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);
    // CreateRenderTarget と同じだが、デバイスを指定でき、作れなかったら name をログに出して無効ハンドルを返す
    RenderTargetHandle LoadRenderTarget(const std::string &name, ID3D11Device *device, UINT width, UINT height, DXGI_FORMAT format);
    RenderTarget      *GetRenderTarget(RenderTargetHandle handle);
    void               UnloadRenderTarget(RenderTargetHandle handle);

    // ============================================================
    // GPU パーティクルのバッファ
    // ============================================================
    ParticleBufferHandle  CreateParticleBuffer(ID3D11Device *device, int maxParticles);
    GPUParticleBufferSet *GetParticleBuffer(ParticleBufferHandle handle);
    void                  UnloadParticleBuffer(ParticleBufferHandle handle);

private:
    // グラフ内の全ステートのクリップとブレンドスペースの点を、ファイルパスから結び付ける（サブマシンの子も）
    // 中で LoadAnimSequence が m_mutex を取るので、ロックを持たずに呼ぶ
    void BindGraphResources(AnimStateGraph &graph);

    // --- プール ---
    CCL::ResourcePool<TextureResource>      m_texturePool;
    CCL::ResourcePool<ModelResource>        m_modelPool;
    CCL::ResourcePool<Model>                m_modelInstancePool;
    CCL::ResourcePool<MaterialData>         m_materialPool;
    CCL::ResourcePool<AnimSequence>         m_animSequencePool;
    CCL::ResourcePool<AnimStateGraph>       m_animGraphPool;
    CCL::ResourcePool<RenderTarget>         m_renderTargetPool;
    CCL::ResourcePool<GPUParticleBufferSet> m_particleBufferPool;

    // --- パスのハッシュ → ハンドル（二重に読み込まないためのキャッシュ）---
    std::unordered_map<uint32_t, TextureHandle>          m_textureRegistry;
    std::unordered_map<uint32_t, ModelHandle>            m_modelRegistry;
    std::unordered_map<uint32_t, AnimSequenceHandle>     m_animSequenceRegistry;
    std::unordered_map<uint32_t, AnimGraphHandle>        m_animGraphRegistry;
    std::unordered_map<uint32_t, std::string>            m_assetPathRegistry; // パスのハッシュ → パス
    std::unordered_set<uint32_t>                         m_lodRequests;       // LOD を作るモデルのパスのハッシュ

    std::mutex m_mutex; // レジストリとプールを守る
};