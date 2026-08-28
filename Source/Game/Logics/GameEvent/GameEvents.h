#pragma once

#include "Engine/ECS/Common/CCL_Types.h" // CCL::ECS::EntityID を使うため
#include <DirectXMath.h>           // DirectX::XMFLOAT3 を使うため


// JUST回避が成功した瞬間にEventBusへ放たれるイベント
struct JustEvadeSuccessEvent
{
    CCL::ECS::EntityID playerID;   // 回避を成功させたプレイヤー
    CCL::ECS::EntityID attackerID; // 攻撃してきた敵。現状の購読側は未使用だが、反撃演出用に残す
};

// ※ カメラへの演出要求（CameraShakeRequestEvent / CameraFovPunchRequestEvent）は
//    Engine/Camera/CameraEvents.h へ移動した。カメラがゲーム側の型を知らずに済むようにするため。



// GameEvents.h
struct EntityKilledEvent {
    CCL::ECS::EntityID killedEntityID;
    DirectX::XMFLOAT3 position;
    bool isPlayer;
    int droppedCoinAmount; // 死亡時に落とす金額をイベントに持たせる
};

// 誰かがダメージを受けた時に発行されるイベント
// 現在の使用System：PlayerFeedbackSystem,JustEvadeSystem
struct DamageTakenEvent {
    CCL::ECS::EntityID targetID;     // ダメージを受けた人
    float              damageAmount; // 受けたダメージ量
    bool               isPlayer;     // プレイヤーかどうか（受け取り側が判定しやすくする便利フラグ）
};

// ===========================================================================
// 攻撃命中確定イベント
//
// 「攻撃者の攻撃が実際に相手へダメージを与えた」ことを通知する。
// DamageTakenEvent が「被弾側」のイベントなのに対して、こちらは「攻撃側」のイベント。
// ===========================================================================
struct AttackHitConfirmedEvent
{
    CCL::ECS::EntityID attackerID;
    CCL::ECS::EntityID victimID;

    float damage = 0.0f;

    // ヒットストップなど、命中の重さを判断するための情報。
    float hitStopDuration = 0.0f;
    float hitStopScale    = 1.0f;
};


// 攻撃が「確定ヒット」した瞬間に発行されるイベント（無敵ですり抜けた時は出ない）。
// 命中位置に衝突エフェクト(GPUパーティクル)を出すために HitEffectSystem が購読する。
struct HitImpactEvent {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f}; // 接触点(ワールド座標)
    float             power    = 1.0f;               // 命中の強さ(ダメージ量など。演出スケール用)
};

// プレイヤーがジャンプした瞬間に発行されるイベント。
// その場に土煙などのVFXを出すために PlayerActionVfxSystem が購読する
// （移動系システムはVFX/プレハブを知らずに「跳んだ」事実だけを announce する）。
struct PlayerJumpedEvent {
    DirectX::XMFLOAT3 position  = {0.0f, 0.0f, 0.0f}; // ジャンプ時のプレイヤー位置(ワールド)
    int               jumpCount = 1;                  // 何段目のジャンプか（2以上＝2段ジャンプ）
};

// プレイヤーがダッシュ（回避バースト）を開始した瞬間に発行されるイベント。
struct PlayerDashedEvent {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f}; // ダッシュ開始時のプレイヤー位置(ワールド)
    CCL::ECS::EntityID playerID = CCL::ECS::InvalidEntityID;
};
