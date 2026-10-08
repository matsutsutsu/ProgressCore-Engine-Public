#pragma once
// ===========================================================================
// 概要: ジャスト回避の成立状態を持つ Component。
//
// 責務:
//   - 今のダッシュでジャスト回避が成立したかを持つ
//   - 同じ相手に続けて成立しないよう、直前の相手と残り時間を持つ
//
// 備考: 書くのは JustEvadeSystem だけ。
// ===========================================================================
#include "Engine/ECS/Common/CCL_IDEmitter.h"

struct JustEvadeComponent
{
    bool isJustSuccess = false; // 今のダッシュで成立済みか。ダッシュしていない間は false に戻す

    // 同じ相手に続けて成立させないための記録
    CCL::ECS::EntityID lastEvadedTarget = CCL::ECS::InvalidEntityID; // 直前にジャスト回避した相手
    float              resetTimer       = 0.0f;                      // lastEvadedTarget を消すまでの残り時間（秒）
};