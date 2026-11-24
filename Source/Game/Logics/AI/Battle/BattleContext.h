#pragma once
// ===========================================================================
// 概要: 1対1のボス戦で、どの AI から見ても同じ値をまとめる World リソース
//
// 備考: BattleContext は誰から見ても同じ値（プレイヤーの位置・ドローンの残数など）を持つ
//       AIBlackboard はその AI から見た値（距離・角度など）を持ち、BT はそちらだけを見る
//       同じ値を1000機が重複して持たずに済む。書くのは主に BattleContextSyncSystem（L01_WorldState）
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include <DirectXMath.h>

struct BattleContext
{
    CCL::ECS::EntityID playerEntity = CCL::ECS::InvalidEntityID; // いなければ InvalidEntityID（1対1なので探索が要らない）
    CCL::ECS::EntityID bossEntity   = CCL::ECS::InvalidEntityID; // 同上

    // プレイヤーの足元（ワールド）。狙う・当てるのには使わない（カプセルは全高9mあるので、
    // 足元を基準にすると胸に当たっていても「4.5m 離れている」と判定される）。そちらは playerCenter
    DirectX::XMFLOAT3 playerPos = {0, 0, 0};

    DirectX::XMFLOAT3 bossPos = {0, 0, 0}; // ボスの足元（ワールド）

    // プレイヤーの体の中心（ワールド）。狙う・当てるのはすべてこちら。オフセットの式は
    // JoltCharacterSetupSystem がカプセルを置くときと同じものを使う
    DirectX::XMFLOAT3 playerCenter = {0, 0, 0};

    // プレイヤーの速度（m/s、平滑化済み）。当たるまで時間のかかる攻撃で、移動先を予測して狙うのに使う
    // 共有するのは、各 AI が個別に前フレームの座標から求めると平滑化の強さが別々になるため
    DirectX::XMFLOAT3 playerVelocity = {0, 0, 0};

    // プレイヤーの走行速度の設定値（m/s）。今の速さではなく、プレハブに設定された性能
    // 「走り続けても追いつく」攻撃の速さを、これに倍率を掛けて決める
    float playerRunSpeed = 0.0f;

    // プレイヤーの回避（ダッシュ）。ボスが「回避を見てから」動くのに使う
    // 回避の後は次の回避まで待ちがあるので、終わってからの秒数でその待ちを狙える
    bool  playerDashing      = false;   // 回避中か
    float playerSinceDashEnd = 9999.0f; // 直近の回避が終わってからの経過（秒）。回避中は 0

    int droneAliveCount = 1000; // ドローンの生存数。書くのは DroneManagerSystem

    DirectX::XMFLOAT3 arenaCenter = {0, 0, 0}; // 戦える範囲（真上から見た円）の中心。書く処理はなく、常に原点

    // 戦える範囲の半径（m）。見えない壁の内側に合わせる。書く処理はなく、既定値のまま
    // 端の判定（C_IsAtStageEdge）と跳躍の着地点が同じ円を見ないと、
    // 「端だから中央へ逃げる」跳躍の着地点が壁の外に出る
    float arenaRadius = 90.0f;

    bool HasPlayer() const
    {
        return playerEntity != CCL::ECS::InvalidEntityID;
    }

    bool HasBoss() const
    {
        return bossEntity != CCL::ECS::InvalidEntityID;
    }
};
