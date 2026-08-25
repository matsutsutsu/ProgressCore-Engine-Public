#pragma once
/// @file GameScene.h
/// @brief 実プレイ用シーン。起動時に BossBattleStage のレベルを読み込んで戦闘へ入る。
///
/// 設計方針:
///   - 実体（ライト/カメラ/プレイヤー/ボス/ステージ）は全てレベル
///     (BossBattleStage.level.json) に含まれるので、レベルを読むだけで戦場が完成する。
///   - EditorScene も同じレベルを読む。両者の唯一の差は、本シーンが導入カメラ演出
///     (BossIntroCamera) を追加で生成すること。デバッグのたびに数秒のムービーを
///     待たされないよう、演出はレベルではなく本シーンが持つ。
///   - AI の共有リソース(BattleContext)はシーンでは触らない。
///     所有者は BattleContextSyncSystem（唯一の書き込み元）。
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

    CCL::ECS::World *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }
};
