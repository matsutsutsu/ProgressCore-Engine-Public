#include "JustEvadeSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Core/Math/StringHash.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

#include "Game/Logics/GameEvent/GameEvents.h"
#include "Engine/Camera/CameraEvents.h" // FOVパンチ要求（カメラへの演出指示）
#include "Engine/Time/TimeState.h"
#include "Engine/Time/TimeService.h" // 時間操作は意図ファサード経由
#include "Game/Logics/Effects/Components/JustEvadeEffectComponent.h"
#include "Engine/Physics/Components/RigidBody/JoltRigidBodyComponent.h" // 壁/地形との接触を除外するため

#include <cstdio>
#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::ECS;

// そのドローンが「今ぶつかったらジャスト回避が成立する相手」かを判定する門番。
// 陣形を組んでいるだけのドローンに突っ込んでも回避が暴発しないようにする。
bool JustEvadeSystem::IsDroneHostile(const DroneComponent *drone) const
{
    if (!drone) return false;
    if (drone->knockbackTimer > 0.0f) return false; // 切り払いで吹き飛ばし中は無害（誤ジャスト回避を防ぐ）
    // 突撃系の状態だけを「危険」とみなす
    return drone->currentState == DroneState::FireCharge;
}


// アニメーション掲示板へ書き込むトリガーのハッシュ
static const uint32_t HASH_TRIG_JUST_EVADE = CCL::Utils::HashString("OnJustEvade");


void JustEvadeSystem::Initialize()
{
    // EventBus から物理衝突の報告を購読する。
    // ここで判定まで済ませると処理が発行元(JoltCollisionEventSystem)のタスク上で走り、
    // 本システムが申告した Write 型がスケジューラから見えなくなる。積むだけに留める。
    ListenEvent<JoltCollisionEvent>([this](const JoltCollisionEvent& ev) {
        m_pendingContacts.push_back(ev);
    });

    // 被弾でチェイン切断（ボス／ドローンいずれの攻撃を食らっても初期化）
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &ev) {
        if (ev.isPlayer) BreakChain(ev.targetID);
    });


    // JUST回避が成功した瞬間のイベントをリッスン
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        // 1. プレイヤーを一瞬止め、世界全体をウィッチタイム（0.5秒・10%速度）にする。
        Time::HitStopEntity(*m_world, ev.playerID, 0.15f); // 自機だけ完全停止(150ms)
        Time::HitStop(*m_world, 0.5f, 0.1f);               // 世界全体を0.5秒だけ10%へ

        // =======================================================
        //  1.5 接近（ジャスト回避の近道）の起動
        //      現在のダッシュを終了し、「殴れる位置に着くための接近時間」へ移行する。
        //      実際の移動と時計の打ち消しは PlayerDashSystem が毎フレーム駆動する。
        // =======================================================
        if (auto *dash = m_world->GetComponent<DashComponent>(ev.playerID))
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
        if (auto *animParams = m_world->GetComponent<AnimParametersComponent>(ev.playerID))
        {
            animParams->SetTrigger(HASH_TRIG_JUST_EVADE);
        }

        // =======================================================
        // 3. 画面演出（ウィッチタイム）のトリガー
        // =======================================================
        // シーンに置いた JustEvadeEffectComponent(singleton) の timer を立てるだけ。
        // 実際の色調/放射ブラー/色収差/ビネットの駆動は JustEvadeEffectSystem(R02) が
        // 毎フレーム行い、演出後は元の PP 値へ自動復元する。
        if (auto *fx = m_world->GetSingletonComponent<JustEvadeEffectComponent>())
        {
            fx->Trigger();
        }

        // TODO(任意): 音響（甲高いガラス音）や残像の追加はここに足す。
        // m_world->GetEventBus().Publish(SoundRequestEvent{"event:/SFX/JustEvade", ev.playerID});
    });


}

std::vector<CCL::ECS::TypeID> JustEvadeSystem::GetWriteTypes() const
{
    // テンプレート引数から自動生成された分（JustEvade/Dash/MovementInput/TimeState）に、
    // JustEvadeSuccessEvent ハンドラが書く分を足す。ハンドラは CheckJustEvade から
    // 同期 Publish される＝本システムの Update 内で走るので、ここでの申告が正しい。
    std::vector<CCL::ECS::TypeID> types = IfSystem::GetWriteTypes();
    types.push_back(TypeInfo<AnimParametersComponent>::ID());
    types.push_back(TypeInfo<JustEvadeEffectComponent>::ID());
    return types;
}

void JustEvadeSystem::Update(float rawDt)
{
    // 1. このフレームの接触を判定する（双方向でプレイヤー側を探す）。
    //    クリーンアップより先に回すこと。成立フラグを立ててから後段の後始末に渡す。
    for (const JoltCollisionEvent &ev : m_pendingContacts)
    {
        CheckJustEvade(ev.entityA, ev.entityB);
        CheckJustEvade(ev.entityB, ev.entityA);
    }
    m_pendingContacts.clear();

    // 2. フラグのクリーンアップ
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
// @note ここだけは Update の外（DamageTakenEvent の発行元＝DamageResolutionSystem の
//       タスク・L06_DamageApply）で走る。書くのは chainCount/idleTimer だけで、
//       L06 に JustEvadeComponent を触るシステムは他に無いため競合しない。
//       バッファリングして次フレームに回すとチェイン切断が1フレーム遅れるので即時のままにする。
void JustEvadeSystem::BreakChain(EntityID playerID)
{
    if (auto *evade = m_world->GetComponent<JustEvadeComponent>(playerID))
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
    // 明るい背景でも数字が読めるよう、1px ずらした黒を先に敷いてから本体を重ねる
    dl->AddText(ImGui::GetFont(), 34.0f, ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 200), buf);
    dl->AddText(ImGui::GetFont(), 34.0f, ImVec2(x, y), col, buf);
}
#else
void JustEvadeSystem::OnGui() {}
#endif

void JustEvadeSystem::CheckJustEvade(EntityID playerCandidate, EntityID otherCandidate)
{
    auto *evade = m_world->GetComponent<JustEvadeComponent>(playerCandidate);
    auto *dash  = m_world->GetComponent<DashComponent>(playerCandidate);

    if (!evade || !dash) return;

    // 1. 【ガード条件】ジャスト回避が可能な「有効ウィンドウ」内か？
    if (!dash->isJustEvadeWindowActive) return;

    // 2. 【ガード条件】このダッシュですでに成功しているなら処理しない
    // ここが最も重要：evade->isJustSuccess を「このダッシュ内での成功フラグ」として扱います
    if (evade->isJustSuccess) return;

    // 3. 【ガード条件】同じ敵に対する多段発動を防ぐ (既存のブラックリスト機能)
    if (evade->lastEvadedTarget == otherCandidate) return;

    // 3.5【ガード条件】壁・地形など「静止した非攻撃物」との接触では絶対に成立させない。
    //   これが無いと、落下防止の見えない壁(INVISIBLE_WALL)や地形(NON_MOVING)に
    //   ダッシュで突っ込んだだけでジャスト回避が誤発動し、接近(isApproaching)で
    //   内側へ強制移動させられてしまう。ドローンやボスの攻撃は別レイヤーなので通過する。
    if (auto *otherBody = m_world->GetComponent<JoltRigidbodyComponent>(otherCandidate))
    {
        if (otherBody->objectLayer == PhysicsLayers::NON_MOVING ||
            otherBody->objectLayer == PhysicsLayers::INVISIBLE_WALL)
            return;
    }

     // 相手がドローンで、かつ「攻撃中」でなければ成立させない
    auto *otherDrone = m_world->GetComponent<DroneComponent>(otherCandidate);
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

    // チェイン加算（上限で頭打ち）。撃破範囲・受付の厳しさ・移動速度は、この段数を
    //    各所（DroneManagerの範囲撃破 / PlayerDashの受付窓 / 移動速度倍率）が参照する。
    //    Publish より前に加算し、購読側が最新の段数を見られるようにする。
    if (evade->chainCount < evade->maxChain) evade->chainCount++;
    evade->idleTimer = 0.0f; // 成立したので静止タイマーをリセット

    m_world->GetEventBus().Publish(JustEvadeSuccessEvent{playerCandidate, otherCandidate});

    // カメラへは「画角を瞬間的に広げてほしい」という汎用要求だけを伝える。
    // （カメラが JustEvadeSuccessEvent を直接購読すると Engine→Game 依存になるため）
    m_world->GetEventBus().Publish(CameraFovPunchRequestEvent{playerCandidate, 130.0f});

    CCL_LOG_SUCCESS(LogCategory::Game, "JUST EVADE SUCCESS!! PlayerID: %llu  (CHAIN x%d)", playerCandidate, evade->chainCount);
}

// 接触判定と同じ L05_Collision に置く。
//   以前は L07_Cleanup だったが、判定本体は JoltCollisionEvent のコールバック＝L05 で
//   走っており、申告した Write 型が実際の実行箇所に効いていなかった。判定を Update へ
//   移したうえでステージも L05 に合わせ、申告と実行を一致させる。
//   後始末(フラグ/チェイン)の消費側は DroneManagerSystem・PlayerDashSystem いずれも
//   L02_Update なので、L05 でも L07 でも「次フレームで反映」となり挙動は変わらない。
REGISTER_LOGIC_SYSTEM(JustEvadeSystem, Priority::LogicStage::L05_Collision);
