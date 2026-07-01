#pragma once

#include "Engine/ECS/Common/CCL_Types.h" // CCL::ECS::EntityID を使うため
#include <DirectXMath.h>           // DirectX::XMFLOAT3 を使うため


// JUST回避が成功した瞬間にEventBusへ放たれるイベント
struct JustEvadeSuccessEvent
{
    CCL::ECS::EntityID playerID;   // 回避を成功させたプレイヤー
    CCL::ECS::EntityID attackerID; // 攻撃してきた敵（今回は不使用でも用意しておく）
};

/// @brief 画面を揺らす要求をシステム全体に知らせるイベント
struct CameraShakeRequestEvent
{
    float amplitude; // 揺れの強さ
    float duration;  // 揺れる時間（秒）
};



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