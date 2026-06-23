#pragma once
/// @file CameraBodyActionTPS.h
/// @brief ハイスピードアクションTPSカメラの調整パラメータおよび実行時状態を管理するコンポーネント。

#include "Engine/Serialization/Core/EntityLink.h"
#include <SimpleMath.h>

struct CameraBodyTPSComponent
{
    // --- ターゲット設定 ---
    CCL::Serialization::EntityLink target;                            ///< 追従対象のEntity
    DirectX::SimpleMath::Vector3   targetOffset = {0.0f, 1.5f, 0.0f}; ///< 注視点のオフセット（キャラクターの頭/肩の高さ）

    // --- カメラ距離と視野角 ---
    float distance = 6.0f;  ///< ターゲットからの基本距離
    float baseFov  = 45.0f; ///< 通常時の視野角(度数法)
    float dashFov  = 70.0f; ///< ダッシュ（加速）時の最大視野角

    // --- オービット（回転）制御 ---
    float currentYaw   = 0.0f;   ///< 現在の水平角度(度)
    float currentPitch = 15.0f;  ///< 現在の垂直角度(度)
    float minPitch     = -70.0f; ///< 垂直角度の最低制限（地面めり込み防止）
    float maxPitch     = 80.0f;  ///< 垂直角度の最高制限

    float lookSpeedX = 120.0f; ///< マウス/スティックX軸感度
    float lookSpeedY = 120.0f; ///< マウス/スティックY軸感度

    // --- 指数減衰（遅延追従）パラメータ ---
    float positionDamping = 12.0f; ///< 位置追従の硬さ（低いほどゴム紐のように遅れる）
    float fovDamping      = 8.0f;  ///< FOV変化の補間速度

    // --- ルックアヘッド（進行方向への注視） ---
    float lookaheadMultiplier = 0.3f; ///< 速度に対する注視点の前方移動量
    float lookaheadDamping    = 3.0f; ///< ルックアヘッドの遅延（酔い防止用）

    DirectX::SimpleMath::Vector3 currentLookahead = {0.0f, 0.0f, 0.0f}; ///< 内部状態

    // --- 実行時内部状態（シリアライズ不要） ---
    DirectX::SimpleMath::Vector3 currentLookAt = {0.0f, 0.0f, 0.0f}; ///< 補間計算用の仮想注視点座標
    bool                         isFirstFrame  = true;               ///< 初回フレームでのワープ初期化フラグ
};