#pragma once
// ===========================================================================
// 概要: プレイヤーだけを動かして確かめるためのシーン。敵の居ない戦場でプレイヤーを操作する。
//
// 責務:
//   - ボス（ミュリエル）を外した戦場のレベル（PlayerTestStage）を読み込む
//   - ムービー用カメラ（CinematicCamera）のプレハブを生成する
//   - 全 System と、ポーズを登録する
//
// 備考: エディタのシーン切り替えから "PlayerTest" を開く。
//       リザルト（GameResultSystem）は登録しない。敵を倒す決着が無いので、決着の matchOver で操作を止められないようにするため。
//       導入ムービーはボスを映すので流さない。ムービー用カメラは、Cinematic Editor のプレビューを出すためだけに置く。
// ===========================================================================
#include "Engine/Scene/BaseScene.h"

class PlayerTestScene : public BaseScene
{
public:
    PlayerTestScene() = default;
    ~PlayerTestScene() override {};

    void Initialize() override;
    void Finalize() override;

    void RegisterSystems() override;

    CCL::ECS::World         *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }
};
