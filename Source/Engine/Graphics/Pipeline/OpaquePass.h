#pragma once
#include "IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h"

#include <vector>

/**
 * @brief 不透明ジオメトリを描画するレンダーパス。
 * @details
 *  RenderQueue から不透明コマンドを取り出し、シェーダー/バッチ単位で
 *  まとめてインスタンシング描画する。半透明と判定されたメッシュは
 *  TransparentPass 用の遅延キューへ転送する。
 *
 *  Services（永続）と FrameContext（揮発）を分離した新パイプライン規約に準拠。
 *  - Initialize : _services の保持 + PSO / InstanceBuffer の生成
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
    void Initialize(const RenderServices& services) override;

    /**
     * @brief 不透明バッチを描画し、半透明メッシュを TransparentPass へ転送する。
     * @param ctx このフレームのみ有効な揮発コンテキスト。
     */
    void Execute(const RenderFrameContext& ctx) override;

    /// GPU マーカー（Pix/RenderDoc）用の表示名。
    const char* GetName() const override { return "OpaquePass"; }

private:
    PipelineState  _psoOpaque;
    InstanceBuffer _instanceBuffer;

    /// インスタンスデータ構築用のスクラッチ領域。メッシュ単位で再利用する。
    /// 関数 static を避け、可読性と将来のマルチスレッド化に配慮してメンバ化。
    std::vector<InstanceData> _instanceScratch;
};