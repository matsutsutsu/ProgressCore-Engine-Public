/// @file  PerceptionComponent.h
/// @brief 敵がプレイヤーを「どう知覚しているか」を保持する実行時コンポーネント。
///
/// 役割:
///   EnemyPerceptionSystem が毎フレーム計算して書き込む「センサーの出力」。
///   StateTree のタスク・条件はこれを読んで判断する（プレイヤー検索を各タスクで
///   やらないことで、検索コストをフレーム1回に集約する）。
///
/// 設計方針:
///   - 実行時専用データ。シリアライズ対象外（Meta は最小限のデバッグ表示のみ）。
///   - POD。Chunk に乗る。
#pragma once
#include <cfloat>      // FLT_MAX
#include <DirectXMath.h>
#include "Engine/ECS/Common/CCL_Types.h"

/// @brief 敵のプレイヤー知覚結果。EnemyPerceptionSystem が書き込む。
struct PerceptionComponent
{
    /// @brief 知覚しているターゲット（プレイヤー）の EntityID。
    ///        いなければ InvalidEntityID。
    CCL::ECS::EntityID target = CCL::ECS::InvalidEntityID;

    /// @brief ターゲットの最後に確認した位置（ワールド座標）。
    ///        タスクの移動目標として使う。
    DirectX::XMFLOAT3 lastKnownPos = { 0, 0, 0 };

    /// @brief ターゲットまでの距離（XZ平面・水平距離）。
    ///        ターゲットがいなければ FLT_MAX。
    float distance = FLT_MAX;

    /// @brief ターゲットへの水平方向の単位ベクトル（移動・回転に使う）。
    DirectX::XMFLOAT3 dirToTarget = { 0, 0, 1 };
};