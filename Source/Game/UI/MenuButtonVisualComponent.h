#pragma once
// ===========================================================================
// 概要: メニューのボタンの見た目の状態（選択の度合い・押してからの時間）を持つコンポーネント。
//
// 責務:
//   - ボタンのラベル（別エンティティ）の ID を持つ
//   - 選択の帯が伸びた量と、押してからの経過時間を持つ
//
// 備考: MenuUiFactory::SpawnButton がボタンに付け、MenuButtonStyleSystem が毎フレーム動かす。
//       選択の帯はボタン自身の UIProgressBarComponent で描く（value が帯の伸びた量）。
//       子のエンティティにしないのは、ポーズ画面などがボタンとラベルの ID だけを覚えて消すため
//       （子を足すと消し残す）。実行時に作るだけなので保存はしない。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"

struct MenuButtonVisualComponent
{
    CCL::ECS::EntityID label    = CCL::ECS::InvalidEntityID; // ラベル（UIText）。選択中は帯の上で読めるよう暗い色にする
    float              focus    = 0.0f;                      // 選択の度合い [0,1]。帯の伸びた量になる
    float              pressAge = 1000.0f;                   // 押してからの秒数（大きい値なら押した演出は終わっている）
};
