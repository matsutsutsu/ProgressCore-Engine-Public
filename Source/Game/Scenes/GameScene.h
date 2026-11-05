#pragma once
// ===========================================================================
// 概要: 実際にプレイするシーン。BossBattleStage のレベルを読み込んで戦闘に入る
//
// 責務:
//   - BossBattleStage のレベルを読み込む（ライト・カメラ・プレイヤー・ボス・ステージがすべて入っている）
//   - ムービー用カメラ（CinematicCamera）のプレハブを生成する
//   - シーンの準備（Initialize）で、導入ムービーの再生を頼んでおく
//   - 全 System と、ゲーム本編だけで使う System（リザルト・ポーズ）を登録する
//
// 備考: EditorScene も同じレベルを読み、同じくムービー用カメラを足して導入ムービーを流す
//       撃破ムービーの再生は DefeatSlowMotionSystem が頼む
//       AI が共有する BattleContext には触らない（書くのは BattleContextSyncSystem だけ）
// ===========================================================================
#include "Engine/Scene/BaseScene.h"

class GameScene : public BaseScene
{
public:
    GameScene()           = default;
    ~GameScene() override = default;

    void Initialize() override;
    void Finalize() override;
    void FixedUpdate(float dt) override;
    void Update(float dt) override;
    void Render() override;

    void RegisterSystems() override;

    CCL::ECS::World         *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }
};
