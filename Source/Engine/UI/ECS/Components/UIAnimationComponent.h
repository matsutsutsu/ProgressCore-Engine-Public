#pragma once
/// @file UIAnimationComponent.h
/// @brief UIの滑らかな追従アニメ（Damping）。旧 UIElement の smoothSpeed/target を ECS へ移植。
///
/// 設計方針:
///   - 目標値と速度だけを持つデータ。UIAnimationSystem が UITransform.scale を毎フレーム
///     目標へ指数減衰で近づける（フレームレート非依存）。
///   - 目標値の供給は用途側が行う（例: UIButtonSystem が hover/focus 時に targetScale を設定）。
struct UIAnimationComponent
{
    float smoothSpeed = 12.0f; ///< 追従速度（大きいほど速く目標へ寄る）
    float targetScale = 1.0f;  ///< UITransform.scale の目標値
};
