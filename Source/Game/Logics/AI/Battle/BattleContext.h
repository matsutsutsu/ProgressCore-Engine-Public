/// @file  BattleContext.h
/// @brief 1対1ボス戦の「世界の客観的な事実」を1箇所に集約する共有リソース。
///
/// 設計思想 :
///   ゲームAIの「ブラックボード」と、ECSの「リソース」を役割で使い分ける。
///     - BattleContext（このリソース）= 世界に1つの客観的事実
///         誰から見ても同じ値（プレイヤー座標・ドローン残数・BREAKゲージ）。
///     - AIBlackboard（各AIが持つ）     = そのAIエージェント個人の主観的認識
///         「このボスから見た距離」「このドローンの担当スロット」など。
///
///   各AIは BattleContext（事実）を読み、自分の Blackboard（認識）を更新し、
///   その Blackboard を見て BT が判断する。
///   → 1000機が同じ事実を重複保持せず、個体判断は各自のブラックボードで独立。
///
/// 1対1なので「敵を探す」処理は不要。プレイヤーもボスも唯一なので、
/// ここに ID と座標を置けば全システムが探索ゼロで直接参照できる。
#pragma once
#include "Engine/ECS/Common/CCL_Types.h"
#include <DirectXMath.h>

/// @brief 戦場全体で共有される唯一の「戦況」。World Resource として1つ持つ。
struct BattleContext
{
    // ----------------------------------------------------------------
    // 登場人物（1対1なので各1体。探索不要で直接参照する）
    // ----------------------------------------------------------------
    CCL::ECS::EntityID playerEntity = CCL::ECS::InvalidEntityID;
    CCL::ECS::EntityID bossEntity   = CCL::ECS::InvalidEntityID;

    /// @brief プレイヤーの現在座標（毎フレーム更新される事実）。
    ///        ボス・ドローンはこれを見れば「常にロックオン」できる。
    DirectX::XMFLOAT3 playerPos = {0, 0, 0};
    DirectX::XMFLOAT3 bossPos   = {0, 0, 0};

    // ----------------------------------------------------------------
    // ドローン（残数がそのままボスの体力を表す）
    // ----------------------------------------------------------------
    /// @brief 生存しているドローンの数。戦術（Control/Pressure/...）の切替に使う。
    ///        DroneManagerSystem が毎フレーム集計して書き込む。
    int droneAliveCount = 1000;

    /// @brief ドローンの最大数（弱体化の割合計算などに使う）。
    int droneMaxCount = 1000;

    // ----------------------------------------------------------------
    // BREAK システム（ジャスト回避を重ねてボスの体勢を崩す）
    // ----------------------------------------------------------------
    /// @brief ジャスト回避成功で溜まるゲージ（0.0〜1.0）。
    float breakGauge = 0.0f;

    /// @brief 現在 BREAK 発動中か（発動中はボス戦術が Broken になる）。
    bool isBreaking = false;

    // ----------------------------------------------------------------
    // ヘルパー
    // ----------------------------------------------------------------
    /// @brief ドローン残数の割合（1.0=満タン, 0.0=全滅）。
    float DroneRatio() const
    {
        if (droneMaxCount <= 0) return 0.0f;
        return static_cast<float>(droneAliveCount) / static_cast<float>(droneMaxCount);
    }

    bool HasPlayer() const
    {
        return playerEntity != CCL::ECS::InvalidEntityID;
    }
    bool HasBoss() const
    {
        return bossEntity != CCL::ECS::InvalidEntityID;
    }
};