#pragma once
#include <cstdint>
#include "Engine/Graphics/RHI/RenderContext.h"

// 各パスをTracyで追跡できるようにインクルード
#include "tracy/Tracy.hpp"



/**
 * @brief レンダリングパスの抽象基底インターフェース。
 * @details
 *  1 つの描画段階（影生成 / 不透明 / 半透明 / ポストエフェクト等）を表す。
 *  RenderPipeline から登録順に Execute される。
 *
 *  設計の核：Services（永続）と FrameContext（揮発）を明確に分離。
 *  - Initialize で受け取った Services は基底クラスが保持する → 派生クラスは _services 経由で参照
 *  - Execute は FrameContext のみ受け取る → フレーム毎データが明示的
 */
class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    /**
     * @brief 初期化。Services を保持し、必要に応じて GPU リソースを生成する。
     * @param services 永続サービス群。ポインタはエンジン終了まで有効。
     * @details
     *  デフォルト実装は _services の保持のみ。
     *  GPU リソースが必要な派生クラスはオーバーライドして基底を呼ぶこと：
     *  @code
     *    void OpaquePass::Initialize(const RenderServices& services) override {
     *        IRenderPass::Initialize(services);  // _services を保持
     *        _psoOpaque = PipelineState(PipelineStateDesc::DefaultOpaque());
     *        _instanceBuffer.Create(services.device, 8192);
     *    }
     *  @endcode
     */
    virtual void Initialize(const RenderServices& services)
    {
        _services = &services;
    }

    /**
     * @brief 破棄（エンジン終了時に 1 回だけ呼ばれる）。
     */
    virtual void Shutdown() {}

    /**
     * @brief 毎フレームの描画実行。
     * @param ctx フレームコンテキスト（このフレームのみ有効）
     */
    virtual void Execute(const RenderFrameContext& ctx) = 0;

    /**
     * @brief ウィンドウリサイズ時の内部 RT 再生成等。
     */
    virtual void OnResize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
    }

    /**
     * @brief デバッグ表示名。GPU マーカー（Pix/RenderDoc）に出力される。
     */
    virtual const char* GetName() const { return "UnnamedPass"; }

    /// パスを一時的に無効化するフラグ。エディタからのトグル用。
    bool enabled = true;

protected:
    /// Initialize 後にアクセス可能な永続サービスへのポインタ。
    const RenderServices* _services = nullptr;
};