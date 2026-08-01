#pragma once
/// @file GameScene.h
/// @brief 実プレイ用シーン。起動時に BossBattleStage のレベルを読み込んで戦闘へ入る。
///
/// 設計方針:
///   - EditorScene と違い、テスト用エンティティ(SpawnGameTest)やライトの手動生成は行わない。
///     必要な実体（ライト/カメラ/プレイヤー/ボス/ステージ）は全てレベル
///     (BossBattleStage.level.json) に含まれるので、レベルを読むだけで戦場が完成する。
///   - エディタ(ImGui)には一切依存しないので Shipping でもそのまま動く。
///   - ゲーム本編専用システム（リザルト/ポーズ）は RegisterSystems で手動登録する。
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

    CCL::ECS::World *GetWorld() override { return _worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return _ecsContext->GetSystemManager(); }
};
