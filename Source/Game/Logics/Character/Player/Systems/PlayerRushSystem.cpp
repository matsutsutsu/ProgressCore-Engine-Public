#include "PlayerRushSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/TimeService.h"      // 世界スロー(SustainWorldSlow)
#include "Game/Core/GameEvents.h"         // JustEvadeSuccessEvent
#include "Game/Logics/Effects/Components/JustEvadeEffectComponent.h" // ビネット演出の要求フラグ
#include "Game/Logics/Combat/Components/DeadTag.h" // 詰め先が死んでいないか判定

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

std::vector<CCL::ECS::TypeID> PlayerRushSystem::GetWriteTypes() const
{
    // 画面演出のコンポーネントも書くので申告
    auto types = Base::GetWriteTypes();
    types.push_back(TypeInfo<JustEvadeEffectComponent>::ID());
    return types;
}

void PlayerRushSystem::Initialize()
{
    // ジャスト回避の成功を購読する。
    // 成功は別システム(JustEvadeSystem)のタスク上で届くので、ここでは溜めるだけにして、
    // 実際の開始は Updateで行う。
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        m_pendingStarts.push_back(ev.playerID);
    });
}

void PlayerRushSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 届いたジャスト回避成功を受けて、対象のラッシュを開始する（操作不能フェーズから）
    // 2. ラッシュ中はスローを維持し、フェーズごとに操作を切り替え、自由が終わったら終える
    // 3. ラッシュ中かどうかを画面演出（ビネット）へ伝える

    // ---------------------------------------------------------
    // 1. ラッシュの開始
    // ---------------------------------------------------------
    for (EntityID id : m_pendingStarts)
    {
        if (auto *rush = m_world->GetComponent<RushComponent>(id))
        {
            StartRush(*rush);
        }
    }
    m_pendingStarts.clear();

    // ---------------------------------------------------------
    // 2. 時間制御・踏み込み・フェーズ遷移
    // ---------------------------------------------------------
    const TimeContext *timeCtx = m_world->HasResource<TimeContext>()
                                     ? &m_world->GetResource<TimeContext>()
                                     : nullptr;

    bool anyActive = false;

    ForEach([&](RushComponent              &rush,
                TimeStateComponent         &time,
                CharacterMovementComponent &movement,
                const PlayerComponent      &player,
                const PlayerInputComponent &input,
                const TransformComponent   &transform) {
        // ラッシュ中でなければ何もしない
        if (!rush.isActive)
        {
            return;
        }

        // スローはフェーズに依らず一律。毎フレーム維持する。
        DriveTimeScale(rush, time, timeCtx);

        // フェーズごとに「操作」だけを切り替える。
        //   操作不能: その場で止める。 準備: 攻撃で敵へ踏み込む。 自由: 通常操作（ここでは何もしない）。
        switch (rush.phase)
        {
        case RushPhase::Locked:
            StopPlayer(movement);
            break;
        case RushPhase::Ready:
            DriveCloseIn(rush, movement, input, player, Vector3(transform.position), dt);
            break;
        case RushPhase::Free:
            break;
        }

        // 今のフェーズの持ち時間が尽きたら、次のフェーズへ進める（自由の後は終了）。
        rush.timer -= dt;
        if (rush.timer <= 0.0f)
        {
            AdvancePhase(rush, time);
        }

        // 終了したフレームは isActive が false になるので、そのフレームから演出も降り始める。
        anyActive = anyActive || rush.isActive;
    });

    // ---------------------------------------------------------
    // 3. 画面演出へ伝える
    // ---------------------------------------------------------
    // rushActive を立てている間だけ、JustEvadeEffectSystem がビネットをピークへ寄せる。
    if (auto *fx = m_world->GetSingletonComponent<JustEvadeEffectComponent>())
    {
        fx->rushActive = anyActive;
    }
}

// ---------------------------------------------------------------------------
// フェーズ
// ---------------------------------------------------------------------------

void PlayerRushSystem::StartRush(RushComponent &rush)
{
    // 操作不能フェーズから始める

    rush.isActive = true;
    rush.phase    = RushPhase::Locked;
    rush.timer    = rush.lockedDuration;
}

void PlayerRushSystem::AdvancePhase(RushComponent &rush, TimeStateComponent &time)
{
    switch (rush.phase)
    {
    case RushPhase::Locked:
        rush.phase = RushPhase::Ready;
        rush.timer = rush.readyDuration;
        break;

    case RushPhase::Ready:
        rush.phase = RushPhase::Free;
        rush.timer = rush.freeDuration;
        break;

    case RushPhase::Free:
        EndRush(rush, time);
        break;
    }
}

// ---------------------------------------------------------------------------
// 時間制御
// ---------------------------------------------------------------------------

void PlayerRushSystem::DriveTimeScale(RushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx)
{
    // 世界を worldScale まで遅くする。毎フレーム要求し続けることで、その遅さを維持する。
    Time::SustainWorldSlow(*m_world, rush.worldScale, rush.slowRecover);

    // プレイヤーだけ実時間へ戻す。
    // localDt = rawDt × worldScale × cinematicScale なので、cinematicScale = playerScale / worldScale と
    // すると worldScale が約分され、プレイヤーの時計だけが playerScale（既定 1.0＝実時間）で進む。
    const float currentWorld = timeCtx
                                   ? (timeCtx->globalScale * timeCtx->groupScales[static_cast<size_t>(time.group)])
                                   : 1.0f;

    // 除数の下限を worldScale で止める。
    // 他のスロー要因で世界が worldScale より深く落ちても、cinematicScale が過大にならないようにする。
    const float worldFloor = (std::max)(currentWorld, rush.worldScale);

    time.cinematicScale = rush.playerScale / worldFloor;
}

void PlayerRushSystem::EndRush(RushComponent &rush, TimeStateComponent &time)
{
    rush.isActive     = false;
    rush.timer        = 0.0f;
    rush.closeInTimer = 0.0f;

    // 世界スローはもう要求しない（呼ぶのをやめれば slowRecover のイーズで等速へ戻る）。
    // プレイヤーのスロー打ち消しもやめる。
    time.cinematicScale = 1.0f;
}

void PlayerRushSystem::StopPlayer(CharacterMovementComponent &movement)
{
    // 操作不能フェーズは成立の「キメ」。移動の意思を打ち消し、その場で止める。
    // overrideSpeed=0 で速度を 0 に上書きする（consume-on-read なので毎フレーム書く）。
    movement.desiredVelocity = Vector3::Zero;
    movement.overrideSpeed   = 0.0f;
}

// ---------------------------------------------------------------------------
// 間合い詰め
// ---------------------------------------------------------------------------

void PlayerRushSystem::DriveCloseIn(RushComponent &rush, CharacterMovementComponent &movement,
                                    const PlayerInputComponent &input, const PlayerComponent &player,
                                    const Vector3 &playerPos, float dt)
{
    // --- 開始 ---
    // 攻撃を押した瞬間に、詰め先と初期の向きを決めて踏み込みを仕込む。
    //   詰め先: ロックオン中で生存している敵。無ければ Invalid（＝向いている方向へ踏み込む）。
    //   向き  : 今キャラが向いている方向（0 が +Z の規約）。ロックオン時は継続中に上書きする。
    // 踏み込み中にもう一度押しても、今の踏み込みが終わるまでは仕込み直さない。
    if (input.attackPressed && rush.closeInTimer <= 0.0f)
    {
        // ターゲットを探す
        rush.closeInTarget = PickCloseInTarget(player);

        const float yaw   = player.currentYaw;
        // 
        rush.closeInDir   = Vector3(std::sin(yaw), 0.0f, std::cos(yaw));
        rush.closeInTimer = rush.closeInMaxTime;
    }

    if (rush.closeInTimer <= 0.0f)
    {
        return;
    }

    // --- 向きの決定 ---
    // ロックオン先が居る間は、毎フレームその位置へ向け直す（敵が動いても追う）。
    // 間合い（closeInStopDistance）に入ったら、その場で踏み込みを終える＝止まる。
    // ロックオンが無い／敵を見失ったときは、仕込んだ向き（closeInDir）のまま進む。
    Vector3 dir = rush.closeInDir;
    if (IsCloseInTargetAlive(rush.closeInTarget))
    {
        // ターゲットが生存している場合、その位置へ向け直す
        if (const auto *enemyTrans = m_world->GetComponent<TransformComponent>(rush.closeInTarget))
        {
            Vector3 to = Vector3(enemyTrans->position) - playerPos;
            to.y       = 0.0f;
            const float dist = to.Length();

            // 間合いに入ったら間合いを詰め終える
            if (dist <= rush.closeInStopDistance)
            {
                rush.closeInTimer = 0.0f;
                return;
            }
            // 方向ベクトルを正規化して覚えておく
            if (dist > 1e-3f)
            {
                dir             = to / dist;
                rush.closeInDir = dir; // 敵を見失っても最後の向きで進めるよう覚えておく
            }
        }
    }

    // --- 前進 ---
    // 動かす側へ「この向きへ closeInSpeed で進め」と毎フレーム要求する。
    // overrideSpeed は consume-on-read なので、終われば書かなくなるだけで自然に止まる。
    movement.desiredVelocity = dir;
    movement.overrideSpeed   = rush.closeInSpeed;
    rush.closeInTimer -= dt;
}

CCL::ECS::EntityID PlayerRushSystem::PickCloseInTarget(const PlayerComponent &player) const
{
    // lockOnTargetID は EntityLink。EntityID へ暗黙変換して渡す。
    const EntityID id = player.lockOnTargetID;
    // 正直生存チェックしなくてそのまま入れるのでもいいけど
    // 今後チュートリアルで別の雑魚敵とか実装するときのための保険
    return IsCloseInTargetAlive(id) ? id : InvalidEntityID;
}

bool PlayerRushSystem::IsCloseInTargetAlive(CCL::ECS::EntityID id) const
{
    return id != InvalidEntityID && m_world->IsEntityValid(id) && !m_world->HasComponent<Tag::DeadTag>(id);
}

// 入力・ダッシュと同じ L02_Update。物理(L03以降)より前に時間倍率を確定させる。
REGISTER_LOGIC_SYSTEM(PlayerRushSystem, Priority::LogicStage::L02_Update);
