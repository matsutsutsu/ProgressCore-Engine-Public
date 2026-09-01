#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h"

#include <cstdint>
#include <vector>

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
 *  Services（永続）と FrameContext（揮発）を分離した新パイプライン規約に準拠。
 *  - Initialize : m_services の保持 + PSO / InstanceBuffer の生成
 *  - Execute    : ctx（フレームデータ）のみ受け取り、旧 Shader API には
 *                 MakeLegacyContext で構築した互換ビューを渡す（移行期）
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
    PipelineState  m_psoOpaque;
    InstanceBuffer m_instanceBuffer;

    // インスタンスデータ構築用のスクラッチ領域。メッシュ単位で再利用する。
    // 関数 static を避け、可読性と将来のマルチスレッド化に配慮してメンバ化。
    std::vector<InstanceData> m_instanceScratch;

    // フラスタムカリングの統計（デバッグ確認用。ImGui やウォッチで参照）。
    uint32_t m_stat_drawnMeshes  = 0;
    uint32_t m_stat_culledMeshes = 0;
};