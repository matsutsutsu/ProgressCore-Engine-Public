#pragma once
// ===========================================================================
// 概要: 登録された RenderPass を、登録した順に実行するパイプライン。
//
// 責務:
//   - パスを持ち、追加した時点で Initialize する
//   - 毎フレーム、有効なパスを順に Execute する（Tracy の計測と GPU マーカーで囲む）
//   - 終了時に追加と逆の順で Shutdown する
//
// 備考: どのパスをどの順で積むかは Graphics::BuildDefaultRenderPasses が決める。
//       このクラスは具体的なパスの型を知らないので、パスを増減してもこのファイルは
//       再コンパイルされない。
// @code
//    RenderPipeline pipeline;
//    pipeline.Initialize(services); // AddPass より前に呼ぶ
//    pipeline.AddPass<ShadowPass>();
//    pipeline.AddPass<OpaquePass>();
//    pipeline.AddPass<PostProcessPass>();
//    // 毎フレーム
//    pipeline.Execute(frameCtx);
// @endcode
// ===========================================================================
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include <typeinfo>
#include <string>

#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/RenderContext.h"

class RenderPipeline
{
public:
    RenderPipeline()  = default;
    ~RenderPipeline() = default;

    RenderPipeline(const RenderPipeline &)            = delete;
    RenderPipeline &operator=(const RenderPipeline &) = delete;

    // 以後の AddPass で各パスに渡す services を写して持つ。AddPass より前に1回呼ぶ
    void Initialize(const RenderServices &services);

    // 全パスを追加と逆の順で Shutdown し、破棄する
    void Shutdown();

    /**
     * @brief パスを末尾に追加し、その場で Initialize する。
     * @tparam T IRenderPass の派生型
     * @tparam Args T のコンストラクタの引数
     * @return 追加したパス（所有するのは RenderPipeline。設定を変えたいときに使う）
     */
    template <typename T, typename... Args>
    T *AddPass(Args &&...args)
    {
        static_assert(std::is_base_of_v<IRenderPass, T>,
                      "T must derive from IRenderPass");

        auto pass = std::make_unique<T>(std::forward<Args>(args)...);
        T   *raw  = pass.get();

        // 型名をパスの表示名にする。MSVC の typeid(T).name() は "class ShadowPass" の
        // ように "class " / "struct " が付くので取り除く
        std::string name = typeid(T).name();
        if (name.find("class ") == 0) name = name.substr(6);
        if (name.find("struct ") == 0) name = name.substr(7);

        raw->m_passName      = name;
        raw->m_tracyZoneName = "Pass: " + name;

#if defined(TRACY_ENABLE)
        // Tracy の GPU ゾーンは SourceLocationData をずっと指し続けるので、static に置く。
        // 関数テンプレートの中の static は T ごとに別の変数になるので、パスの型ごとに1つできる
        static std::string                     staticGpuName = "GPU: " + name;
        static const tracy::SourceLocationData loc           = {
            staticGpuName.c_str(), __FUNCTION__, __FILE__, (uint32_t)__LINE__, 0};
        raw->m_gpuSourceLocation = &loc;
#endif

        pass->Initialize(m_services);
        m_passes.push_back(std::move(pass));
        return raw;
    }

    // 登録順に全パスを実行する。enabled が false のパスは飛ばす
    void Execute(const RenderFrameContext &ctx);

    // 全パスの OnResize を呼ぶ
    void OnResize(uint32_t width, uint32_t height);

    size_t GetPassCount() const noexcept { return m_passes.size(); }

    // Initialize で受け取った services（Graphics::GetRenderServices から、パイプラインの外にも渡す）
    const RenderServices &GetServices() const noexcept { return m_services; }

private:
    RenderServices                            m_services{}; // 各パスの m_services はこれを指す
    std::vector<std::unique_ptr<IRenderPass>> m_passes;     // 登録順 = 描画順
};
