#pragma once
// ===========================================================================
// 概要: メニュー共通の見た目を持つ MenuStyle プレハブを、シーンの始めに置く System
//
// 責務:
//   - 最初の Update で Assets/Prefabs/UI/MenuStyle.json を生成する
//
// 備考: ポーズと設定は描画ステージで画面を組み、そこではプレハブを生成できない
//       そのため、ロジックが動いているうちに置いておく
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

class MenuStyleSystem : public CCL::ECS::IfSystem<
    MenuStyleSystem>
{
public:
    MenuStyleSystem() : IfSystem("MenuStyleSystem") {}

    void Update(float dt) override;
    void OnGui() override; // 調整の手順の案内

private:
    bool m_spawned = false; // 生成を試したか（失敗しても毎フレーム読み直さない）
};
