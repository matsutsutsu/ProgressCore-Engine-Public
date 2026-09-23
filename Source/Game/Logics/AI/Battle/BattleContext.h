// 1対1ボス戦の「世界の客観的な事実」を1箇所に集約する共有リソース。
//
// BattleContext（ここ）… 誰から見ても同じ値。プレイヤー座標・ドローン残数など。
// AIBlackboard（各AI） … そのAIから見た主観。距離・担当スロットなど。
//
// 各AIは事実を読んで自分の認識を更新し、BTはその認識だけを見て判断する。
// 1000機が同じ事実を重複して持たずに済む。
#pragma once
#include "Engine/ECS/Common/CCL_Types.h"
#include <DirectXMath.h>

/// 戦闘中の「誰から見ても同じ事実」。書き手は主に BattleContextSyncSystem（L01_WorldState）。
struct BattleContext
{
    CCL::ECS::EntityID playerEntity = CCL::ECS::InvalidEntityID; // 居なければ InvalidEntityID（1対1なので探索が要らない）
    CCL::ECS::EntityID bossEntity   = CCL::ECS::InvalidEntityID; // 同上

    /// プレイヤーの足元（ワールド）。狙う・当てるのには使わない（カプセルは全高9mあるので、
    /// 足元基準だと胸を貫いても「4.5m 離れている」と判定される）。そちらは playerCenter。
    DirectX::XMFLOAT3 playerPos = {0, 0, 0};

    DirectX::XMFLOAT3 bossPos = {0, 0, 0}; // ボスの足元（ワールド）

    /// プレイヤーの体の中心（ワールド）。狙う・当てるのはすべてこちら。オフセットの式は
    /// JoltCharacterSetupSystem がカプセルを載せるときと同じものを使う。
    DirectX::XMFLOAT3 playerCenter = {0, 0, 0};

    /// プレイヤーの速度 [m/s]（平滑化済み）。飛翔時間のある攻撃の偏差射撃に使う。
    /// 共有するのは、各AIが個別に前フレーム座標を覚えると平滑化の強さが別々になるため。
    DirectX::XMFLOAT3 playerVelocity = {0, 0, 0};

    int droneAliveCount = 1000; // ドローンの生存数＝ボスの体力。書き手は DroneManagerSystem

    DirectX::XMFLOAT3 arenaCenter = {0, 0, 0}; // 戦える範囲（真上から見た円）の中心。書き手はまだ居らず既定値のまま

    /// 戦える範囲の半径 [m]。見えない壁の内側に合わせる。端の判定（C_IsAtStageEdge）と
    /// 跳躍の着地点が同じ円を見ないと、「端だから中央へ逃げる」の着地点が壁の外に出る。
    float arenaRadius = 90.0f;

    /// プレイヤーが居るか。
    bool HasPlayer() const
    {
        return playerEntity != CCL::ECS::InvalidEntityID;
    }

    /// ボスが居るか。
    bool HasBoss() const
    {
        return bossEntity != CCL::ECS::InvalidEntityID;
    }
};
