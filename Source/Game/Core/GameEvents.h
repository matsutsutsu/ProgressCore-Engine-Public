#pragma once
// ゲームの System どうしが EventBus でやり取りするイベント
// カメラへの演出の要求は、カメラがゲーム側の型を知らずに済むよう Engine/Camera/CameraEvents.h に置く
#include "Engine/ECS/Common/CCL_Types.h"
#include <DirectXMath.h>

// ジャスト回避に成功した瞬間に出す。受けた PlayerRushSystem がラッシュを始める
struct JustEvadeSuccessEvent
{
    CCL::ECS::EntityID playerID;   // 回避したプレイヤー
    CCL::ECS::EntityID attackerID; // 避けた攻撃（ボスの剣の判定かドローン）。ラッシュがロックオンなしのとき向く先に使う
};

// ダメージを受けたときに出す（受けた側のイベント）
// DamageResolutionSystem が出し、PlayerFeedbackSystem と BossPerceptionSystem が受ける
struct DamageTakenEvent
{
    CCL::ECS::EntityID targetID;     // ダメージを受けたエンティティ
    float              damageAmount; // 受けたダメージ量
    bool               isPlayer;     // 受けたのがプレイヤーか
};

// 攻撃が相手にダメージを与えたときに出す（攻撃した側のイベント）
struct AttackHitConfirmedEvent
{
    CCL::ECS::EntityID attackerID;
    CCL::ECS::EntityID victimID;

    float damage = 0.0f;

    // 命中の重さ（ヒットストップの長さと、その間の時間の速さ）
    float hitStopDuration = 0.0f;
    float hitStopScale    = 1.0f;
};

// 攻撃が当たったと確定した瞬間に出す（無敵ですり抜けたときは出ない）
// HitEffectSystem が受けて、当たった位置に GPU パーティクルを出す
struct HitImpactEvent
{
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f}; // 当たった位置（ワールド座標）
    float             power    = 1.0f;               // 命中の強さ（演出の大きさに使う）
};

// プレイヤーが跳んだ瞬間に出す。PlayerActionVfxSystem が受けて土煙などを出す
// 移動の System が VFX やプレハブを知らずに済むよう、跳んだことだけを知らせる
struct PlayerJumpedEvent
{
    DirectX::XMFLOAT3 position  = {0.0f, 0.0f, 0.0f}; // 跳んだときのプレイヤーの位置（ワールド座標）
    int               jumpCount = 1;                  // 何段目のジャンプか（2 以上なら2段ジャンプ）
};

// プレイヤーがダッシュ（回避）を始めた瞬間に出す。PlayerActionVfxSystem が受ける
struct PlayerDashedEvent
{
    DirectX::XMFLOAT3  position = {0.0f, 0.0f, 0.0f}; // ダッシュを始めたときのプレイヤーの位置（ワールド座標）
    CCL::ECS::EntityID playerID = CCL::ECS::InvalidEntityID;
};
