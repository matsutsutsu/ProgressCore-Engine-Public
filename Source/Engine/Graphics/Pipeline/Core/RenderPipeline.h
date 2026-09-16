#pragma once
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include <typeinfo>
#include <string>

#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/RenderContext.h"

/**
 * @brief 複数の RenderPass を順序付きで実行するパイプライン。
 * @details
 *  - パスの追加順 = 描画順
 *  - Debug ビルドでは GPU マーカーを自動挿入（Pix/RenderDoc から識別可能）
 *
 *  このクラスは「登録されたパスを順に回す器」に徹し、具象パスの型を一切知らない。
 *  どのパスをどの順で積むかは描画全体を統括する Graphics 側の責務
 *  （Graphics::BuildDefaultRenderPasses）。これにより、パスを増減しても
 *  このファイルは再コンパイルされず、将来シーンごとに構成を差し替えることもできる。
 *
 *  典型的な使い方：
 *  @code
 *    RenderPipeline pipeline;
 *    pipeline.Initialize(services);          // ← 必ず AddPass より前に呼ぶ
 *    pipeline.AddPass<ShadowMapPass>();
 *    pipeline.AddPass<OpaqueGeometryPass>();
 *    pipeline.AddPass<PostProcessPass>();
 *    // 毎フレーム
 *    pipeline.Execute(frameCtx);
 *  @endcode
 */
class RenderPipeline
{
public:
    RenderPipeline()  = default;
    ~RenderPipeline() = default;

    RenderPipeline(const RenderPipeline &)            = delete;
    RenderPipeline &operator=(const RenderPipeline &) = delete;

    /**
     * @brief パイプラインを初期化する。AddPass より前に必ず1回呼ぶこと。
     * @details 以降の AddPass に渡すサービス群をここで保持する。
     */
    void Initialize(const RenderServices &services);

    /**
     * @brief 全パスを Shutdown して破棄する。パスは追加と逆順で破棄される。
     */
    void Shutdown();

    /**
     * @brief パスを末尾に追加し、その場で Initialize まで実行する。
     * @tparam T   IRenderPass の派生型
     * @tparam Args T のコンストラクタ引数
     * @return 追加されたパスへの非所有ポインタ（所有権は RenderPipeline）
     * @note  戻り値はパスのプロパティ調整等に使う。寿命管理は RenderPipeline が行う。
     */
    template <typename T, typename... Args>
    T *AddPass(Args &&...args)
    {
        static_assert(std::is_base_of_v<IRenderPass, T>,
                      "T must derive from IRenderPass");

        auto pass = std::make_unique<T>(std::forward<Args>(args)...);
        T   *raw  = pass.get();

        // =========================================================================
        // クラスの型情報(typeid)から名前を自動抽出し、キャッシュする
        // MSVCの場合、typeid(T).name() は "class ShadowPass" のように prefix が付くので除去する
        // =========================================================================
        std::string name = typeid(T).name();
        if (name.find("class ") == 0) name = name.substr(6);
        if (name.find("struct ") == 0) name = name.substr(7);

        raw->m_passName      = name;
        raw->m_tracyZoneName = "Pass: " + name;

        // =========================================================
        // 型(T)ごとに固有の静的GPUタグを生成する
        // =========================================================
#if defined(TRACY_ENABLE)
        // テンプレート関数内の static は型(T)ごとに別実体になるため、パス種別ごとに1つ確保される
        static std::string                     staticGpuName = "GPU: " + name;
        static const tracy::SourceLocationData loc           = {
            staticGpuName.c_str(), __FUNCTION__, __FILE__, (uint32_t)__LINE__, 0};
        raw->m_gpuSourceLocation = &loc;
#endif

        pass->Initialize(m_services); // m_services は Initialize() 後にのみ有効
        m_passes.push_back(std::move(pass));
        return raw;
    }

    /**
     * @brief 登録された全パスを順番に実行する。
     * @details
     *  - enabled == false のパスはスキップ
     *  - Debug/Profile ビルドでは ID3DUserDefinedAnnotation で各パスを囲む
     */
    void Execute(const RenderFrameContext &ctx);

    /**
     * @brief ウィンドウリサイズを全パスに伝播する。
     * @details 内部 RT を持つパス（SSAO 等）が再生成を行う。
     */
    void OnResize(uint32_t width, uint32_t height);

    // 登録されているパス数を返す（デバッグ・テスト用）。
    size_t GetPassCount() const noexcept { return m_passes.size(); }

    /**
    * @brief 初期化時に渡されたサービス群への const 参照を返す。
    * @details EnvironmentSystem 等のパイプライン外システムが services を
    *          参照できるようにするアクセサ。Graphics::GetRenderServices() 経由で公開される。
    */
    const RenderServices &GetServices() const noexcept { return m_services; }

private:
    RenderServices                            m_services{};
    std::vector<std::unique_ptr<IRenderPass>> m_passes;
};