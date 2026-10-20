#pragma once
// ===========================================================================
// 概要: チュートリアルを遊ぶシーン。本番と同じ戦場で、ボスに手加減させながら操作を教える。
//
// 責務:
//   - BossBattleStage のレベルを読み込む（本番の GameScene と同じ戦場）
//   - 全 System と、チュートリアルだけで使う System（進行・ヒント・倒れない・完了の画面）、ポーズを登録する
//
// 備考: タイトルの「チュートリアル」から入り、教え終えたら完了の画面の「本番へ」で GameScene へ移る。
//       導入ムービーは流さず、明転が終わればすぐ TPS のカメラで操作できる。
//       ムービーは本番（GameScene）の始まりにだけ見せ、本番の戦いを最初から味わってもらうため。
//       そのためムービー用カメラ（CinematicCamera）のプレハブも足さない。
//       リザルト（GameResultSystem）は登録しない。チュートリアルは勝ち負けを決める場ではないため。
//       代わりに TutorialHealthGuardSystem でプレイヤーを倒れさせず（ボスの形態も移らせず）、
//       TutorialCompleteSystem で次へ進ませる。
// ===========================================================================
#include "Engine/Scene/BaseScene.h"

class TutorialScene : public BaseScene
{
public:
    TutorialScene()           = default;
    ~TutorialScene() override = default;

    void Initialize() override;
    void Finalize() override;

    void RegisterSystems() override;

    CCL::ECS::World         *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }
};
