#pragma once
// ===========================================================================
// 概要: 実際にプレイするシーン。BossBattleStage のレベルを読み込み、導入ムービーを流して戦闘に入る。
//
// 責務:
//   - BossBattleStage のレベルを読み込む（ライト・カメラ・プレイヤー・ボス・ステージがすべて入っている）
//   - 導入ムービー（BossIntroTimeline）のプレハブを生成する
//   - 全 System と、ゲーム本編だけで使う System（リザルト・ポーズ）を登録する
//
// 備考: EditorScene も同じレベルを読む。違いは導入ムービーの有無だけで、演出をレベルに
//       入れずこのシーンが持つのは、デバッグで起動するたびに数秒の演出を待たないため。
//       AI が共有する BattleContext には触らない（書くのは BattleContextSyncSystem だけ）。
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
