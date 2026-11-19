#pragma once
// ===========================================================================
// 概要: 不透明のメッシュを先に1回描き、深度とビュー空間の法線を書き出すパス
//
// 責務:
//   - 不透明のコマンドを法線用のシェーダーで描き、NormalBuffer（SSAO が t6 で読む）に書く
//   - 深度も書いて、後の OpaquePass で early-Z が効くようにする
//   - メッシュごとに視錐台カリングし、見えるインスタンスの列を RenderQueue に残す
//     （OpaquePass は同じ列を使い、カリングをやり直さない）
//   - LOD のあるメッシュは、インスタンスごとに段を選んで段の順に並べ、段ごとに描く
//
// 備考: OpaquePass より前に置く。LOD の段はここで決め、OpaquePass はそれに従う
//       バッチの区切り方とカリングは OpaquePass と同じにする。違うと残した列が合わなくなる
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/Core/InstanceBuffer.h"
#include "Engine/Graphics/Renderer/Core/MeshLod.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/RHI/NormalBuffer.h"

#include <DirectXCollision.h>
#include <vector>

struct RenderKey;
struct RenderCommand;
struct MaterialData;
class Shader;

class NormalPrepass : public IRenderPass
{
public:
    void Initialize(const RenderServices &services) override;
    void Execute(const RenderFrameContext &ctx) override;
    void OnResize(uint32_t width, uint32_t height) override;

    ID3D11ShaderResourceView *GetNormalSRV() const noexcept { return m_normalBuffer.GetSRV(); }

private:
    // 並べたキーの列のうち、同じバッチキーが続く区間
    // count がバッチのインスタンスの数（同じモデルを置いた数）になる
    struct BatchRange
    {
        size_t start = 0;
        size_t count = 0;
    };

    // 法線バッファの SRV を Graphics に渡す（MakeFrameContext が ctx.sceneNormalSRV に入れる）。SRV を作り直した直後に呼ぶ
    void PublishNormalSRV();

    // 法線バッファを描画先にし、PSO・定数バッファ・シェーダーなど全バッチで共通のものをバインドする
    void BindCommonResources(ID3D11DeviceContext *dc, const RenderFrameContext &ctx, Shader &shader);

    // cursor から同じバッチキーが続く区間を切り出し、cursor をその次へ進める
    static BatchRange ConsumeBatch(const std::vector<RenderKey> &sortKeys, size_t &cursor);

    // バッチのモデルの不透明なメッシュを描き、見えるインスタンスの列を OpaquePass 用に残す
    void DrawBatch(const RenderFrameContext &ctx, const BatchRange &batch);

    // meshIdx 番目のメッシュに使う上書きのマテリアル。なければ nullptr
    const MaterialData *ResolveOverrideMaterial(const RenderCommand &cmd, size_t meshIdx) const;

    // バッチのインスタンスのうち、見えるものだけを m_instanceScratch に集め、LOD の段の順に並べる
    // 戻り値は段ごとの数。スキンメッシュはカリングせず、すべて LOD0 にする
    MeshLod::LevelCounts BuildVisibleInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh,
                                               const BatchRange &batch, const DirectX::BoundingBox &localAABB,
                                               bool isSkinned);

    // 頂点・インデックスバッファとトポロジを設定する
    static void BindMeshIA(ID3D11DeviceContext *dc, const Model::Mesh &mesh);

    // m_instanceScratch を LOD の段ごとに、インスタンスバッファに入る数ずつに分けて描く
    void DrawInstancedBatched(ID3D11DeviceContext *dc, const Model::Mesh &mesh, const MeshLod::LevelCounts &levelCounts);

    // スキンメッシュを1体ずつ描く（ボーン行列が1体ごとに違い、インスタンシングでまとめられない）
    void DrawSkinnedInstances(ID3D11DeviceContext *dc, const Model::Mesh &mesh, size_t meshIdx,
                              const BatchRange &batch);

    PipelineState             m_pso;             // 深度を書く・裏面カリング
    InstanceBuffer            m_instanceBuffer;  // 見えるインスタンスの行列を頂点シェーダーへ渡す
    NormalBuffer              m_normalBuffer;    // ビュー空間の法線の書き出し先
    std::vector<InstanceData> m_instanceScratch; // メッシュごとに使い回す作業用の配列
    std::vector<InstanceData> m_lodSortTemp;     // LOD の段の順に並べ替えるときの作業用の配列
    MeshLod::LevelCounts      m_lodTotals{};     // LOD のあるメッシュの段ごとの数（Tracy のグラフ用）
};
