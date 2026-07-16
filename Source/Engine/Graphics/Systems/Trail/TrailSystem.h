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

/// @brief 武器トレイルの区間記録とリボン生成を担うシステム。
class TrailSystem
    : public CCL::ECS::IfSystem<TrailSystem,
          CCL::ECS::Read<TransformComponent>,
          CCL::ECS::Write<TrailComponent>>
{
public:
    TrailSystem() : IfSystem("TrailSystem") {}

    void Update(float dt) override;
};
