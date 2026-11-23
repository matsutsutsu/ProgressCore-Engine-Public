#pragma once
// ===========================================================================
// 概要: 不透明のメッシュを、シェーダー・モデル・マテリアルごとにまとめてインスタンシングで描くパス
//
// 責務:
//   - RenderQueue の不透明のコマンドを並べ、同じキーが続く区間を1つのバッチにする
//   - スキンでないメッシュをインスタンスごとに視錐台カリングする（NormalPrepass の結果があれば使う）
//   - LOD のあるメッシュは、NormalPrepass が選んだ段ごとに描く
//   - alphaMode が Blend のメッシュは描かず、TransparentPass 用のキューへ回す
//
// 備考: スキンメッシュはアニメーションで元の AABB からはみ出すことがあるので、カリングしない
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/Core/InstanceBuffer.h"
#include "Engine/Graphics/Renderer/Core/MeshLod.h"
#include "Engine/Graphics/Resource/Model.h"

#include <DirectXCollision.h>
#include <cstdint>
#include <vector>

struct RenderKey;
struct RenderCommand;
struct MaterialData;
class IMaterialShader;

class OpaquePass : public IRenderPass
{
public:
    void Initialize(const RenderServices &services) override;
    void Execute(const RenderFrameContext &ctx) override;

    // 直近の Execute で描いた（見えるインスタンスが1つ以上あった）メッシュの数
    uint32_t GetDrawnMeshCount() const
    {
        return m_stat_drawnMeshes;
    }
    // 直近の Execute で、全インスタンスがカリングされて描かなかったメッシュの数
    uint32_t GetCulledMeshCount() const
    {
        return m_stat_culledMeshes;
    }

private:
    // 並べたキーの列のうち、同じバッチキーが続く区間
    // count がバッチのインスタンスの数（同じモデルを置いた数）になる
    struct BatchRange
    {
        size_t start = 0;
        size_t count = 0;
    };

    // シャドウマップ・サンプラー・PSO など、全バッチで共通のものをバインドする
    void BindCommonResources(ID3D11DeviceContext *dc);

    // cursor から同じバッチキーが続く区間を切り出し、cursor をその次へ進める
    static BatchRange ConsumeBatch(const std::vector<RenderKey> &sortKeys, size_t &cursor);

    // バッチのモデルの全メッシュを、描くか半透明のキューへ回す
    void DrawBatch(const RenderFrameContext &ctx, const BatchRange &batch, IMaterialShader &matShader);

    // meshIdx 番目のメッシュに使う上書きのマテリアル。なければ nullptr
    MaterialData *ResolveOverrideMaterial(const RenderCommand &cmd, size_t meshIdx) const;

    // 半透明のメッシュを、見えるインスタンスだけ TransparentPass 用のキューへ回す（距離も求める）
    void ForwardMeshToTransparentQueue(const RenderFrameContext &ctx, const Model::Mesh &mesh, size_t meshIdx,
                                       const BatchRange &batch, uint32_t shaderHash,
                                       const DirectX::BoundingBox &localAABB, bool isSkinned);

    // このメッシュの見えるインスタンスの列を決め、m_visibleData / m_visibleCount / m_visibleLevelCounts に入れる
    // NormalPrepass が同じ区切りで求めた列があればそれを使い、なければ自分で作る。スキンメッシュはカリングしない
    void ResolveVisibleInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh, const BatchRange &batch,
                                 size_t meshIdx, const DirectX::BoundingBox &localAABB, bool isSkinned);

    // バッチのインスタンスのうち、見えるものだけを m_instanceScratch に集め、LOD の段の順に並べる
    void BuildVisibleInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh, const BatchRange &batch,
                               const DirectX::BoundingBox &localAABB, bool isSkinned);

    // 頂点・インデックスバッファとトポロジを設定する
    static void BindMeshIA(ID3D11DeviceContext *dc, const Model::Mesh &mesh);

    // 見えるインスタンスを LOD の段ごとに、インスタンスバッファに入る数ずつに分けて描く
    void DrawInstancedBatched(ID3D11DeviceContext *dc, const Model::Mesh &mesh);

    // スキンメッシュを1体ずつ描く（ボーン行列が1体ごとに違い、インスタンシングでまとめられない）
    void DrawSkinnedInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh, size_t meshIdx,
                              const BatchRange &batch);

    PipelineState  m_psoOpaque;      // 深度を書く・裏面カリング
    InstanceBuffer m_instanceBuffer; // 見えるインスタンスの行列を頂点シェーダーへ渡す

    // 見えるインスタンスを集める作業用の配列（メッシュごとに使い回す）
    // NormalPrepass の結果を使えたメッシュでは使わない
    std::vector<InstanceData> m_instanceScratch;
    std::vector<InstanceData> m_lodSortTemp; // m_instanceScratch を LOD の段の順に並べ替えるときの作業用

    // 実際に描くインスタンスの列。RenderQueue の中の配列か m_instanceScratch を指す（所有しない）
    // RenderQueue のその配列に書き足すのは NormalPrepass だけなので、このパスが読んでいる間に
    // 配列が確保し直されて、このポインタが無効になることはない
    const InstanceData  *m_visibleData  = nullptr;
    size_t               m_visibleCount = 0;
    MeshLod::LevelCounts m_visibleLevelCounts{}; // m_visibleData の段ごとの数（列は段の順に並んでいる）

    // カリングの集計（毎フレーム 0 に戻す。デバッガで見る用で、今は表示している UI はない）
    uint32_t m_stat_drawnMeshes  = 0;
    uint32_t m_stat_culledMeshes = 0;
};
