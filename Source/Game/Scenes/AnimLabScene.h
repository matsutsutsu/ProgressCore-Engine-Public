#pragma once
// ===========================================================================
// 概要: アニメーションを調整するためのシーン。AnimLab のレベルを読み込む
//
// 責務:
//   - AnimLab.level.json を読み込む（平らな床・ライト・プレイヤーとボスのプレビュー用プレハブ）
//   - 全 System を登録する
//
// 備考:
//   - エディタのシーン切り替えから開く
//   - 置いているのは操作・物理・AI を持たない Preview プレハブ。当たり方と手触りの確認は本編のシーンで行う
// ===========================================================================
#include "Engine/Scene/BaseScene.h"

class AnimLabScene : public BaseScene
{
public:
    AnimLabScene() = default;
    ~AnimLabScene() override {};

    void Initialize() override;
    void Finalize() override;

    CCL::ECS::World         *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }

protected:
    void RegisterSystems() override;
};
