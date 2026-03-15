#pragma once
#include "ECS/Core/CCL_World.h"
#include <DirectXMath.h>
#include <vector>

// 1回の被弾情報を表す構造体（手紙）
struct DamageEvent {
    CCL::ECS::EntityID targetID;       // 誰が殴られたか（宛先）
    float              amount;         // ダメージ量
    DirectX::XMFLOAT3  knockbackForce; // ノックバック力
    CCL::ECS::EntityID attackerID;     // 誰が殴ったか（差出人）

    const char *senderName; // どのシステムが送ったか？
};

// 【郵便局】Worldに1つだけ持たせるグローバルなキュー
struct GlobalDamageQueue {
    std::vector<DamageEvent> events;

    // 毎フレームの最後に手紙を燃やす（クリアする）
    void Clear() { events.clear(); }
};