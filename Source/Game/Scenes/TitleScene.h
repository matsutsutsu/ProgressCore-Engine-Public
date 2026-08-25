#pragma once
/// @file TitleScene.h
/// @brief タイトル画面シーン。UIシステム一式＋TitleMenuSystem だけを登録する軽量シーン。
///        GameScene / EditorScene と違い、戦場レベルの読み込みは行わない。
#include "Engine/Scene/BaseScene.h"

class TitleScene : public BaseScene
{
  protected:
    // BaseScene::Initialize から呼ばれる拡張ポイント。ここで登録するシステムを絞る。
    void RegisterSystems() override;
};
