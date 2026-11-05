#pragma once
// ===========================================================================
// 概要: チュートリアルを遊ぶシーン。本番と同じ戦場で、ボスに手加減させながら操作を教える
//
// 責務:
//   - BossBattleStage のレベル（本番と同じ戦場）を読み込む
//   - 全 System と、チュートリアルだけで使う System とポーズを登録する
//
// 備考: タイトルの「チュートリアル」から入り、完了の画面の「本番へ」で GameScene へ移る
//       導入ムービーは本番の始まりにだけ見せるので、ここでは流さず、ムービー用カメラも置かない
//       リザルトは登録せず、代わりに TutorialHealthGuardSystem で倒れさせず、TutorialCompleteSystem で次へ進ませる
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
