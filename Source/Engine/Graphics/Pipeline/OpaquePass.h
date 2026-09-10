#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h"
#include "Engine/Graphics/Resource/Model.h" // ヘルパの引数に Model::Mesh を使う（入れ子型のため前方宣言不可）

#include <DirectXCollision.h>
#include <cstdint>
#include <vector>

struct RenderKey;
struct RenderCommand;
struct MaterialData;
class Shader;
class IMaterialShader;

/**
 * @brief 不透明ジオメトリを描画するレンダーパス。
 * @details
 *  RenderQueue から不透明コマンドを取り出し、シェーダー/バッチ単位で
 *  まとめてインスタンシング描画する。半透明と判定されたメッシュは
 *  TransparentPass 用の遅延キューへ転送する。
 *
 *  メッシュ単位フラスタムカリングを内蔵：非スキンドメッシュはインスタンス毎に
 *  ワールド空間 AABB を視錐台判定し、視界外インスタンスを描画から除外する。
 *  可視インスタンスが 0 のメッシュはドローコールごとスキップする。
 *
 *  Execute は「1 フレームの手順書」に徹し、各工程は private ヘルパへ委譲する。
 *  読む順番は Execute → DrawBatch → 各ヘルパ、の順。
 */
class OpaquePass : public IRenderPass
{
  public:
    /**
     * @brief PSO とインスタンスバッファを生成する。
     * @param services 永続サービス群（device / shaders / resources 等が
     *        呼び出し側で全て埋まっている必要がある）。
     */
    void Initialize(const RenderServices &services) override;

    /**
     * @brief 不透明バッチを描画し、半透明メッシュを TransparentPass へ転送する。
     * @param ctx このフレームのみ有効な揮発コンテキスト。
     */
    void Execute(const RenderFrameContext &ctx) override;

    // GPU マーカー（Pix/RenderDoc）用の表示名。
    const char *GetName() const override
    {
        return "OpaquePass";
    }

    // 直近フレームで実際に描画した（可視インスタンスを持つ）メッシュ数。
    uint32_t GetDrawnMeshCount() const
    {
        return m_stat_drawnMeshes;
    }
    // 直近フレームでフラスタムカリングにより丸ごとスキップしたメッシュ数。
    uint32_t GetCulledMeshCount() const
    {
        return m_stat_culledMeshes;
    }

  private:
    // ソート済みキー列のうち、同一バッチキーが連続している区間。
    // count がそのままバッチ内のインスタンス数（＝同じモデルの配置数）になる。
    struct BatchRange
    {
        size_t start = 0;
        size_t count = 0;
    };

    // --- Execute から順に呼ばれるヘルパ群 -------------------------------

    // シャドウマップ・サンプラー・既定 PSO など、全バッチ共通のバインドを行う。
    void BindCommonResources(ID3D11DeviceContext *dc);

    // cursor 位置から同一バッチキーが続く区間を切り出し、cursor をその先へ進める。
    static BatchRange ConsumeBatch(const std::vector<RenderKey> &sortKeys, size_t &cursor);

    // シェーダー固有 PSO があれば適用する。lastPso は実際に適用中のものを指す。
    void ApplyShaderPso(ID3D11DeviceContext *dc, const Shader &shader, const PipelineState *&lastPso);

    // 1 バッチが指すモデルの全メッシュを処理する（描画 or 半透明キューへの転送）。
    void DrawBatch(const RenderFrameContext &ctx, const BatchRange &batch, const Shader &shader,
                   IMaterialShader &matShader);

    // meshIdx 番目のメッシュに適用する override マテリアル。無ければ nullptr。
    MaterialData *ResolveOverrideMaterial(const RenderCommand &cmd, size_t meshIdx) const;

    // 半透明メッシュを、距離ソートが必要な TransparentPass の遅延キューへ送る。
    void ForwardMeshToTransparentQueue(const RenderFrameContext &ctx, const Model::Mesh &mesh, size_t meshIdx,
                                       const BatchRange &batch, uint32_t shaderHash,
                                       const DirectX::BoundingBox &localAABB, bool isSkinned);

    // バッチ内のインスタンスから可視ぶんだけを m_instanceScratch に集める。
    void BuildVisibleInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh, const BatchRange &batch,
                               const DirectX::BoundingBox &localAABB, bool isSkinned);

    // 頂点／インデックスバッファとトポロジをセットする。
    static void BindMeshIA(ID3D11DeviceContext *dc, const Model::Mesh &mesh);

    // m_instanceScratch をバッファ容量ぶんずつ分割してインスタンシング描画する。
    void DrawInstancedBatched(ID3D11DeviceContext *dc, const Model::Mesh &mesh);

    // スキンドメッシュを 1 体ずつ描画する（ボーン定数がインスタンス間で共有できないため）。
    void DrawSkinnedInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh, size_t meshIdx,
                              const BatchRange &batch);

    // --- 状態 -----------------------------------------------------------

    PipelineState  m_psoOpaque;
    InstanceBuffer m_instanceBuffer;

    // インスタンスデータ構築用のスクラッチ領域。メッシュ単位で再利用する。
    // 関数 static を避け、可読性と将来のマルチスレッド化に配慮してメンバ化。
    std::vector<InstanceData> m_instanceScratch;

    // フラスタムカリングの統計（デバッグ確認用。ImGui やウォッチで参照）。
    uint32_t m_stat_drawnMeshes  = 0;
    uint32_t m_stat_culledMeshes = 0;
};
