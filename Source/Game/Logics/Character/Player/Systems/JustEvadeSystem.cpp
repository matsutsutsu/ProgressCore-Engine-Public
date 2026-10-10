#include "JustEvadeSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Components/EntityLabel.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Core/Math/StringHash.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"

#include "Game/Core/GameEvents.h"
#include "Game/Logics/Combat/Components/CombatComponents.h" // HitboxComponent（攻撃判定がアクティブな時だけ成立させるため）
#include "Engine/Camera/CameraEvents.h"          // FOVパンチ要求（カメラへの演出指示）
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Time/TimeService.h" // 時間操作は意図ファサード経由
#include "Game/Logics/Effects/Components/JustEvadeEffectComponent.h"
#include "Game/Logics/Character/Player/Components/CleaveComponent.h"
#include "Engine/Physics/Components/RigidBody/JoltRigidBodyComponent.h" // 壁/地形との接触を除外するため

using namespace CCL::ECS;

bool JustEvadeSystem::IsDroneHostile(const DroneComponent *drone) const
{
    if (!drone) return false;
    if (drone->knockbackTimer > 0.0f) return false; // 切り払いで吹き飛ばされている間は攻撃とみなさない
    // 突撃中の状態だけを攻撃とみなす
    return drone->currentState == DroneState::FireCharge;
}

// AnimParameters へ書くトリガーのハッシュ
static const uint32_t HASH_TRIG_JUST_EVADE = CCL::Utils::HashString("OnJustEvade");

void JustEvadeSystem::Initialize()
{
    // EventBus から物理の接触を購読する。
    // ここで判定まで済ませると処理が発行元(JoltCollisionEventSystem)のタスク上で走り、
    // この System が申告した Write 型がスケジューラから見えなくなる。溜めるだけにする。
    ListenEvent<JoltCollisionEvent>([this](const JoltCollisionEvent &ev) {
        m_pendingContacts.push_back(ev);
    });

    // ジャスト回避が成立した瞬間の処理
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        // 1. ラッシュ(踏み込み)を開始する。全停止は入れず、即“ぎゅいん”と踏み込ませたいので
        //    プレイヤーの完全停止(HitStopEntity)は使わない。世界スローは PlayerDashSystem が
        //    毎フレーム SustainWorldSlow で駆動するが、成立フレームの1フレームだけ空かないよう
        //    ここでも一度スローを点火しておく（受け取りは次フレームの PlayerDashSystem）。
        if (auto *dash = m_world->GetComponent<DashComponent>(ev.playerID))
        {
            dash->isDashing               = false; // 回避ダッシュを終えてラッシュへ移る
            dash->isJustEvadeWindowActive = false;
            dash->isInvincible            = true; // ラッシュ中はスロー中のドローンに当たらないよう無敵
            dash->isFlurryLunging         = true;  // フェーズA：踏み込み
            dash->isFlurryPlanting        = false;
            dash->isFlurryFocusing        = false;
            dash->flurryTimer             = dash->flurryLungeDuration;

            Time::SustainWorldSlow(*m_world, dash->flurryWorldScale, dash->flurrySlowRecover);
        }

        // 3. ジャスト回避専用のモーションへ遷移させる。
        if (auto *animParams = m_world->GetComponent<AnimParametersComponent>(ev.playerID))
        {
            animParams->SetTrigger(HASH_TRIG_JUST_EVADE);
        }

        // 4. 画面演出（ビネット）を立てる。実際のフェードイン／アウトは JustEvadeEffectSystem が
        //    weight を実時間で動かして行う。降ろす（active=false）のはラッシュ終了時に PlayerDashSystem。
        if (auto *fx = m_world->GetSingletonComponent<JustEvadeEffectComponent>())
        {
            fx->flurryActive = true;
        }

        // TODO(2026-07-16): ジャスト回避の効果音と残像を足す
    });
}

std::vector<CCL::ECS::TypeID> JustEvadeSystem::GetWriteTypes() const
{
    // テンプレート引数から作られる分（JustEvade/Dash/TimeState）に、
    // JustEvadeSuccessEvent のハンドラが書く分を足す。ハンドラは CheckJustEvade から
    // 同期で Publish されるので、この System の Update の中で走る。
    // CleaveComponent は PlayerCleaveSystem のハンドラ（切り払いゲージの加算）が書く。
    std::vector<CCL::ECS::TypeID> types = IfSystem::GetWriteTypes();
    types.push_back(TypeInfo<AnimParametersComponent>::ID());
    types.push_back(TypeInfo<JustEvadeEffectComponent>::ID());
    types.push_back(TypeInfo<CleaveComponent>::ID());
    return types;
}

void JustEvadeSystem::Update(float rawDt)
{
    // 1. このフレームの接触を判定する（A/B のどちらがプレイヤーでもよいよう両方向で調べる）。
    //    2. より先に行う。
    for (const JoltCollisionEvent &ev : m_pendingContacts)
    {
        CheckJustEvade(ev.entityA, ev.entityB);
        CheckJustEvade(ev.entityB, ev.entityA);
    }
    m_pendingContacts.clear();

    // 2. 成立フラグと、直前の相手の記録を片付ける。
    ForEach([&](JustEvadeComponent &evade, DashComponent &dash, const TimeStateComponent &time) {
        // 成立フラグは「このダッシュで成立済みか」を表すので、ダッシュしていない間は下ろす。
        // 成立時は JustEvadeSuccessEvent のハンドラが isDashing を false にするので、そのフレームで下りる。
        if (!dash.isDashing)
        {
            evade.isJustSuccess = false;
        }

        if (evade.resetTimer > 0.0f)
        {
            evade.resetTimer -= time.localDt;

            // 時間が過ぎたら直前の相手の記録を消し、同じ相手にも再び成立できるようにする
            if (evade.resetTimer <= 0.0f)
            {
                evade.lastEvadedTarget = CCL::ECS::InvalidEntityID;
            }
        }
    });
}

void JustEvadeSystem::CheckJustEvade(EntityID playerCandidate, EntityID otherCandidate)
{
    auto *evade = m_world->GetComponent<JustEvadeComponent>(playerCandidate);
    auto *dash  = m_world->GetComponent<DashComponent>(playerCandidate);

    if (!evade || !dash) return;

    // 1. ジャスト回避の受付時間内か
    if (!dash->isJustEvadeWindowActive) return;

    // 2. このダッシュで既に成立していないか（isJustSuccess は「このダッシュで成立済み」を表す）
    if (evade->isJustSuccess) return;

    // 3. 直前に成立した相手と同じでないか（同じ相手に続けて成立させない）
    if (evade->lastEvadedTarget == otherCandidate) return;

    // 4. 壁や地形など、動かない物との接触では成立させない。
    //   これが無いと、落下防止の見えない壁(INVISIBLE_WALL)や地形(NON_MOVING)に
    //   ダッシュでぶつかっただけでジャスト回避が成立し、接近で内側へ移動させられる。
    //   ドローンやボスの攻撃は別のレイヤーなので、ここは通る。
    if (auto *otherBody = m_world->GetComponent<JoltRigidbodyComponent>(otherCandidate))
    {
        if (otherBody->objectLayer == PhysicsLayers::NON_MOVING ||
            otherBody->objectLayer == PhysicsLayers::INVISIBLE_WALL)
            return;
    }

    // 5. 相手がこのフレームで有効な攻撃のときだけ成立させる。
    //   ・ドローン         : 突撃中(FireCharge)だけ。隊列で待機しているドローンにぶつかっても成立しない。
    //   ・Hitbox(ボスの剣) : 判定が開いているときだけ。剣のコライダーは振っていなくても常にあるため、
    //                        ここを見ないと、攻撃していない剣にぶつかっただけで成立する。
    //   ・どちらでもない相手（ボス本体・障害物など）は攻撃ではないので成立させない。
    if (auto *otherDrone = m_world->GetComponent<DroneComponent>(otherCandidate))
    {
        if (!IsDroneHostile(otherDrone)) return;
    }
    else if (auto *otherHitbox = m_world->GetComponent<HitboxComponent>(otherCandidate))
    {
        if (!otherHitbox->isActive) return; // 攻撃判定が開いていない剣は無害
    }
    else
    {
        return; // 攻撃判定を持たない相手にはジャスト回避を成立させない
    }

    // 成立。受付を閉じて成立フラグを立て、このダッシュ中の他の相手との接触を無視する
    dash->isJustEvadeWindowActive = false;
    evade->isJustSuccess          = true;

    evade->lastEvadedTarget = otherCandidate;
    evade->resetTimer       = 1.0f; // 同じ相手に再び成立できるまでの時間（秒）

    m_world->GetEventBus().Publish(JustEvadeSuccessEvent{playerCandidate, otherCandidate});

    // カメラへは「画角を一瞬広げてほしい」という Engine 側の要求だけを送る。
    // （カメラが JustEvadeSuccessEvent を直接購読すると Engine→Game の依存になるため）
    m_world->GetEventBus().Publish(CameraFovPunchRequestEvent{playerCandidate, 130.0f});

    CCL_LOG_SUCCESS(LogCategory::Game, "JUST EVADE SUCCESS!! Player: %s",
                    CCL::EntityLabel(*m_world, playerCandidate).c_str());
}

// 接触を配送する JoltCollisionEventSystem と同じ L05_Collision に置き、GetRunAfter でその後に走らせる。
//   成立時に書く DashComponent（接近の開始）は PlayerDashSystem(L02_Update) が次のフレームで読む。
REGISTER_LOGIC_SYSTEM(JustEvadeSystem, Priority::LogicStage::L05_Collision);
