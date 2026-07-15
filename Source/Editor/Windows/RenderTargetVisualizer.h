/// @file  RenderTargetVisualizer.h
/// @brief RenderDebugWindow(v2) 用の可視化ヘルパ。中間バッファの SRV を種別に応じて
///        「表示可能な色」へ焼き直し、その SRV を返す（ImGui::Image で貼るため）。
///        役割     : 深度線形化 / 法線remap / AOグレースケール / HDR露出 / 配列スライス選択。
///        責務     : FullScreenVS + RenderTargetDebugPS で内部RTへ全画面描画する。
///        位置付け : Editor 専用（Release ビルドからは除外）。RenderDebugWindow が所有する。
///        設計方針 : サムネイル用途なので bake 解像度は上限でクランプ。RT はフレームで使い回す。
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <cstdint>

class RenderTarget;

/// @brief 可視化パラメータ。RenderDebugWindow が種別と UI から組み立てる。
struct RTVisualizeParams
{
    int   mode          = 0;      ///< DebugTexKind の整数値
    float exposure      = 1.0f;   ///< HDR 表示の露出
    float nearZ         = 0.1f;   ///< 深度線形化用
    float farZ          = 1000.0f;
    int   arraySlice    = 0;      ///< 配列テクスチャの表示スライス
    int   useArray      = 0;      ///< 0=Texture2D / 1=Texture2DArray
    int   channelSelect = 0;      ///< 0=RGB 1=R 2=G 3=B 4=A
};

/// @brief 中間バッファを可視化用 RT へ焼くヘルパ。
class RenderTargetVisualizer
{
public:
    /// @brief シェーダー等を遅延初期化する（初回のみ）。失敗時 false。
    bool EnsureInitialized(ID3D11Device* device);

    /// @brief フレーム先頭で RT プールのインデックスをリセットする。
    void BeginFrame() noexcept { _poolIndex = 0; }

    /// @brief src を可視化して内部 RT へ焼き、その SRV を返す。失敗時 src をそのまま返す。
    /// @note  返る SRV は「次に同じプール RT が再利用されるまで」有効（ImGui 描画までは安全）。
    ID3D11ShaderResourceView* Visualize(ID3D11DeviceContext* dc,
        ID3D11ShaderResourceView* src, uint32_t w, uint32_t h,
        const RTVisualizeParams& p);

private:
    // b0 の中身（HLSL CbDebug と一致させること）
    struct Cb
    {
        int   mode; float exposure; float nearZ; float farZ;
        int   arraySlice; int useArray; int channelSelect; int _pad0;
    };

    RenderTarget* AcquirePoolTarget(ID3D11Device* device, uint32_t w, uint32_t h);

    Microsoft::WRL::ComPtr<ID3D11VertexShader>   _vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>    _ps;
    Microsoft::WRL::ComPtr<ID3D11Buffer>         _cb;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>   _sampler;
    // ★直前のパス(SSAO/Bloom等)が加算/乗算ブレンドや深度テストを残したまま
    //   全画面三角形を描くと、意図した色で上書きされず黒く見える。
    //   他の PostProcess エフェクト(SSAOEffect 等)と同じく明示的に不透明固定する。
    Microsoft::WRL::ComPtr<ID3D11BlendState>     _opaqueBlendState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _noDepthState;

    std::vector<std::unique_ptr<RenderTarget>> _pool; // 表示エントリ数ぶん使い回す
    size_t _poolIndex   = 0;
    bool   _initialized = false;
};
