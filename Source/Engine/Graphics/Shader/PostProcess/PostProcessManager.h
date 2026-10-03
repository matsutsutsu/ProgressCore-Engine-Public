#pragma once
// ===========================================================================
// 概要: シーンを描く HDR バッファと、ポストプロセスのエフェクトの列、最後のトーンマップをまとめるクラス。
//
// 責務:
//   - HDR バッファ2枚・SDR の出力・屈折用のシーンの色の写しを作る
//   - BeginScene で HDR バッファをクリアして描画先にする
//   - EndScene で有効なエフェクトを順にかけ、トーンマップで SDR に書き出す
//   - SceneColorGrabPass のために、その時点の HDR バッファを写す
//
// 備考: エフェクトをかける順は Initialize の AddEffect の順。増やすときはそこに1行足す。
//       値は PostProcessSystem が型で引いて毎フレーム渡す。
// @code
//    // PostProcessSystem から毎フレーム
//    if (auto *bloom = postProcess->GetEffect<BloomEffect>())
//    {
//        bloom->SetEnabled(comp && comp->enable);
//        if (comp) bloom->UpdateParams(comp->data);
//    }
// @endcode
// ===========================================================================
#include <memory>
#include <vector>
#include <type_traits>
#include <d3d11.h>
#include <wrl/client.h>

#include "IPostEffect.h"
#include "Engine/Graphics/RHI/RenderTarget.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Components/PostProcess/ToneMapConfigComponent.h"
#include "ToneMapEffect.h"

class PostProcessManager
{
public:
    void Initialize(ID3D11Device *device, UINT width, UINT height);
    ~PostProcessManager();

    void Resize(ID3D11Device *device, UINT width, UINT height);

    // HDR バッファ（1枚目）を clearColor でクリアし、dsv と一緒に描画先にする
    void BeginScene(ID3D11DeviceContext    *dc,
                    const float             clearColor[4],
                    ID3D11DepthStencilView *dsv);

    /**
     * @brief 有効なエフェクトを順にかけ、トーンマップして書き出す。
     * @param targetRTV nullptr なら中で持つ SDR のバッファに書く（エディタの GameView に表示する用）
     * @param sceneDepthSRV t4 にバインドする深度（SSAO・ContactShadow・DoF が読む）
     * @param sceneNormalSRV t6 にバインドする法線（SSAO が読む）
     * @return SDR のバッファの SRV
     */
    ID3D11ShaderResourceView *EndScene(ID3D11DeviceContext      *dc,
                                       ID3D11RenderTargetView   *targetRTV      = nullptr,
                                       ID3D11ShaderResourceView *sceneDepthSRV  = nullptr,
                                       ID3D11ShaderResourceView *sceneNormalSRV = nullptr);

    // HDR バッファを写し、写しを t5 にバインドする（SceneColorGrabPass から呼ばれる）
    void GrabSceneColor(ID3D11DeviceContext *dc);

    /**
     * @brief GrabSceneColor の写しを返す。
     *
     * 半透明やパーティクルの描画が t5 を上書きしている場合があるので、屈折を描くもの（TrailRenderer など）は毎回ここから取って自分でバインドする。
     *
     * @return 写しの SRV。所有しない。作れていなければ nullptr。
     */
    ID3D11ShaderResourceView *GetSceneColorSRV() const;

    // ============================================================
    // エフェクト
    // ============================================================

    /**
     * @brief エフェクトを末尾に足して Initialize する。
     * @tparam T IPostEffect の派生クラス
     */
    template <typename T, typename... Args>
    T *AddEffect(Args &&...args)
    {
        static_assert(std::is_base_of_v<IPostEffect, T>,
                      "T must derive from IPostEffect");
        auto effect = std::make_unique<T>(std::forward<Args>(args)...);
        T   *raw    = effect.get();
        effect->Initialize(m_device, m_width, m_height);
        m_effects.push_back(std::move(effect));
        return raw;
    }

    // T のエフェクトを返す（最初に見つかったもの）。無ければ nullptr
    template <typename T>
    T *GetEffect() const
    {
        for (const auto &e : m_effects)
        {
            if (auto *p = dynamic_cast<T *>(e.get()))
                return p;
        }
        return nullptr;
    }

    size_t GetEffectCount() const noexcept { return m_effects.size(); }

    // トーンマップの設定（PostProcessSystem が毎フレーム写す）。
    // 他のエフェクトの設定は、GetEffect<T>() で引いて UpdateParams で渡す
    ToneMapConfigComponent toneMapConfig;

private:
    ID3D11Device *m_device = nullptr;
    UINT          m_width  = 0; // px
    UINT          m_height = 0; // px

    RenderTargetHandle m_hdrBuffers[2];    // 読み書きを交互に入れ替える HDR バッファ。シーンは [0] に描く
    RenderTargetHandle m_outputSDR{};      // targetRTV が無いときの出力
    RenderTargetHandle m_sceneColorCopy{}; // GrabSceneColor の写し

    std::vector<std::unique_ptr<IPostEffect>> m_effects; // かける順

    // トーンマップは必ず最後に1回だけかけるので、エフェクトの列とは別に持つ
    std::unique_ptr<ToneMapEffect> m_toneMapEffect;
};