#pragma once
// ===========================================================================
// RootMotionComponent.h
// 役割:   rootMotionCurve 駆動のルートモーション状態を保持する。
// 責務:   有効フラグ・前フレーム進捗の記憶（累積カーブの差分計算に使う）。
// 位置付け:
//   AnimControllerSystem(L02_Update=200) が遷移時に enabled/justStarted を設定。
//   RootMotionSystem(L02_Update=200) が curve を評価し overrideSpeed 機構へ流す。
//   物理適用は JoltCharacterUpdateSystem(L03_PrePhysics) が既存経路で行う。
// 設計方針:
//   - カーブは「進捗(0-1)→累積前進距離(m)」。各フレーム速度は差分/localDt。
//   - enabled はステートの applyRootMotion から AnimController が設定する。
// ===========================================================================

/// @brief ルートモーションの実行状態。
struct RootMotionComponent
{
    bool  enabled      = false; ///< 現ステートが applyRootMotion なら true
    float prevProgress = 0.0f;  ///< 前フレーム進捗(0-1)。差分計算用
    bool  justStarted  = false; ///< 開始直後フラグ（prevProgress 初期化用）
};