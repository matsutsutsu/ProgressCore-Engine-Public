/// @file  MoveIntentComponent.h
/// @brief AI が「移動したい」という意図を書き込むコンポーネント。
///
/// ★ 意図・実行の分離原則 ★
///   StateTreeSystem  → このコンポーネントに「欲望」を書く
///   MovementSystem   → このコンポーネントを読んで実際に Transform/Velocity を動かす
///
/// これにより:
///   - StateTreeSystem の Write 依存を「Intent 系のみ」に絞れる
///   - TaskScheduler の並列グラフが汚染されない
///   - ルールベースAI / 物理ベース移動 / ナビメッシュ経路 の差し替えが StateTree 側無変更で可能
#pragma once
#include <DirectXMath.h>
#include "Engine/ECS/Common/CCL_Types.h"

/// @brief 1フレーム分の「こう移動したい」意図。
///        毎フレーム上書きされるのでクリア不要（MovementSystem が消費する）。
struct MoveIntentComponent
{
    bool     hasTarget     = false;                ///< true のとき以下が有効
    DirectX::XMFLOAT3 desiredTarget = {0,0,0};    ///< 目標ワールド座標
    float    desiredSpeed  = 0.0f;                 ///< 希望移動速度 (m/s)

    // フレーム内に複数のシステムが上書きしないよう「書き込んだシステムの優先度」を持つ
    // 高い数字が優先（回避行動 > 追跡行動 など）。MovementSystem が参照。
    int      priority      = 0;
};
