#include "JustEvadeSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void JustEvadeSystem::Initialize()
{
    // ★ EventBus から物理衝突の報告を購読する
    ListenEvent<JoltCollisionEvent>([this](const JoltCollisionEvent& ev) {
        // 双方向でプレイヤーがどちらに含まれているかチェックする
        CheckJustEvade(ev.entityA, ev.entityB);
        CheckJustEvade(ev.entityB, ev.entityA);
    });
}

void JustEvadeSystem::Update(float rawDt)
{
    // ★ Update の責務変更: フラグのクリーンアップ
    // イベントは同期的に飛んできてフラグを true にするため、
    // 次のフレームに成功判定が持ち越されないよう、フレームの最後に全フラグをリセットする。
    ForEach([](JustEvadeComponent& evade, const DashComponent& dash) {
        evade.isJustSuccess = false;
    });
}

void JustEvadeSystem::CheckJustEvade(EntityID playerCandidate, EntityID otherCandidate)
{
    // 1. 対象エンティティが JustEvade と Dash を持っているか
    auto* evade = _world->GetComponent<JustEvadeComponent>(playerCandidate);
    auto* dash = _world->GetComponent<DashComponent>(playerCandidate);

    if (!evade || !dash) return;

    // 2. 無敵中でなければ JUST 判定しない
    if (!dash->isInvincible) return;

    // 既にこのフレームで成功済みなら重複処理を防ぐ
    if (evade->isJustSuccess) return;

    // 3. 判定成立 
    // ※ 将来的には otherCandidate が「敵の攻撃判定(Hitbox)」かどうかのチェックをここに追加します
    evade->isJustSuccess = true;
    
    CCL_LOG_SUCCESS(LogCategory::Game, "JUST EVADE SUCCESS!! PlayerID: %llu", playerCandidate);

}

// イベントを受け取り終わった後の状態を初期化するため、最も遅い Cleanup フェーズに登録する
REGISTER_LOGIC_SYSTEM(JustEvadeSystem, Priority::LogicStage::L07_Cleanup);