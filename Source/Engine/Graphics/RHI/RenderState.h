#pragma once
// ===========================================================================
// 概要: サンプラー・深度・ブレンド・ラスタライザのステートオブジェクトを、
//       起動時に全種類作って持つクラス。
//
// 責務:
//   - 各ステートの種類を列挙値で定義する
//   - 列挙値からステートオブジェクトを返す
//
// 備考: 描画中にステートを作らないよう、コンストラクタで全部作っておく。
//       Graphics が1つ持ち、RenderServices 経由で各パスに渡す。
// ===========================================================================
#include <wrl.h>
#include <d3d11.h>

enum class SamplerState
{
    PointWrap,
    PointClamp,
    LinearWrap,
    LinearClamp,

    // 異方性フィルタリング。斜めに寝た面（床・壁・地面）のテクスチャ用。
    //   トライリニアは UV の変化が大きい方の軸に合わせてミップを選ぶので、視線に対して
    //   寝ている面ほど必要より粗いミップが選ばれ、ぼけすぎる。
    //   異方性は短い辺の解像度に合わせ、長い辺の方向へ何回もサンプリングするので、
    //   遠くの面や斜めの面でもくっきり見える。
    AnisotropicWrap,
    AnisotropicClamp,

    EnumCount
};

enum class DepthState
{
    TestAndWrite,

    // テストあり（LESS_EQUAL）・書き込みなし。
    //   スカイボックスを Z=1.0 の一番奥へ描くのもこれ（1.0 <= 1.0 が通る）。
    //   同じ設定の別名をここに足さないこと。同じ中身のステートオブジェクトが2つでき、
    //   Apply のたびに無駄な切り替えが起きる。
    TestOnly,

    WriteOnly,
    NoTestNoWrite,

    EnumCount
};

enum class BlendState
{
    Opaque,
    Transparency,
    Additive,
    Subtraction,
    Multiply,
    // UI 用のアルファ合成。色は通常のアルファ合成だが、RT のアルファは書き換えない
    // （不透明なシーンが書いた α=1 を残す）。これを使わないと、文字の透明部分で RT の α に
    // 0 の穴が開き、エディタの GameView（ImGui::Image がアルファ付きで貼る）で暗い背景が透け、
    // 黒い枠に見える。
    UITransparency,
    // UI 用の加算合成。色は Additive と同じだが、UITransparency と同じ理由で RT のアルファは書き換えない
    // （透明な部分の多い画像を加算で重ねると、画像の矩形の形に α の穴が開いて黒く見える）
    UIAdditive,

    EnumCount
};

enum class RasterizerState
{
    SolidCullNone,
    SolidCullBack,
    WireCullNone,
    WireCullBack,
    ShadowCullBack, // シャドウマップ専用（深度バイアスとスロープスケールドバイアスを掛ける）

    EnumCount
};

class RenderState
{
public:
    // 全種類のステートオブジェクトを作る
    RenderState(ID3D11Device *device);
    ~RenderState() = default;

    ID3D11SamplerState *GetSamplerState(SamplerState state) const
    {
        return samplerStates[static_cast<int>(state)].Get();
    }

    ID3D11DepthStencilState *GetDepthStencilState(DepthState state) const
    {
        return depthStencilStates[static_cast<int>(state)].Get();
    }

    ID3D11BlendState *GetBlendState(BlendState state) const
    {
        return blendStates[static_cast<int>(state)].Get();
    }

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
