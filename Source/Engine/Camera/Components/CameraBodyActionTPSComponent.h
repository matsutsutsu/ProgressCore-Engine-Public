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

    float lookSpeedX = 120.0f; ///< ゲームパッド右スティックX軸の回転速度(度/秒)。dtで積分される。
    float lookSpeedY = 120.0f; ///< ゲームパッド右スティックY軸の回転速度(度/秒)。dtで積分される。

    // --- マウス視点操作（デルタ入力：dtを掛けない） ---
    float mouseSensitivityX      = 0.15f; ///< マウス水平感度(度/ピクセル)。移動量に直接掛ける。
    float mouseSensitivityY      = 0.15f; ///< マウス垂直感度(度/ピクセル)。
    bool  mouseInvertY           = false; ///< 上下反転（true=マウス下でカメラ上向き）。
    bool  mouseLookRequiresButton = false; ///< true=右クリック押下中のみ視点回転。false=常時マウス視点(TPS標準)。

    // --- 指数減衰（遅延追従）パラメータ ---
    float positionDamping = 12.0f; ///< 位置追従の硬さ（低いほどゴム紐のように遅れる）
    float fovDamping      = 8.0f;  ///< FOV変化の補間速度

    // --- ルックアヘッド（進行方向への注視） ---
    float lookaheadMultiplier = 0.3f; ///< 速度に対する注視点の前方移動量
    float lookaheadDamping    = 3.0f; ///< ルックアヘッドの遅延（酔い防止用）

    DirectX::SimpleMath::Vector3 currentLookahead = {0.0f, 0.0f, 0.0f}; ///< 内部状態

    // =======================================================================
    // ターゲット・ロックオンシステム用パラメータ
    // =======================================================================
    CCL::Serialization::EntityLink lockOnTarget;                   ///< ロックオン対象のエネミーEntity
    bool                           isLockOnActive         = false; ///< ロックオンが現在有効かどうか
    float                          lockOnPitch            = 12.0f; ///< ロックオン中の固定理想ピッチ角(度)
    float                          lockOnSubjugationRatio = 0.35f; ///< 敵への注視偏重率 (0.0=プレイヤー中心, 0.5=完全な中間地点)
    float                          lockOnRotationDamping  = 6.0f;  ///< ロックオン時のカメラ角度の自動回り込み補間速度

    // --- ロックオン・フレーミング（遠距離でもプレイヤーを画面内に保つ） ---
    float lockOnMaxLookShift      = 3.0f;  ///< 注視点がプレイヤーから敵側へずれる上限(m)。大きいほど敵寄り。
    float lockOnDistanceGain      = 0.35f; ///< 敵との離間1mあたり追加で引くカメラ距離(m)。0で引き無し。
    float lockOnMaxExtraDistance  = 8.0f;  ///< 離間による追加引きの上限(m)。引きすぎでプレイヤーが小さくなるのを防ぐ。

    // --- 実行時内部状態（シリアライズ不要） ---
    DirectX::SimpleMath::Vector3 currentLookAt = {0.0f, 0.0f, 0.0f}; ///< 補間計算用の仮想注視点座標
    DirectX::SimpleMath::Vector3 currentPivot  = {0.0f, 0.0f, 0.0f}; ///< 回り込み中心の補間座標（ロックオン時はプレイヤー基準）
    bool                         isFirstFrame  = true;               ///< 初回フレームでのワープ初期化フラグ
};