#include "JustEvadeSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Core/Math/StringHash.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

#include "Game/Logics/GameEvent/GameEvents.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Graphics/Components/Impact/JustEvadeEffectComponent.h"
#include "Engine/Physics/Components/RigidBody/JoltRigidBodyComponent.h" // 壁/地形との接触を除外するため

#include <cstdio>
#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

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
    hasGui = true; // チェインHUDを OnGui で前面描画する

    // ★ EventBus から物理衝突の報告を購読する
    ListenEvent<JoltCollisionEvent>([this](const JoltCollisionEvent& ev) {
        // 双方向でプレイヤーがどちらに含まれているかチェックする
        CheckJustEvade(ev.entityA, ev.entityB);
        CheckJustEvade(ev.entityB, ev.entityA);
    });

    // ★ 被弾でチェイン切断（ボス／ドローンいずれの攻撃を食らっても初期化）
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &ev) {
        if (ev.isPlayer) BreakChain(ev.targetID);
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
        //  1.5 接近（ジャスト回避の近道）の起動
        //      現在のダッシュを終了し、「殴れる位置に着くための接近時間」へ移行する。
        //      実際の移動と時計の打ち消しは PlayerDashSystem が毎フレーム駆動する。
        // =======================================================
        if (auto *dash = _world->GetComponent<DashComponent>(ev.playerID))
        {
            dash->isDashing               = false; // 回避ダッシュを終了して接近へ
            dash->isJustEvadeWindowActive = false;
            dash->isInvincible            = true;  // 接近中もスロー中の群れをすり抜けられるよう無敵
            dash->isApproaching           = true;
            dash->approachTimer           = dash->approachDuration;
        }

        // =======================================================
        //  2. アニメーションを「ジャスト回避専用モーション」へ強制遷移
        // =======================================================
        if (auto *animParams = _world->GetComponent<AnimParametersComponent>(ev.playerID))
        {
            animParams->SetTrigger(HASH_TRIG_JUST_EVADE);
        }

        // =======================================================
        // 3. 画面演出（ウィッチタイム）のトリガー
        // =======================================================
        // シーンに置いた JustEvadeEffectComponent(singleton) の timer を立てるだけ。
        // 実際の色調/放射ブラー/色収差/ビネットの駆動は JustEvadeEffectSystem(R02) が
        // 毎フレーム行い、演出後は元の PP 値へ自動復元する。
        if (auto *fx = _world->GetSingletonComponent<JustEvadeEffectComponent>())
        {
            fx->Trigger();
        }

        // TODO(任意): 音響（甲高いガラス音）や残像の追加はここに足す。
        // _world->GetEventBus().Publish(SoundRequestEvent{"event:/SFX/JustEvade", ev.playerID});
    });


}

void JustEvadeSystem::Update(float rawDt)
{
    // ★ Update の責務変更: フラグのクリーンアップ
    // イベントは同期的に飛んできてフラグを true にするため、
    // 次のフレームに成功判定が持ち越されないよう、フレームの最後に全フラグをリセットする。
    ForEach([&](JustEvadeComponent &evade, DashComponent &dash, CharacterMovementInputComponent &input, const TimeStateComponent &time) {
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

        // ==========================================================
        //  チェイン: 段数に応じた効果を毎フレーム反映する
        //   （速度上昇＝リターン / 受付窓の縮小＝リスク。撃破範囲は Drone 側で適用）
        // ==========================================================
        int chain = (evade.chainCount < 0) ? 0 : evade.chainCount;

        // 移動速度上昇（上限 chainSpeedMax）。JoltCharacterUpdateSystem が walk/override 速度に掛ける。
        float speedUp = chain * evade.chainSpeedPerLevel;
        if (speedUp > evade.chainSpeedMax) speedUp = evade.chainSpeedMax;
        input.speedMultiplier = 1.0f + speedUp;

        // ジャスト受付窓の厳しさ（下限 windowScaleMin）。PlayerDashSystem が窓幅に掛ける。
        float wScale = 1.0f - chain * evade.windowScalePerLevel;
        if (wScale < evade.windowScaleMin) wScale = evade.windowScaleMin;
        dash.justEvadeWindowScale = wScale;

        // ==========================================================
        //  チェイン切断: 攻撃が来ていない状況での「ダラケた静止」
        //   ※「攻撃予兆に対する正しい待機」との区別は未実装(暫定)。
        //     誤爆を避けるため idleBreakTime は緩め、静止のみをカウントする。
        // ==========================================================
        if (evade.idleBreakTime > 0.0f && chain > 0)
        {
            bool moving = input.desiredVelocity.LengthSquared() > 0.001f;
            bool acting = dash.isDashing || dash.isApproaching;
            if (moving || acting)
            {
                evade.idleTimer = 0.0f;
            }
            else
            {
                evade.idleTimer += time.localDt;
                if (evade.idleTimer >= evade.idleBreakTime)
                {
                    evade.chainCount = 0;
                    evade.idleTimer  = 0.0f;
                }
            }
        }
        else
        {
            evade.idleTimer = 0.0f;
        }
    });
}

// チェインを初期状態へ戻す（被弾などで呼ぶ）。効果値(速度/窓)は次フレームの Update が 0 段基準へ戻す。
void JustEvadeSystem::BreakChain(EntityID playerID)
{
    if (auto *evade = _world->GetComponent<JustEvadeComponent>(playerID))
    {
        if (evade->chainCount != 0)
            CCL_LOG_INFO(LogCategory::Game, "CHAIN BROKEN (was x%d)", evade->chainCount);
        evade->chainCount = 0;
        evade->idleTimer  = 0.0f;
    }
}

// ============================================================================
//  チェインHUD：段数を前面に大きく表示する（爽快感のフィードバック）
// ============================================================================
#ifdef ENABLE_EDITOR
void JustEvadeSystem::OnGui()
{
    const JustEvadeComponent *found = nullptr;
    ForEach([&](JustEvadeComponent &evade, DashComponent &, CharacterMovementInputComponent &, const TimeStateComponent &) {
        if (!found) found = &evade;
    });
    if (!found || found->chainCount <= 0) return;

    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImGuiIO    &io = ImGui::GetIO();

    // 段数が上がるほど熱い色（青→黄→赤）に寄せる
    float t = (found->maxChain > 0) ? (float)found->chainCount / (float)found->maxChain : 0.0f;
    if (t > 1.0f) t = 1.0f;
    ImU32 col = IM_COL32((int)(120 + 135 * t), (int)(200 - 120 * t), (int)(255 - 235 * t), 255);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "CHAIN x%d", found->chainCount);

    // OVERDRIVE ゲージ(左下)と被らないよう、その少し上に表示する
    const float x = 48.0f;
    const float y = io.DisplaySize.y - 120.0f;
    dl->AddText(ImGui::GetFont(), 34.0f, ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 200), buf); // 影
    dl->AddText(ImGui::GetFont(), 34.0f, ImVec2(x, y), col, buf);
}
#else
void JustEvadeSystem::OnGui() {}
#endif

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

    // 3.5【ガード条件】壁・地形など「静止した非攻撃物」との接触では絶対に成立させない。
    //   これが無いと、落下防止の見えない壁(INVISIBLE_WALL)や地形(NON_MOVING)に
    //   ダッシュで突っ込んだだけでジャスト回避が誤発動し、接近(isApproaching)で
    //   内側へ強制移動させられてしまう。ドローンやボスの攻撃は別レイヤーなので通過する。
    if (auto *otherBody = _world->GetComponent<JoltRigidbodyComponent>(otherCandidate))
    {
        if (otherBody->objectLayer == PhysicsLayers::NON_MOVING ||
            otherBody->objectLayer == PhysicsLayers::INVISIBLE_WALL)
            return;
    }

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

    // ★ チェイン加算（上限で頭打ち）。撃破範囲・受付の厳しさ・移動速度は、この段数を
    //    各所（DroneManagerの範囲撃破 / PlayerDashの受付窓 / 移動速度倍率）が参照する。
    //    Publish より前に加算し、購読側が最新の段数を見られるようにする。
    if (evade->chainCount < evade->maxChain) evade->chainCount++;
    evade->idleTimer = 0.0f; // 成立したので静止タイマーをリセット

    _world->GetEventBus().Publish(JustEvadeSuccessEvent{playerCandidate, otherCandidate});

    CCL_LOG_SUCCESS(LogCategory::Game, "JUST EVADE SUCCESS!! PlayerID: %llu  (CHAIN x%d)", playerCandidate, evade->chainCount);
}

// イベントを受け取り終わった後の状態を初期化するため、最も遅い Cleanup フェーズに登録する
REGISTER_LOGIC_SYSTEM(JustEvadeSystem, Priority::LogicStage::L07_Cleanup);