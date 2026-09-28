#pragma once
// ===========================================================================
// 概要: ブレンド・深度・ラスタライザ・トポロジーの組み合わせ（パイプラインステート）。
//
// 責務:
//   - PipelineStateDesc: 4つの設定の組と、よく使う組み合わせの既定値を持つ
//   - PipelineState: 設定の組をデバイスコンテキストへまとめて設定する
//
// 備考: ステートオブジェクトの実体は RenderState が持つ。ここは列挙値を持つだけ。
// ===========================================================================
#include "Engine/Graphics/RHI/RenderState.h"
#include <d3d11.h>

struct PipelineStateDesc
{
    BlendState               blend      = BlendState::Opaque;
    DepthState               depth      = DepthState::TestAndWrite;
    RasterizerState          rasterizer = RasterizerState::SolidCullBack;
    D3D11_PRIMITIVE_TOPOLOGY topology   = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // ============================================================
    // よく使う組み合わせ。ここから取って必要な項目だけ変える
    // ============================================================

    // 不透明
    static PipelineStateDesc DefaultOpaque()
    {
        PipelineStateDesc desc;
        desc.blend      = BlendState::Opaque;
        desc.depth      = DepthState::TestAndWrite;
        desc.rasterizer = RasterizerState::SolidCullBack;
        desc.topology   = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        return desc;
    }

    // 半透明・UI（深度は書かない）
    static PipelineStateDesc DefaultTransparent()
    {
        PipelineStateDesc desc;
        desc.blend      = BlendState::Transparency;
        desc.depth      = DepthState::TestOnly;           // 奥のものを消さないよう、テストだけする
        desc.rasterizer = RasterizerState::SolidCullNone; // 裏面も描く
        desc.topology   = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        return desc;
    }

    // シャドウマップ
    static PipelineStateDesc DefaultShadow()
    {
        PipelineStateDesc desc;
        desc.blend = BlendState::Opaque;
        desc.depth = DepthState::TestAndWrite;
        // 影専用のラスタライザ（深度バイアスとスロープスケールドバイアスを掛ける）
        desc.rasterizer = RasterizerState::ShadowCullBack;
        desc.topology   = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        return desc;
    }
};

class PipelineState
{
public:
    PipelineState() = default;

    PipelineState(const PipelineStateDesc &desc) : m_desc(desc) {}

    // 描画の直前に呼び、4つの設定をまとめてデバイスコンテキストへ設定する
    void Apply(ID3D11DeviceContext *dc, const RenderState *rs) const
    {
        dc->IASetPrimitiveTopology(m_desc.topology);
        dc->OMSetBlendState(rs->GetBlendState(m_desc.blend), nullptr, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(rs->GetDepthStencilState(m_desc.depth), 0);
        dc->RSSetState(rs->GetRasterizerState(m_desc.rasterizer));
    }

    // Opaque 以外のブレンド（Transparency / Additive など）か
    bool IsBlendEnabled() const { return m_desc.blend != BlendState::Opaque; }

private:
    PipelineStateDesc m_desc;
};
