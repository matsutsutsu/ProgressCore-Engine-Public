#include "JustEvadeSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Core/Math/StringHash.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

#include "Game/Logics/GameEvent/GameEvents.h"
#include "Engine/Timer/TimeState.h"

using namespace CCL::ECS;

// JustEvadeSystem.cpp に追加（無名関数 or メンバ）
static bool IsDroneHostile(const DroneComponent *drone)
{
    if (!drone) return false;
    // 突撃系の状態だけを「危険」とみなす
    return drone->currentState == DroneState::FireCharge;
}


// アニメーション掲示板へ書き込むトリガーのハッシュ
static const uint32_t HASH_TRIG_JUST_EVADE = CCL::Utils::HashString("OnJustEvade");


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

        // =======================================================
        //  2. アニメーションを「ジャスト回避専用モーション」へ強制遷移
        // =======================================================
        if (auto *animParams = _world->GetComponent<AnimParametersComponent>(ev.playerID))
        {
            animParams->SetTrigger(HASH_TRIG_JUST_EVADE);
        }

        // =======================================================
        // 3. エフェクト・残像・SEの呼び出し (Game Feel)
        // =======================================================
        // EventBus を使って、再生すべきSEやエフェクトをシステムに命令します。
        // （※これらの構造体は例です。あなたのプロジェクトの構造に合わせてください）

        // 音響：甲高いガラスが割れるような音
        // _world->GetEventBus().Publish(SoundRequestEvent{"event:/SFX/JustEvade", ev.playerID});

        // 画面効果：色収差（Chromatic Aberration）を強めるイベント
        // _world->GetEventBus().Publish(PostProcessPunchEvent{PostProcessType::ChromaticAberration, 1.0f});

        // 残像：ダッシュ残像を3倍の濃度・赤色で出す命令
        // _world->GetEventBus().Publish(GhostTrailRequestEvent{ev.playerID, Color::Red, 0.5f});
    });


}

void JustEvadeSystem::Update(float rawDt)
{
    // ★ Update の責務変更: フラグのクリーンアップ
    // イベントは同期的に飛んできてフラグを true にするため、
    // 次のフレームに成功判定が持ち越されないよう、フレームの最後に全フラグをリセットする。
    ForEach([&](JustEvadeComponent &evade, DashComponent &dash, const TimeStateComponent &time) {
        // 毎フレームリセットではなく、ダッシュが終わった瞬間にリセットする
        if (!dash.isDashing)
        {
            evade.isJustSuccess = false;
        }

        // ==========================================================
        //  ブラックリストタイマーの更新
        // ==========================================================
        if (evade.resetTimer > 0.0f)
        {
            evade.resetTimer -= time.localDt;

            // 時間が経過したら、相手の顔を忘れる（再びジャスト回避可能になる）
            if (evade.resetTimer <= 0.0f)
            {
                evade.lastEvadedTarget = CCL::ECS::InvalidEntityID;
            }
        }
    });
}

void JustEvadeSystem::CheckJustEvade(EntityID playerCandidate, EntityID otherCandidate)
{
    auto *evade = _world->GetComponent<JustEvadeComponent>(playerCandidate);
    auto *dash  = _world->GetComponent<DashComponent>(playerCandidate);

    if (!evade || !dash) return;

    // 1. 【ガード条件】ジャスト回避が可能な「有効ウィンドウ」内か？
    if (!dash->isJustEvadeWindowActive) return;

    // 2. 【ガード条件】このダッシュですでに成功しているなら処理しない
    // ★ここが最も重要：evade->isJustSuccess を「このダッシュ内での成功フラグ」として扱います
    if (evade->isJustSuccess) return;

    // 3. 【ガード条件】同じ敵に対する多段発動を防ぐ (既存のブラックリスト機能)
    if (evade->lastEvadedTarget == otherCandidate) return;

     // 相手がドローンで、かつ「攻撃中」でなければ成立させない
    auto *otherDrone = _world->GetComponent<DroneComponent>(otherCandidate);
    if (otherDrone && !IsDroneHostile(otherDrone)) return;
    //   → 攻撃中でない陣形ドローンに突っ込んでも回避は起きない
    //   → otherDrone が nullptr（ボス本体の攻撃など）の場合は従来通り成立


    // =====================================================================
    // 5. 判定成立
    // =====================================================================
    // 受付窓を閉じ、成功フラグを立てることで、このダッシュ中の他敵との接触は無視される
    dash->isJustEvadeWindowActive = false;
    evade->isJustSuccess          = true;

    evade->lastEvadedTarget = otherCandidate;
    evade->resetTimer       = 1.0f;

    _world->GetEventBus().Publish(JustEvadeSuccessEvent{playerCandidate, otherCandidate});

    CCL_LOG_SUCCESS(LogCategory::Game, "JUST EVADE SUCCESS!! PlayerID: %llu", playerCandidate);
}

// イベントを受け取り終わった後の状態を初期化するため、最も遅い Cleanup フェーズに登録する
REGISTER_LOGIC_SYSTEM(JustEvadeSystem, Priority::LogicStage::L07_Cleanup);