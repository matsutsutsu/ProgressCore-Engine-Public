#pragma once
#include "IPostEffect.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/RHI/RenderTarget.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <memory>

/**
 * @brief Bloom エフェクト。旧 BloomPass を IPostEffect として完全再設計。
 *
 * @details
 *  【旧設計からの変更点】
 *   - static g_bloomResources（グローバル）を廃止 → メンバとして所有
 *   - Graphics::Instance() 依存を廃止 → 独自の OpaqueBlendState を保持
 *   - Process() の返値が「シーン + グロー合成済み HDR」になった
 *     旧: グローのみを返し Tone Mapper が 2 入力で合成
 *     新: BloomEffect が合成まで完結 → Tone Mapper は 1 入力で済む
 *   - BloomConfigComponent のコピーは不要。フィールドを直接書き込む
 *
 *  【内部パイプライン】
 *  @code
 *   inputSRV (sceneHDR)
 *     → Extraction (full→half)
 *     → Downsample × N
 *     → Upsample   × N → _upChain[0] (half-res bloom glow)
 *     → Composite: _compositeRT = scene + glow * intensity  (full-res HDR)
 *     ← 返す
 *  @endcode
 *
 *  【使い方】
 *  @code
 *   auto* bloom = _postProcess->AddEffect<BloomEffect>();
 *   bloom->intensity  = 1.5f;
 *   bloom->threshold  = 0.9f;
 *   bloom->SetEnabled(true);
 *  @endcode
 */
class BloomEffect : public IPostEffect
{
public:
    BloomEffect() = default;
    ~BloomEffect() override;

    void Initialize(ID3D11Device* device, UINT width, UINT height) override;
    void Resize(ID3D11Device* device, UINT width, UINT height) override;

    /**
     * @param inputSRV シーン HDR テクスチャ
     * @return シーン + Bloom グロー の合成済み HDR SRV（次のエフェクトの入力）
     */
    void Process(
        ID3D11DeviceContext* dc,
        ID3D11ShaderResourceView* inputSRV,
        ID3D11RenderTargetView* outputRTV) override;

    const char* GetName() const override { return "Bloom"; }

    // ---- パラメータ（ECS や ImGui から直接書き込む）----
    float threshold = 1.0f;  ///< グローを発生させる最低輝度
    float softKnee = 0.5f;  ///< 閾値のソフトエッジ幅
    float intensity = 1.0f;  ///< グローの加算強度
    float radius = 1.0f;  ///< アップサンプル時の拡散半径倍率

private:
    static constexpr int kMaxIterations = 6;

    // ---- GPU 定数バッファレイアウト（HLSL の CbBloom と一致させる）----
    struct CbBloomData {
        float threshold;
        float softKnee;
        float intensity;
        float padding;
        float texelWidth;
        float texelHeight;
        float pad2[2];
    };

    // ---- GPU リソース（旧 static グローバルをメンバに移動）----
    struct BloomResources {
        Microsoft::WRL::ComPtr<ID3D11VertexShader>  vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>   psExtract;   // 輝度抽出
        Microsoft::WRL::ComPtr<ID3D11PixelShader>   psDown;      // ダウンサンプル
        Microsoft::WRL::ComPtr<ID3D11PixelShader>   psUp;        // アップサンプル
        Microsoft::WRL::ComPtr<ID3D11PixelShader>   psComposite; // scene + glow 合成 (新規)
        Microsoft::WRL::ComPtr<ID3D11Buffer>         cbBloom;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>  sampler;
    };
    std::unique_ptr<BloomResources> _res;

    // Opaque ブレンドステート（旧 Graphics::Instance() 依存を排除）
    Microsoft::WRL::ComPtr<ID3D11BlendState> _opaqueBlendState;

    // RT チェーン
    std::vector<RenderTargetHandle> _downChain;
    std::vector<RenderTargetHandle> _upChain;
  
    void CreateChains(UINT width, UINT height);
    void ReleaseChains();
    void UpdateCbBloom(ID3D11DeviceContext* dc, const CbBloomData& data);
};