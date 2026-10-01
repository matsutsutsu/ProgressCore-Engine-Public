#include "DroneFormationSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Attack.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h"
#include "Game/Logics/AI/Drone/Formation/Formations_DataDriven.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Squad.h"

#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Time/TimeState.h"

#include <algorithm>

#include <imgui.h>

using namespace CCL::ECS;
using namespace CCL::AI::Formation;

namespace
{
    // 予告の溜めの上限 [秒]。振幅は移動層で頭打ちになるので、値が際限なく育たないための蓋。
    constexpr float kWindupTimerCap = 10.0f;
} // namespace

void DroneFormationSystem::Initialize()
{
    // このシステムが評価する陣形クラスを登録する。陣形を追加したときは
    // 各 Formations_*.h の RegisterAll() に1行足せば全シーンへ自動反映される。
    CCL::AI::Formation::Basic::RegisterAll();
    CCL::AI::Formation::Attack::RegisterAll(); // データで書けない攻撃陣形（弾幕）
    CCL::AI::Formation::Squad::RegisterAll();  // 小隊陣形（5機編隊）

    // データ陣形は最後。C++ 実装の枠を上書きしないことを登録側でも確かめられる
    // （Has() で埋まっている枠を弾くので、順序が逆だと検出できない）。
    CCL::AI::Formation::Data::RegisterAll();

    CCL_LOG_INFO(LogCategory::AI, "[DroneFormationSystem] 陣形クラス登録完了");
}

FormationContext DroneFormationSystem::MakeContext(float dt)
{
    FormationContext ctx;
    ctx.time = m_elapsed;
    ctx.dt   = dt; // 攻撃の進行度（ヒンジ回転など）を進めるのに使う

    // 「今撃て」信号。狙いを固定するタイミングの判定に使う。ボス位置とあわせて
    // BossCommandComponent を持つ最初の Entity から取る。
    bool attackActive = false;
    bool hasBoss      = false;

    {
        auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
        for (auto b : bossView)
        {
            if (auto *t = m_world->GetComponent<TransformComponent>(b))
            {
                ctx.bossPos   = t->GetWorldPosition(); // position はローカル。陣形の中心はワールドで置く
                m_bossBodyPos = ctx.bossPos;           // 下で地面の高さに揃える前の、実際の位置
                hasBoss       = true;
            }
            // 陣形の中心はボスの足元の高さに置く。跳躍や滞空で浮くと中心ごと持ち上がり、
            // 地面を薙ぐ技がプレイヤーの頭上を素通りする（各機はボス相対で並ぶため）。
            // 踏み切った地点の高さがそのときの地面なので、浮いている間はそれで代用する。
            //   突進で浮く間は揃えない。ドローンもボスと一緒に持ち上がるのが狙いの見た目。
            //   頭上円陣のような隊形は、揃える前の m_bossBodyPos を使う（FollowsBossHeight）。
            if (const auto *act = m_world->GetComponent<BossActionComponent>(b))
            {
                if (act->currentState == BossActionState::Leap && act->leapPhase != BossLeapPhase::Windup)
                    ctx.bossPos.y = act->leapTakeoffPos.y;
            }

            if (auto *c = m_world->GetComponent<BossCommandComponent>(b))
                attackActive = c->droneAttackActive;
            break;
        }
    }

    // 陣形の中心が動いている速度。前フレームの中心との差から出すので、上の「地面の高さに揃える」も
    // そのまま反映される（跳躍中に中心を持ち上げないなら、縦の速度も乗せない）。
    // 実際の位置のほう（頭上円陣などが使う）も同じ方法で別に測る。
    //   一定の速さを超えたら瞬間移動（エディタでの配置・リスポーン）とみなして乗せない。
    //   乗せると、ドローンが1フレームだけ数百 m/s で飛んでいく。
    constexpr float kTeleportSpeed = 300.0f; // ボスの最速（突進 150 m/s 前後）より十分上
    auto velocityOf = [&](const DirectX::SimpleMath::Vector3 &now, const DirectX::SimpleMath::Vector3 &prev) {
        if (!hasBoss || !m_hasPrevBossPos || dt <= 0.0f) return DirectX::SimpleMath::Vector3::Zero;
        const DirectX::SimpleMath::Vector3 v = (now - prev) / dt;
        return (v.LengthSquared() <= kTeleportSpeed * kTeleportSpeed) ? v : DirectX::SimpleMath::Vector3::Zero;
    };
    ctx.bossVelocity   = velocityOf(ctx.bossPos, m_prevBossPos);
    m_bossBodyVelocity = velocityOf(m_bossBodyPos, m_prevBodyPos);

    m_prevBossPos    = ctx.bossPos;
    m_prevBodyPos    = m_bossBodyPos;
    m_hasPrevBossPos = hasBoss;

    // プレイヤーが居ないフレームは playerPos を配らない（既定のまま＝原点を狙わせない）。
    if (m_world->HasResource<BattleContext>())
    {
        auto &battle = m_world->GetResource<BattleContext>();
        if (battle.HasPlayer())
        {
            ctx.playerPos      = battle.playerPos;
            ctx.playerCenter   = battle.playerCenter;
            ctx.playerVelocity = battle.playerVelocity;
        }
    }

    // 小隊のアンカー（SquadUpdateSystem が今フレーム更新済み）。ここでは読むだけ。
    if (m_world->HasResource<SquadRegistry>())
        ctx.squads = &m_world->GetResource<SquadRegistry>();

    // 狙う向きを「撃つと決めた瞬間」に1回だけ固定する。予備動作の間は追い続ける
    // （形がプレイヤーを向くので、どちらへ避けるかを読める）。信号が立った後は
    // 動かさないので、避ければ当たらない。
    if (attackActive && !m_prevAttackActive)
    {
        DirectX::SimpleMath::Vector3 aim = ctx.playerPos - ctx.bossPos;
        aim.y                            = 0.0f;
        if (aim.LengthSquared() < 0.001f) aim = DirectX::SimpleMath::Vector3::UnitZ;
        aim.Normalize();

        m_lockedAimDir = aim;
        m_aimLocked    = true;
    }
    else if (!attackActive)
    {
        m_aimLocked = false;
    }
    m_prevAttackActive = attackActive;

    ctx.aimLocked = m_aimLocked;
    ctx.aimDir    = m_lockedAimDir;

    return ctx;
}

void DroneFormationSystem::Update(float rawDt)
{
    ZoneScopedN("DroneFormationSystem::Update");

    // ドローンと同じ時計で進める。rawDt のままだと、ジャスト回避の世界スロー中も陣形だけが
    // 等速で動き、持ち場へ張り付く技（縦割り等）はスローにならない。
    // ボスの localDt は使わない（命中でボスだけ止まると、陣形まで止まってしまう）。
    float dt = rawDt;
    if (m_world->HasResource<TimeContext>())
    {
        const auto &time = m_world->GetResource<TimeContext>();
        dt *= time.globalScale * time.groupScales[static_cast<size_t>(TimeGroup::Enemy)];
    }

    m_elapsed += dt;

    auto &registry = FormationRegistry::Instance();

    // 陣形へ渡す情報は2通り。違うのはボスの位置と速度だけで、陣形ごとにどちらかを渡す。
    //   groundCtx … 跳躍・滞空中も中心を地面の高さに残す（既定。地面を薙ぐ技など）
    //   bodyCtx   … ボスの実際の高さを中心にする（FollowsBossHeight。頭上円陣など）
    const FormationContext groundCtx = MakeContext(dt);
    FormationContext       bodyCtx   = groundCtx;
    bodyCtx.bossPos                  = m_bossBodyPos;
    bodyCtx.bossVelocity             = m_bossBodyVelocity;

    // ポリモーフィズムは「陣形を引くとき」だけ。ループ自体は連続アクセス。
    ForEach([&](DroneComponent &drone) {
        IFormation *formation = registry.Get(drone.currentFormation);
        if (!formation)
        {
            // 未登録の陣形（Hidden等）は何もしない。溜めだけは戻す（抱えたままだと
            // 次に出てきた瞬間から全開で震える）。
            drone.windupTimer       = 0.0f;
            drone.telegraphTime     = 0.0f;
            drone.followFeedForward = DirectX::SimpleMath::Vector3::Zero;
            return;
        }

        const FormationContext &ctx = formation->FollowsBossHeight() ? bodyCtx : groundCtx;

        // 先渡しの速度は Calculate の前に入れる。中心と同じ情報から取るので、頭上円陣なら
        // 跳躍の上下も乗る。ボス以外を基準にする陣形（小隊）は、Calculate の中で自分の基準の
        // 速度に上書きする。
        drone.followFeedForward = formation->FollowsBoss() ? ctx.bossVelocity * m_bossCarry : DirectX::SimpleMath::Vector3::Zero;

        formation->Calculate(drone, ctx); // targetPosition を書き込む

        // 予告（震え）の溜めを進める。震えを使う技で、持ち場に着いていて、まだ撃つ合図が
        // 来ていない。この3つが揃う時間帯が予備動作にあたる。
        //   hasReachedSlot は「一度でも着いたか」のラッチなので陣形を切り替えた直後も
        //   true のままで、溜めは BT が陣形を指示した時点から始まって Wait(予告) の
        //   長さと一致する。突撃から帰る途中だけ false に落ちるので帰りながらは震えない。
        const bool telegraphing =
            formation->UsesShiverTelegraph() && drone.hasReachedSlot && !drone.wantsAttack;

        drone.windupTimer = telegraphing
                                ? (std::min)(kWindupTimerCap, drone.windupTimer + dt)
                                : 0.0f;

        // 予告の時計。震わせる設定かどうかに関係なく、同じ時間帯（着いてから撃つまで）で進む。
        drone.telegraphTime = (drone.hasReachedSlot && !drone.wantsAttack)
                                  ? (std::min)(kWindupTimerCap, drone.telegraphTime + dt)
                                  : 0.0f;
    });
}

void DroneFormationSystem::OnGui()
{
    ImGui::SliderFloat("ボスの速度を乗せる割合", &m_bossCarry, 0.0f, 1.0f);
    ImGui::TextDisabled("1 でボスと一緒に動く。下げるほど少し遅れて引きずられる");
    ImGui::TextDisabled("（小隊突撃は先頭機を基準にするので対象外）");
}

REGISTER_LOGIC_SYSTEM(DroneFormationSystem, Priority::LogicStage::L02_Update)
