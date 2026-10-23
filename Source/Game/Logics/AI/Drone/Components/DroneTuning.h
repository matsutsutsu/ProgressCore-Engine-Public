#pragma once
// ===========================================================================
// 概要: 全ドローンで共通の調整値。1機ぶんの状態ではなく、群れ全体で1つだけ持つ値を置く。
//
// 責務:
//   - 予告の震えの見せ方（DroneShiverTuning。World リソース）
//   - 全ドローンが潜らない床の高さ
//   - 突撃の速度倍率（移動層と弾幕の陣形が共有する）
//
// 備考: 1機ぶんの状態（DroneComponent.h）と分けて、どれが機体ごとの値かを読み分けやすくしている。
// ===========================================================================

// 予告の震えの調整値。全機共通なので World リソースとして1つだけ持つ
struct DroneShiverTuning
{
    float amplitude = 0.25f; // 溜め切ったときの振れ幅 [m]
    float frequency = 7.0f;  // 震えの速さ [Hz]。上げすぎるとノイズに見えて予告にならない
    float rampTime  = 1.2f;  // 最大まで溜まる時間 [秒]
    float scatter   = 1.0f;  // 機体ごとの位相のずれ具合。0=全機同時 / 1=最大限ずらす
};

// 全ドローンの床。ステージ床のワールドY と、そこから浮かせておく最低高度 [m]。
// 平らな床が前提。起伏を作るならレイキャストへ差し替える
inline constexpr float kDroneGroundY         = 0.0f;
inline constexpr float kDroneGroundClearance = 0.5f;

// 突撃時に moveSpeed へ掛ける倍率。弾幕の陣形も飛翔時間の計算に使う
inline constexpr float kDroneChargeSpeedScale = 5.5f;
