#pragma once
// ===========================================================================
// 概要: エディタで開発するときのシーン。GameScene と同じ戦場レベルを読み、導入カメラ演出は出さない。
//
// 責務:
//   - BossBattleStage のレベルを読み込む
//   - 全 System と、ゲーム本編だけで使う System（リザルト・ポーズ）を登録する
//
// 備考: 導入カメラ演出は GameScene だけが出す。デバッグで起動するたびに数秒の演出を待たないため。
// ===========================================================================
#include "Engine/Scene/BaseScene.h"

class EditorScene : public BaseScene
{
public:
    EditorScene() = default;
    ~EditorScene() override {};

    void Initialize() override;
    void Finalize() override;
    void FixedUpdate(float dt) override;
    void Update(float dt) override;
    void Render() override;

    void RegisterSystems() override;

    CCL::ECS::World         *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }
};