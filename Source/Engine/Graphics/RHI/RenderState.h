#pragma once

#include <wrl.h>
#include <d3d11.h>

// サンプラステート
enum class SamplerState
{
    PointWrap,
    PointClamp,
    LinearWrap,
    LinearClamp,

    // 異方性フィルタリング。斜めに寝た面（床・壁・地面）のテクスチャ用。
    //   トライリニアは UV の変化量が大きい方の軸に合わせてミップを選ぶため、
    //   視線に対して寝ている面ほど必要より粗いミップが選ばれて過剰にボケる。
    //   異方性は短辺側の解像度に合わせて長辺方向へ複数回サンプリングし直すので、
    //   遠景・斜め面の解像感が保たれる。
    AnisotropicWrap,
    AnisotropicClamp,

    EnumCount
};

// デプスステート
enum class DepthState
{
    TestAndWrite,

    // テストあり(LESS_EQUAL)・書き込みなし。
    //   Skybox が Z=1.0 の奥底へ描くのもこれ（1.0 <= 1.0 が通る）。
    //   以前は同じ記述の TestLessEqual が別枠であり、同一のステートオブジェクトが
    //   2つ作られて Apply のたびに無駄な切り替えが起きていたので統合した。
    TestOnly,

    WriteOnly,
    NoTestNoWrite,

    EnumCount
};

// ブレンドステート
enum class BlendState
{
    Opaque,
    Transparency,
    Additive,
    Subtraction,
    Multiply,
    // UI用のα合成。色は通常のストレートα合成だが、RTのアルファチャンネルは書き換えない
    // （＝不透明シーンのα=1を保持）。これを使わないとテキスト等の透明部分でRTのαに0の穴が開き、
    // エディタの GameView(ImGui::Image がαで貼る) で暗い背景が透けて「黒い枠」になる。
    UITransparency,

    EnumCount
};

// ラスタライザステート
enum class RasterizerState
{
    SolidCullNone,
    SolidCullBack,
    WireCullNone,
    WireCullBack,
    ShadowCullBack, // 影生成専用（深度バイアス・スロープスケールドバイアス有効）

    EnumCount
};

// レンダーステート
class RenderState
{
public:
    RenderState(ID3D11Device *device);
    ~RenderState() = default;

    // サンプラステート取得
    ID3D11SamplerState *GetSamplerState(SamplerState state) const
    {
        return samplerStates[static_cast<int>(state)].Get();
    }

    // デプスステート取得
    ID3D11DepthStencilState *GetDepthStencilState(DepthState state) const
    {
        return depthStencilStates[static_cast<int>(state)].Get();
    }

    // ブレンドステート取得
    ID3D11BlendState *GetBlendState(BlendState state) const
    {
        return blendStates[static_cast<int>(state)].Get();
    }

    // ラスタライザーステート取得
    ID3D11RasterizerState *GetRasterizerState(RasterizerState state) const
    {
        return rasterizerStates[static_cast<int>(state)].Get();
    }

private:
    Microsoft::WRL::ComPtr<ID3D11SamplerState>      samplerStates[static_cast<int>(SamplerState::EnumCount)];
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilStates[static_cast<int>(DepthState::EnumCount)];
    Microsoft::WRL::ComPtr<ID3D11BlendState>        blendStates[static_cast<int>(BlendState::EnumCount)];
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>   rasterizerStates[static_cast<int>(RasterizerState::EnumCount)];
};
