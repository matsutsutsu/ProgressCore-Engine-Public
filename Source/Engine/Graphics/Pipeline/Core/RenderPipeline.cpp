#pragma once
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "Engine/Graphics/Pipeline/Core/RenderPipeline.h"
#include "Engine/Graphics/RHI/RenderContext.h"

/**
 * @brief 複数の RenderPass を順序付きで実行するパイプライン。
 * @details
 *  - パスの追加順 = 描画順
 *  - 各パスは独立しており、追加/削除がコンパイル時の影響を受けない
 *  - Debug ビルドでは GPU マーカーを自動挿入（Pix/RenderDoc から識別可能）
 *
 *  典型的な使い方：
 *  @code
 *    RenderPipeline pipeline;
 *    pipeline.Initialize(services);
 *    pipeline.AddPass<ShadowMapPass>();
 *    pipeline.AddPass<OpaqueGeometryPass>();
 *    pipeline.AddPass<TransparentPass>();
 *    pipeline.AddPass<PostProcessPass>();
 *
 *    // 毎フレーム
 *    pipeline.Execute(frameCtx);
 *  @endcode
 */
class RenderPipeline
{
public:
    RenderPipeline() = default;
    ~RenderPipeline() = default;

    RenderPipeline(const RenderPipeline&) = delete;
    RenderPipeline& operator=(const RenderPipeline&) = delete;

    /**
     * @brief パイプラインを初期化する。
     * @details
     *  AddPass で渡すサービス群をここで保持する。
     *  以降の AddPass / Execute / OnResize より前に必ず 1 回呼ぶこと。
     */
    void Initialize(const RenderServices& services);

    /**
     * @brief 全パスを Shutdown して破棄する。
     * @details パスは追加と逆順で破棄される（依存関係対策）。
     */
    void Shutdown();

    /**
     * @brief パスを末尾に追加し、その場で Initialize まで実行する。
     * @tparam T IRenderPass の派生型
     * @tparam Args T のコンストラクタ引数
     * @return 追加されたパスへの非所有ポインタ（所有権は RenderPipeline）
     *
     * @note 戻り値はパスのプロパティ調整等に使う。寿命管理は RenderPipeline が行う。
     */
    template <typename T, typename... Args>
    T* AddPass(Args&&... args)
    {
        static_assert(std::is_base_of_v<IRenderPass, T>,
            "T must derive from IRenderPass");

        auto pass = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = pass.get();
        pass->Initialize(_services);
        _passes.push_back(std::move(pass));
        return raw;
    }

    /**
     * @brief 登録された全パスを順番に実行する。
     * @details
     *  - enabled == false のパスはスキップ
     *  - Debug ビルドでは ID3DUserDefinedAnnotation で各パスを Begin/End マーカーで囲む
     */
    void Execute(const RenderFrameContext& ctx);

    /**
     * @brief ウィンドウリサイズを全パスに伝播する。
     * @details 内部 RT を持つパス（SSAO 等）が再生成を行う。
     */
    void OnResize(uint32_t width, uint32_t height);

    /// 登録されているパス数を返す（デバッグ・テスト用）。
    size_t GetPassCount() const noexcept { return _passes.size(); }

private:
    RenderServices                            _services{};
    std::vector<std::unique_ptr<IRenderPass>> _passes;
};