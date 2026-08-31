/**
 * 1対1ボス戦の「世界の客観的な事実」を1箇所に集約する共有リソース。
 *
 * 設計思想 :
 *   ゲームAIの「ブラックボード」と、ECSの「リソース」を役割で使い分ける。
 *     - BattleContext（このリソース）= 世界に1つの客観的事実
 *         誰から見ても同じ値（プレイヤー座標・ドローン残数など）。
 *     - AIBlackboard（各AIが持つ）     = そのAIエージェント個人の主観的認識
 *         「このボスから見た距離」「このドローンの担当スロット」など。
 *
 *   各AIは BattleContext（事実）を読み、自分の Blackboard（認識）を更新し、
 *   その Blackboard を見て BT が判断する。
 *   → 1000機が同じ事実を重複保持せず、個体判断は各自のブラックボードで独立。
 *
 * 1対1なので「敵を探す」処理は不要。プレイヤーもボスも唯一なので、
 * ここに ID と座標を置けば全システムが探索ゼロで直接参照できる。
 */
#pragma once
#include "Engine/ECS/Common/CCL_Types.h"
#include <DirectXMath.h>

// 戦場全体で共有される唯一の「戦況」
struct BattleContext
{
    // プレイヤーとボスのEntityID
    // 今は２つだけど、今後雑魚敵など増やす予定
    CCL::ECS::EntityID playerEntity = CCL::ECS::InvalidEntityID;
    CCL::ECS::EntityID bossEntity   = CCL::ECS::InvalidEntityID;

    // プレイヤーとボスの座標
    DirectX::XMFLOAT3 playerPos = {0, 0, 0};
    DirectX::XMFLOAT3 bossPos   = {0, 0, 0};

    // ドローンの生存数
    // 残数がそのままボスの体力を表す。DroneManagerSystem が毎フレーム数えて書き込む
    // （誰から見ても同じ値なので、個体の Blackboard ではなくここに置く）
    int droneAliveCount = 1000;

    // プレイヤーが存在するか
    bool HasPlayer() const
    {
        return playerEntity != CCL::ECS::InvalidEntityID;
    }
    bool HasBoss() const
    {
        return bossEntity != CCL::ECS::InvalidEntityID;
    }
};
