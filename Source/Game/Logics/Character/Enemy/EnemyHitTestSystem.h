#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Physics/Collision/JoltCollisionEvent.h"

// 衝突イベントを受信するテストシステム
class EnemyHitTestSystem : public CCL::ECS::IfSystem<EnemyHitTestSystem> {
public:
    EnemyHitTestSystem() : IfSystem("EnemyHitTestSystem") {}

    // システム生成時にEventBusへ登録を行う
    void Initialize() override;
    
    // イベント駆動なのでUpdateの中身は空でよい
    void Update(float dt) override {} 

private:
    // イベントを受信する関数
    void OnCollision(const JoltCollisionEvent& ev);
};