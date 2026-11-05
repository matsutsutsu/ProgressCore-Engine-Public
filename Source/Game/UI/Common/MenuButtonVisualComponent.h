#pragma once
#include "Engine/ECS/Common/CCL_Types.h"

// メニューのボタンの見た目の状態。実行時に作るだけで保存しない
// ラベルを子にしないのは、ポーズ画面などがボタンとラベルの ID だけを覚えて消すため
struct MenuButtonVisualComponent
{
    CCL::ECS::EntityID label    = CCL::ECS::InvalidEntityID; // ラベル（UIText）
    float              focus    = 0.0f;                      // 選択の度合い [0,1]。帯の伸びた量
    float              pressAge = 1000.0f;                   // 押してからの秒数（大きい値なら演出は終わっている）
};
