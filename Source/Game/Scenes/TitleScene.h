#pragma once
// ===========================================================================
// 概要: タイトル画面のシーン。3D の背景（床・ライト・立ち姿・ドローンの群れ）とメニューを出す。
//
// 責務:
//   - タイトル用の Title.level.json を読み込む
//   - 全 System と、タイトルだけで使う System（カメラ・群れ・メニュー）を登録する
//
// 備考: 戦場のレベル（BossBattleStage）は読み込まない。読むとタイトルでも本編と同じロード時間がかかり、
//       本編に入るときにもう一度かかる。Title.level.json は床とライトと立ち姿だけの軽い構成。
// ===========================================================================
#include "Engine/Scene/BaseScene.h"

class TitleScene : public BaseScene
{
public:
    void Initialize() override;

protected:
    // BaseScene::Initialize から呼ばれる
    void RegisterSystems() override;
};
