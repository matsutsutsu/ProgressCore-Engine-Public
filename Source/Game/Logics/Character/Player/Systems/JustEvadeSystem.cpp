#include "JustEvadeSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Game/Logics/GameEvent/GameEvents.h"
#include "Engine/Timer/TimeState.h"

using namespace CCL::ECS;

void JustEvadeSystem::Initialize()
{
    // ★ EventBus から物理衝突の報告を購読する
    ListenEvent<JoltCollisionEvent>([this](const JoltCollisionEvent& ev) {
        // 双方向でプレイヤーがどちらに含まれているかチェックする
        CheckJustEvade(ev.entityA, ev.entityB);
        CheckJustEvade(ev.entityB, ev.entityA);
    });


    // JUST回避が成功した瞬間のイベントをリッスン
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        // 1. プレイヤーの時間を「150ミリ秒(0.15f)」完全に止める（ヒットストップ）
        if (auto *timeState = _world->GetComponent<TimeStateComponent>(ev.playerID))
        {
            timeState->hitStopTimer = 0.15f;
        }

        auto& timeContext = _world->GetResource<TimeContext>();
        timeContext.globalFreezeScale = 0.1f;
        timeContext.globalHitStopTimer = 0.5f;

        // ※ 将来的に敵だけをスローモーションにしたい場合は、ここで ev.attackerID の
        // TimeState を取得して cinematicScale を 0.1f などに落とす処理を追記します。
    });


}

void JustEvadeSystem::Update(float rawDt)
{
    // ★ Update の責務変更: フラグのクリーンアップ
    // イベントは同期的に飛んできてフラグを true にするため、
    // 次のフレームに成功判定が持ち越されないよう、フレームの最後に全フラグをリセットする。
    ForEach([&](JustEvadeComponent& evade, const DashComponent& dash) {

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
    
    // ===============================================================
    //  成功の事実をシステム全体にブロードキャスト（放送）する
    // ===============================================================
    _world->GetEventBus().Publish(JustEvadeSuccessEvent{playerCandidate, otherCandidate});

    CCL_LOG_SUCCESS(LogCategory::Game, "JUST EVADE SUCCESS!! PlayerID: %llu", playerCandidate);

}

// イベントを受け取り終わった後の状態を初期化するため、最も遅い Cleanup フェーズに登録する
REGISTER_LOGIC_SYSTEM(JustEvadeSystem, Priority::LogicStage::L07_Cleanup);