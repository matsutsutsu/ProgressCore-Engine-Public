#pragma once
// ===========================================================================
// 概要: UI のスケールを目標値へ滑らかに追従させるためのコンポーネント UIAnimationComponent。
//
// 責務:
//   - 追従の目標スケールと速さを保持する
//
// 備考: 追従は UIAnimationSystem が指数減衰で行う（フレームレートに依存しない）。
//       目標値は使う側が書く（例: UIButtonSystem が hover/focus 時に targetScale を設定する）。
// ===========================================================================
struct UIAnimationComponent
{
    float smoothSpeed = 12.0f; // 追従の速さ（1 秒あたり。大きいほど速く目標へ寄る）
    float targetScale = 1.0f;  // UITransform.scale の目標値
};
