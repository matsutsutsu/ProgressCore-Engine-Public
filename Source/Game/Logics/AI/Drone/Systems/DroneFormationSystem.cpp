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

    // データ陣形は最後に登録する。
    // 登録側は Has() で埋まっている枠を弾くので、C++ 実装の枠を JSON が名乗ったことを
    // 検出できるのは、C++ 実装が先に登録されているときだけ。
    CCL::AI::Formation::Data::RegisterAll();

    CCL_LOG_INFO(LogCategory::AI, "[DroneFormationSystem] 陣形クラス登録完了");
}

FormationContext DroneFormationSystem::MakeContext(float dt)
{
    // 処理フロー:
    // 1. ボスの位置と「今撃て」信号を読む
    // 2. 陣形の中心の速度を測る
    // 3. プレイヤーの情報を入れる
    // 4. 小隊の状態を渡す
    // 5. 狙いの向きを固定・解除する

    FormationContext ctx;
    ctx.time = m_elapsed;
    ctx.dt   = dt; // 攻撃の進行度（ヒンジ回転など）を進めるのに使う

    // --- ボス ---

    // 「今撃て」信号は、狙いの向きを固定する瞬間（下の「狙いの固定」）を決めるのに使う。
    bool attackActive = false;
    bool hasBoss      = false;

    // ボスは BossCommandComponent を持つ最初の Entity とする（1対1の戦闘が前提）。
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

            // 跳躍中は、陣形の中心の高さを踏み切った地点の高さに置く。
            // 各機はボス相対で並ぶので、浮いたボスをそのまま中心にすると陣形ごと持ち上がり、
            // 地面を薙ぐ技がプレイヤーの頭上を素通りするため。
            // 浮いている間の地面の高さは分からないので、踏み切った地点の高さで代用する。

            // 踏み切り前の溜め（Windup）は揃えない。まだ地面にいるため。

            // 突進で浮く間も揃えない。ドローンもボスと一緒に持ち上がるのが狙いの見た目のため。

            // 頭上円陣のようにボスに付いて回るだけの隊形には、揃える前の m_bossBodyPos を渡す
            // （FollowsBossHeight。Update で bodyCtx に入れる）。
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

    // --- 速度 ---

    // 陣形の中心が動いている速度は、前フレームの中心との差から出す。
    // 差から出すので、上の「跳躍中は地面の高さに揃える」もそのまま反映される
    // （中心を持ち上げないなら、縦の速度も乗せない）。

    // 実際の位置（頭上円陣などが使う）の速度も、同じ方法で別に測る。

    // 一定の速さを超えたら瞬間移動（エディタでの配置・リスポーン）とみなして速度に含めない。
    // 含めると、ドローンが1フレームだけ数百 m/s で飛んでいく。
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

    // --- プレイヤー ---

    // プレイヤーが居ないフレームは playerPos を配らない。
    // 既定値（原点）のまま渡せば、陣形が原点を狙うことはない。
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

    // --- 小隊 ---

    // 小隊のアンカーは SquadUpdateSystem が今フレーム更新済み。ここでは読むだけ。
    if (m_world->HasResource<SquadRegistry>())
        ctx.squads = &m_world->GetResource<SquadRegistry>();

    // --- 狙いの固定 ---

    // 狙う向きは「撃つと決めた瞬間」（信号の立ち上がり）に1回だけ固定し、信号が下りたら解除する。

    // 予備動作の間は固定しない。
    // 形がプレイヤーの方を向き続けるので、どちらへ避けるかを読める。

    // 信号が立った後は動かさない。
    // 撃った後も形がプレイヤーを追うと、避けても当たる。
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

    // 処理フロー:
    // 1. 陣形の時間を進める
    // 2. 陣形へ渡す情報を2通り作る
    // 3. 機体ごとに持ち場を計算し、予告の時間を進める

    // --- 時間 ---

    // ドローンと同じ時計で進める。
    // rawDt のままだと、ジャスト回避の世界スロー中も陣形だけが等速で動き、
    // 持ち場へ張り付く技（縦割り等）はスローにならない。

    // ボスの localDt は使わない。
    // 命中でボスだけヒットストップすると、陣形まで止まってしまうため。
    float dt = rawDt;
    if (m_world->HasResource<TimeContext>())
    {
        const auto &time = m_world->GetResource<TimeContext>();
        dt *= time.globalScale * time.groupScales[static_cast<size_t>(TimeGroup::Enemy)];
    }

    m_elapsed += dt;

    auto &registry = FormationRegistry::Instance();

    // --- 陣形へ渡す情報 ---

    // 陣形へ渡す情報は2通り。違うのはボスの位置と速度だけで、陣形ごとにどちらかを渡す。
    //   groundCtx … 跳躍・滞空中も中心を地面の高さに残す（既定。地面を薙ぐ技など）
    //   bodyCtx   … ボスの実際の高さを中心にする（FollowsBossHeight。頭上円陣など）
    const FormationContext groundCtx = MakeContext(dt);
    FormationContext       bodyCtx   = groundCtx;
    bodyCtx.bossPos                  = m_bossBodyPos;
    bodyCtx.bossVelocity             = m_bossBodyVelocity;

    // --- 機体ごとの計算 ---

    // 仮想関数（陣形の取得・Calculate など）は機体ごとに呼ぶ。
    // 全機がほぼ同じ陣形なので分岐予測が当たり、呼び出しのコストは小さい。
    // ループ自体は DroneComponent の配列を先頭から順に読む。
    ForEach([&](DroneComponent &drone) {
        IFormation *formation = registry.Get(drone.currentFormation);
        if (!formation)
        {
            // 未登録の陣形（Hidden等）では持ち場を書かない。
            // ただし予告の溜めは戻す。抱えたままだと、次に陣形が出た瞬間から全開で震える。
            drone.windupTimer       = 0.0f;
            drone.telegraphTime     = 0.0f;
            drone.followFeedForward = DirectX::SimpleMath::Vector3::Zero;
            return;
        }

        const FormationContext &ctx = formation->FollowsBossHeight() ? bodyCtx : groundCtx;

        // 先渡しの速度は Calculate の前に入れる。
        // ボス以外を基準にする陣形（小隊）は、Calculate の中で自分の基準の速度に上書きするため。

        // 先渡しの速度は、陣形の中心と同じ情報（ctx）から取る。
        // 頭上円陣なら、跳躍で上下する速度も乗る。
        drone.followFeedForward = formation->FollowsBoss() ? ctx.bossVelocity * m_bossCarry : DirectX::SimpleMath::Vector3::Zero;

        formation->Calculate(drone, ctx); // targetPosition を書き込む

        // 予告（震え）の溜めは、次の3つが揃う間だけ進める。この時間帯が予備動作にあたる。
        //   - 震えを使う技である
        //   - 持ち場に着いている
        //   - まだ撃つ合図が来ていない

        // 陣形を切り替えた直後も溜めは始まる。
        // hasReachedSlot は「一度でも着いたか」のラッチで、切り替えでは false に戻らない。
        // そのため溜めは BT が陣形を指示した時点から始まり、BT の Wait(予告) の長さと一致する。

        // 突撃から帰る途中は震えない。
        // 帰る途中だけ hasReachedSlot が false に落ちるため。
        const bool telegraphing =
            formation->UsesShiverTelegraph() && drone.hasReachedSlot && !drone.wantsAttack;

        drone.windupTimer = telegraphing
                                ? (std::min)(kWindupTimerCap, drone.windupTimer + dt)
                                : 0.0f;

        // 予告の時計は、震わせる設定かどうかに関係なく、同じ時間帯（着いてから撃つまで）で進める。
        // データ陣形の Shiver の帯は、震えの有無と別にこの時計を読む。
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
