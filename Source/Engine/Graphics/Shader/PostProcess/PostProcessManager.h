#pragma once
#include <memory>
#include <vector>
#include <type_traits>
#include <d3d11.h>
#include <wrl/client.h>

#include "IPostEffect.h"
#include "Engine/Graphics/RHI/RenderTarget.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Components/PostProcess/ToneMapConfigComponent.h"

// PostProcessが直接ToneMapを保持して最後に使うためインクルード
#include "ToneMapEffect.h"

/**
 * @brief HDR → SDR パイプライン全体を管理するクラス。
 *
 * @details
 *  【設計変更後の責務】
 *   - BeginScene: シーンを HDR RT に切り替える
 *   - エフェクトチェーン実行（BloomEffect を含む全 IPostEffect）
 *   - ToneMapping（1 入力のみ、bloom 合成は BloomEffect が完結）
 *
 *  【使い方】
 *   - 実行順 = PostProcessManager::Initialize 内の AddEffect の並び順。
 *     エフェクトを増やすときは、そこに 1 行足す。
 *   - パラメータ更新は ECS 側（PostProcessSystem）から型で引いて行う。
 *  @code
 *   // PostProcessSystem から毎フレーム
 *   if (auto* bloom = postProcess->GetEffect<BloomEffect>()) {
 *       bloom->threshold = component.threshold;
 *       bloom->SetEnabled(component.enable);
 *   }
 *  @endcode
 */
class PostProcessManager
{
public:
    void Initialize(ID3D11Device *device, UINT width, UINT height);
    ~PostProcessManager();

    void Resize(ID3D11Device *device, UINT width, UINT height);

    /** シーン描画前に HDR RT を設定する */
    void BeginScene(ID3D11DeviceContext    *dc,
                    const float             clearColor[4],
                    ID3D11DepthStencilView *dsv);

    /**
     * @brief エフェクトチェーン → ToneMap を一括実行する。
     * @param targetRTV nullptr の場合は内部 SDR バッファに出力（ImGui 用）。
     */
    ID3D11ShaderResourceView *EndScene(ID3D11DeviceContext      *dc,
                                       ID3D11RenderTargetView   *targetRTV      = nullptr,
                                       ID3D11ShaderResourceView *sceneDepthSRV  = nullptr,
                                       ID3D11ShaderResourceView *sceneNormalSRV = nullptr);

    void GrabSceneColor(ID3D11DeviceContext *dc);

    // GrabSceneColor が確保したシーンカラーのコピー。未確保なら nullptr。
    //
    // 半透明/パーティクル描画が t5 を上書きしている可能性があるため、
    // 屈折を描くレンダラは毎回ここから取り直して自分でバインドする。
    ID3D11ShaderResourceView *GetSceneColorSRV() const;

    // ================================================================
    //  エフェクト管理 API
    // ================================================================

    /**
     * @brief エフェクトを末尾に追加して Initialize する。
     * @tparam T IPostEffect を継承したクラス
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

    /**
     * @brief 型でエフェクトを検索する（最初に見つかったもの）。
     */
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

    // ToneMap の露出値（ECS から更新）
    ToneMapConfigComponent toneMapConfig;

    // ToneMap 以外のエフェクトは設定用メンバを持たない。
    // GetEffect<T>() で実体を引いて直接書き込む
    // （例: GetEffect<BloomEffect>()->intensity = 1.0f;）。

private:
    ID3D11Device *m_device = nullptr;
    UINT          m_width  = 0;
    UINT          m_height = 0;

    //  HDR キャンバスを 2枚 用意する（Ping-Pong用）
    RenderTargetHandle m_hdrBuffers[2];
    RenderTargetHandle m_outputSDR{};
    RenderTargetHandle m_sceneColorCopy{};

    std::vector<std::unique_ptr<IPostEffect>> m_effects;

    // ToneMap は常に最終ステップとして PostProcessManager が保持
    // BloomEffect が合成済み HDR を返すため 1 入力のみになった
    std::unique_ptr<ToneMapEffect> m_toneMapEffect;
};