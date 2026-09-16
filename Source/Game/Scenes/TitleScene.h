#pragma once
// タイトル画面シーン。3D背景（軽量ステージ＋立ち姿＋群れ）とメニューUIを持つ。
//
// GameScene と違い、戦場レベル(BossBattleStage)は読み込まない。読み込むと
// タイトルでゲーム本編と同じロード時間を払い、そのあと本編でもう一度払うことになる。
// タイトル専用の Title.level.json は床とライトと立ち姿だけの軽量構成。
#include "Engine/Scene/BaseScene.h"

class TitleScene : public BaseScene
{
public:
    void Initialize() override; // 共通初期化＋タイトル用レベルの読み込み

protected:
    // BaseScene::Initialize から呼ばれる拡張ポイント。ここで登録するシステムを絞る。
    void RegisterSystems() override;
};
