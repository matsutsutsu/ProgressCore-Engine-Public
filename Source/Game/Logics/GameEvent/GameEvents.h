#pragma once

#include "Engine/ECS/Common/CCL_Types.h" // CCL::ECS::EntityID を使うため
#include <DirectXMath.h>           // DirectX::XMFLOAT3 を使うため


// JUST回避が成功した瞬間にEventBusへ放たれるイベント
struct JustEvadeSuccessEvent
{
    CCL::ECS::EntityID playerID;   // 回避を成功させたプレイヤー
    CCL::ECS::EntityID attackerID; // 攻撃してきた敵（今回は不使用でも用意しておく）
};

// ※ カメラへの演出要求（CameraShakeRequestEvent / CameraFovPunchRequestEvent）は
//    Engine/Camera/CameraEvents.h へ移動した。カメラがゲーム側の型を知らずに済むようにするため。



// GameEvents.h
struct EntityKilledEvent {
    CCL::ECS::EntityID killedEntityID;
    DirectX::XMFLOAT3 position;
    bool isPlayer;
    int droppedCoinAmount; // ★ 死亡時に落とす金額をイベントに持たせる
};

// 誰かがダメージを受けた時に発行されるイベント
struct DamageTakenEvent {
    CCL::ECS::EntityID targetID;     // ダメージを受けた人
    float              damageAmount; // 受けたダメージ量
    bool               isPlayer;     // プレイヤーかどうか（受け取り側が判定しやすくする便利フラグ）
};

// 攻撃が「確定ヒット」した瞬間に発行されるイベント（無敵ですり抜けた時は出ない）。
// 命中位置に衝突エフェクト(GPUパーティクル)を出すために HitEffectSystem が購読する。
struct HitImpactEvent {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f}; // 接触点(ワールド座標)
    float             power    = 1.0f;               // 命中の強さ(ダメージ量など。演出スケール用)
};