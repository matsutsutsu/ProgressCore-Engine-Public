#pragma once
// ===========================================================================
// 概要: LOD のあるメッシュを、段ごとの色（LOD0 緑・LOD1 黄・LOD2 赤）で上から重ねて描くデバッグ用のパス。
//
// 責務:
//   - NormalPrepass が RenderQueue に残した、段の順に並んだ見えるインスタンスの列を読む
//   - 段ごとに色を定数バッファへ入れ、その段のインデックスの範囲で半透明に描く
//   - 直近のフレームで段ごとに描いた数を持つ（エディタに表示する）
//
// 備考: 既定はオフ（enabled = false）で、エディタのレンダーデバッグのウィンドウから切り替える。
//       頂点シェーダーは NormalPrepass のものをそのまま使い、同じ深度になる形を描く。
//       深度は LESS_EQUAL のテストだけにして、表に見えている面にだけ色を乗せる。
//       マテリアルのシェーダーには触らないので、オフのときの負荷は無い。
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/Core/InstanceBuffer.h"
#include "Engine/Graphics/Renderer/Core/MeshLod.h"
#include <d3d11.h>
#include <wrl/client.h>

class LodDebugPass : public IRenderPass
{
public:
    void Initialize(const RenderServices &services) override;
    void Execute(const RenderFrameContext &ctx) override;

    // 直近に描いたフレームの、段ごとのインスタンスの数（オフの間は最後に描いたときの値のまま）
    const MeshLod::LevelCounts &GetLastLevelCounts() const { return m_lastLevelCounts; }

    // 段 level に使う色（エディタの凡例にも使う）
    static DirectX::XMFLOAT4 GetLevelColor(size_t level);

private:
    // 段の色を定数バッファへ書く
    void UpdateColor(ID3D11DeviceContext *dc, size_t level);

    PipelineState                             m_pso;            // 半透明・深度はテストだけ
    InstanceBuffer                            m_instanceBuffer;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;    // 段の色を出すだけのピクセルシェーダー
    Microsoft::WRL::ComPtr<ID3D11Buffer>      m_colorBuffer;    // 段の色（b0）

    MeshLod::LevelCounts m_lastLevelCounts{}; // 直近に描いたフレームの、段ごとのインスタンスの数
};
