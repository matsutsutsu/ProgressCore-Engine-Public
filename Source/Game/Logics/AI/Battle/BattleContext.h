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

struct BattleContext
{
    // 今は1対1なので探索が要らない。雑魚敵を足すときは配列になる。
    CCL::ECS::EntityID playerEntity = CCL::ECS::InvalidEntityID;
    CCL::ECS::EntityID bossEntity   = CCL::ECS::InvalidEntityID;

    // 足元の座標。狙う・当てるのには使わない（カプセルは全高9mあるので、足元基準だと
    // 胸を貫いても「4.5m 離れている」と判定される）。そちらは playerCenter。
    DirectX::XMFLOAT3 playerPos = {0, 0, 0};
    DirectX::XMFLOAT3 bossPos   = {0, 0, 0};

    // 体の中心。狙う・当てるのはすべてこちら。オフセットの式は
    // JoltCharacterSetupSystem がカプセルを載せるときと同じものを使う。
    DirectX::XMFLOAT3 playerCenter = {0, 0, 0};

    // 速度 [m/s]。飛翔時間のある攻撃の偏差射撃に使う。共有するのは、各AIが個別に
    // 前フレーム座標を覚えると平滑化の強さが別々になって挙動が揃わないため。
    DirectX::XMFLOAT3 playerVelocity = {0, 0, 0};

    // ドローンの生存数。残数がそのままボスの体力を表す。書き手は DroneManagerSystem。
    int droneAliveCount = 1000;

    // 戦える範囲（真上から見た円）。見えない壁の内側に合わせる。書き手はまだ居らず既定値のまま。
    //   端の判定と跳躍の着地点が同じ円を見ないと、「端だから中央へ逃げる」の着地点が
    //   壁の外に出て、空中で壁に引っかかる。
    DirectX::XMFLOAT3 arenaCenter = {0, 0, 0};
    float             arenaRadius = 48.0f;

    bool HasPlayer() const
    {
        return playerEntity != CCL::ECS::InvalidEntityID;
    }
    bool HasBoss() const
    {
        return bossEntity != CCL::ECS::InvalidEntityID;
    }
};
