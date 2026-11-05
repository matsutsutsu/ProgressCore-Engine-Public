#pragma once
// 全ドローンで共通の調整値。機体ごとの状態は DroneComponent.h に置く

// 全ドローンの床。ステージ床のワールドY と、そこから浮かせておく最低高度 [m]
// 平らな床が前提。起伏を作るならレイキャストへ差し替える
inline constexpr float kDroneGroundY         = 0.0f;
inline constexpr float kDroneGroundClearance = 0.5f;

// 突撃時に moveSpeed へ掛ける倍率。弾幕の陣形も飛翔時間の計算に使う
inline constexpr float kDroneChargeSpeedScale = 5.5f;
