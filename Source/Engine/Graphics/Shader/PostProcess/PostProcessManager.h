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
 *  @code
 *   // Graphics::Initialize 等で登録（順序 = 実行順）
 *   auto* bloom = _postProcess->AddEffect<BloomEffect>();
 *   bloom->intensity = 1.5f;
 *   _postProcess->AddEffect<VignetteEffect>();
 *
 *   // ECS システムからパラメータ更新
 *   auto* bloom = _postProcess->GetEffect<BloomEffect>();
 *   if (bloom) {
 *       bloom->threshold = component->threshold;
 *       bloom->SetEnabled(component->enable);
 *   }
 *  @endcode
 */
class PostProcessManager
{
public:
    void Initialize(ID3D11Device* device, UINT width, UINT height);
    ~PostProcessManager();

    void Resize(ID3D11Device* device, UINT width, UINT height);

    /** シーン描画前に HDR RT を設定する */
    void BeginScene(ID3D11DeviceContext* dc,
        const float clearColor[4],
        ID3D11DepthStencilView* dsv);

    /**
     * @brief エフェクトチェーン → ToneMap を一括実行する。
     * @param targetRTV nullptr の場合は内部 SDR バッファに出力（ImGui 用）。
     */
    ID3D11ShaderResourceView* EndScene(ID3D11DeviceContext* dc,
        ID3D11RenderTargetView* targetRTV = nullptr);

    // ================================================================
    //  エフェクト管理 API
    // ================================================================

    /**
     * @brief エフェクトを末尾に追加して Initialize する。
     * @tparam T IPostEffect を継承したクラス
     */
    template<typename T, typename... Args>
    T* AddEffect(Args&&... args)
    {
        static_assert(std::is_base_of_v<IPostEffect, T>,
            "T must derive from IPostEffect");
        auto effect = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = effect.get();
        effect->Initialize(_device, _width, _height);
        _effects.push_back(std::move(effect));
        return raw;
    }

    /**
     * @brief 型でエフェクトを検索する（最初に見つかったもの）。
     */
    template<typename T>
    T* GetEffect() const
    {
        for (const auto& e : _effects) {
            if (auto* p = dynamic_cast<T*>(e.get()))
                return p;
        }
        return nullptr;
    }

    size_t GetEffectCount() const noexcept { return _effects.size(); }

    // ToneMap の露出値（ECS から更新）
    ToneMapConfigComponent toneMapConfig;

    // 注: bloomConfig は廃止。BloomEffect のメンバを直接書き込む。
    //   旧: _postProcess->bloomConfig.intensity = 1.0f;
    //   新: _postProcess->GetEffect<BloomEffect>()->intensity = 1.0f;

private:
    ID3D11Device* _device = nullptr;
    UINT          _width = 0;
    UINT          _height = 0;

    RenderTargetHandle _sceneHDR{};  // シーン描画先 (HDR)
    RenderTargetHandle _outputSDR{}; // ToneMap 出力 (SDR, ImGui 用)

    std::vector<std::unique_ptr<IPostEffect>> _effects;

    // ToneMap は常に最終ステップとして PostProcessManager が保持
    // BloomEffect が合成済み HDR を返すため 1 入力のみになった
    struct ToneMapResources {
        Microsoft::WRL::ComPtr<ID3D11VertexShader>  vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>   ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>        cbToneMap;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>  sampler;
    };
    std::unique_ptr<ToneMapResources> _toneMapRes;
};