#pragma once
// ===========================================================================
// 概要: エディタで開発するときのシーン。GameScene と同じ戦場レベルを読み、同じくムービーも流す
//
// 責務:
//   - BossBattleStage のレベルを読み込む
//   - ムービー用カメラ（CinematicCamera）のプレハブを生成する
//   - シーンの準備（Initialize）で、導入ムービーの再生を頼んでおく
//   - 全 System と、ゲーム本編だけで使う System（リザルト・ポーズ）を登録する
//
// 備考: 導入ムービーは長押しでスキップできるので、デバッグ起動でも GameScene と同じく流す
//       ムービー用カメラがないと、撃破ムービーも Cinematic Editor のプレビューも出ない
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