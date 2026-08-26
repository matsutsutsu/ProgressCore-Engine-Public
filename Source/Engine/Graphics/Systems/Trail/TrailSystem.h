#pragma once
// ===========================================================================
// TrailSystem.h
// -------------
// 役割:
//   TrailComponent を持つエンティティの刃2端点を毎フレーム記録し、過去区間から
//   加算リボン(三角形リスト)を生成して TrailRenderer に積む。
//
// 責務:
//   - renderWorldMatrix でローカル端点をワールドへ変換（描画補間に追従）。
//   - 区間の老化・寿命切れ破棄・新区間の追記（移動量ゲート付き）。
//   - 尾ほど薄くフェードする頂点カラーを付けてリボンを構築・提出。
//
// 位置付け:
//   R08_Main（メイン描画ステージ）で走り、同フレームの TrailPass が描画する。
//   ロジックには一切干渉しない純粋な見た目システム（Read Transform / Write Trail）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Trail/TrailComponent.h"
#include "Engine/Graphics/Renderer/TrailRenderer.h"

#include <vector>

/// @brief 武器トレイルの区間記録とリボン生成を担うシステム。
class TrailSystem
    : public CCL::ECS::IfSystem<TrailSystem,
          CCL::ECS::Read<TransformComponent>,
          CCL::ECS::Write<TrailComponent>>
{
public:
    TrailSystem() : IfSystem("TrailSystem") {}

    void Update(float dt) override;

private:
    /// @brief 補間後の1断面。中心と「中心→先端」の半ベクトルで表す。
    /// @note  形状(板/十字/筒)はこの断面列から作られる。断面の作り方を1箇所に
    ///        まとめることで、形状を増やしても補間側を触らずに済む。
    struct Ring
    {
        DirectX::XMFLOAT3 center = {};
        DirectX::XMFLOAT3 half   = {};   ///< 中心→先端（幅カーブ適用済み）
        float             age    = 0.0f; ///< フェード用の経過秒
        float             taper  = 1.0f; ///< 尾ほど細くする係数（thickness 指定時に使う）
    };

    /// @brief 区間リングを CatmullRom で補間し、連続した断面列に展開する。
    void BuildRings(const TrailComponent &trail, std::vector<Ring> &outRings) const;

    /// @brief 断面列から、shape に応じた三角形リストを積む。
    void EmitRibbon(const TrailComponent &trail, const std::vector<Ring> &rings,
                    std::vector<TrailRenderer::Vertex> &outVerts) const;
    void EmitCross(const TrailComponent &trail, const std::vector<Ring> &rings,
                   std::vector<TrailRenderer::Vertex> &outVerts) const;
    void EmitTube(const TrailComponent &trail, const std::vector<Ring> &rings,
                  const DirectX::XMFLOAT3 &cameraEye,
                  std::vector<TrailRenderer::Vertex> &outVerts) const;

    /// @brief 断面 i の進行方向（末尾は直前の向きを流用）。
    DirectX::XMVECTOR RingTangent(const std::vector<Ring> &rings, size_t i) const;

    /// @brief Cross / Tube が使う半径。thickness=0 なら半ベクトル長を採用。
    float RingRadius(const TrailComponent &trail, const Ring &ring) const;

    /// @brief 尾ほど薄くフェードする頂点色を作る。
    TrailRenderer::Vertex MakeVertex(const TrailComponent &trail,
                                     const DirectX::XMFLOAT3 &pos,
                                     float age, float intensity) const;
};
