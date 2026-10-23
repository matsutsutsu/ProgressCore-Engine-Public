#include "DroneFormationSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Attack.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h"
#include "Game/Logics/AI/Drone/Formation/Data/DataDrivenFormation.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Squad.h"

#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Time/Components/TimeState.h"

#include <algorithm>

#include <imgui.h>

using namespace CCL::ECS;
using namespace CCL::AI::Formation;

namespace
{
    // 予告の時間（shiverTime / telegraphTime）の上限 [秒]。際限なく育たないための蓋
    constexpr float kTelegraphTimeCap = 10.0f;
} // namespace

// Droneのアクションを登録する
void DroneFormationSystem::Initialize()
{
    // 評価するこの System が作るので、どのシーンでも同じ陣形が揃う
    if (!m_world->HasResource<FormationRegistry>()) m_world->AddResource<FormationRegistry>();
    auto &registry = m_world->GetResource<FormationRegistry>();

    CCL::AI::Formation::Basic::RegisterAll(registry);
    CCL::AI::Formation::Attack::RegisterAll(registry); // データで書けない攻撃陣形（弾幕）
    CCL::AI::Formation::Squad::RegisterAll(registry);  // 小隊陣形

    // データ陣形は最後。C++ 実装の枠を JSON が名乗ったとき、埋まっている枠として弾けるように
    CCL::AI::Formation::Data::RegisterAll(registry);

    CCL_LOG_INFO(LogCategory::AI, "[DroneFormationSystem] 陣形クラス登録完了");
}

FormationContext DroneFormationSystem::MakeContext(float dt)
{
    // 処理フロー:
    // 1. ボスの位置と「今撃て」信号を読む
    // 2. 陣形の中心の速度を測る
    // 3. プレイヤーの情報を入れる
    // 4. 狙いの向きを固定・解除する

    FormationContext ctx;
    ctx.time = m_elapsed;
    ctx.dt   = dt;

    // --- ボス ---

    bool attackActive = false;
    bool hasBoss      = false;

    // ボスは BossCommandComponent を持つ最初の Entity（1対1の戦闘が前提）
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

            // 跳躍中は、陣形の中心を踏み切った地点の高さに置く。
            // 浮いたボスをそのまま中心にすると陣形ごと持ち上がり、地面を薙ぐ技がプレイヤーの頭上を素通りする。
            // 踏み切り前の溜め（Windup）と突進は揃えない（ドローンも一緒に持ち上がる見た目にする）
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

    // 前フレームの中心との差から出す。地面の高さに揃えた中心と、ボスの実際の位置の2つを別々に測る。

    // 一定の速さを超えたら瞬間移動（配置・リスポーン）とみなして含めない。
    // 含めると、ドローンが1フレームだけ数百 m/s で飛んでいく
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

    // --- 狙いの固定 ---

    // 撃つ合図の立ち上がりで1回だけ固定し、合図が下りたら解除する。
    // 予備動作の間はプレイヤーを向き続けて避ける方向を読ませ、撃った後は追わない（避けても当たるのを防ぐ）。

    // 立ち上がりは DroneCommandSystem も検出しているが、その結果を使うと実行順で1フレームずれるので、ここで検出する
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
    // 2. 陣形へ渡す情報を2通り作り、各陣形にフレームの始まりを伝える
    // 3. 機体ごとに持ち場を計算し、予告の時間を進める

    // --- 時間 ---

    // ドローンと同じ時計（敵グループ）で進める。rawDt だとジャスト回避のスロー中も陣形だけ等速になる。
    // ボスの localDt は使わない。ボスだけヒットストップしたときに陣形まで止まるため
    float dt = rawDt;
    if (m_world->HasResource<TimeContext>())
    {
        const auto &time = m_world->GetResource<TimeContext>();
        dt *= time.globalScale * time.groupScales[static_cast<size_t>(TimeGroup::Enemy)];
    }

    m_elapsed += dt;

    auto *registryPtr = FormationRegistry::Find(*m_world);
    if (!registryPtr) return; // Initialize で作るので通常は必ずある
    auto &registry = *registryPtr;

    // --- 陣形へ渡す情報 ---

    // 違うのはボスの位置と速度だけ。陣形ごとにどちらかを渡す
    //   groundCtx … 跳躍中も中心を地面の高さに残す（既定）
    //   bodyCtx   … ボスの実際の高さを中心にする（FollowsBossHeight）
    const FormationContext groundCtx = MakeContext(dt);
    FormationContext       bodyCtx   = groundCtx;
    bodyCtx.bossPos                  = m_bossBodyPos;
    bodyCtx.bossVelocity             = m_bossBodyVelocity;

    registry.BeginFrame(*m_world);

    // --- 機体ごとの計算 ---

    // 陣形の仮想関数は機体ごとに呼ぶ。全機がほぼ同じ陣形なので分岐予測が当たり、コストは小さい
    ForEach([&](DroneComponent &drone) {
        // 陣形が切り替わったら予告の時計を 0 から数え直す。前の陣形で待った時間を次の技へ持ち越さないため。
        // データ陣形が Calculate の中でこの時計を読むので、Calculate より前に行う
        if (drone.telegraphFormation != drone.currentFormation)
        {
            drone.telegraphFormation = drone.currentFormation;
            drone.shiverTime         = 0.0f;
            drone.telegraphTime      = 0.0f;
        }

        IFormation *formation = registry.Get(drone.currentFormation);
        if (!formation)
        {
            // 未登録の陣形（None 等）では持ち場も springScale も書かない（陣形の外で動かす機は、動かす側が書く）。
            // 予告の溜めだけは戻す。抱えたままだと、次に陣形が出た瞬間から全開で震える
            drone.shiverTime        = 0.0f;
            drone.telegraphTime     = 0.0f;
            drone.followFeedForward = DirectX::SimpleMath::Vector3::Zero;
            return;
        }

        const FormationContext &ctx = formation->FollowsBossHeight() ? bodyCtx : groundCtx;

        // 先渡しの速度と springScale は Calculate の前に既定を入れる。小隊の陣形が Calculate の中で上書きする
        drone.followFeedForward = formation->FollowsBoss() ? ctx.bossVelocity * m_bossCarry : DirectX::SimpleMath::Vector3::Zero;
        drone.springScale       = 1.0f;

        formation->Calculate(drone, ctx);

        // 予告の溜めは「震えを使う技で、持ち場に着いていて、まだ撃つ合図が来ていない」間だけ進める。
        // hasReachedSlot は帰投中だけ false に落ちるので、帰る途中は震えない
        const bool telegraphing =
            formation->UsesShiverTelegraph() && drone.hasReachedSlot && !drone.wantsAttack;

        drone.shiverTime = telegraphing
                               ? (std::min)(kTelegraphTimeCap, drone.shiverTime + dt)
                               : 0.0f;

        // 予告の時計は、震えを使うかどうかに関係なく進める（データ陣形の震えの帯が読む）
        drone.telegraphTime = (drone.hasReachedSlot && !drone.wantsAttack)
                                  ? (std::min)(kTelegraphTimeCap, drone.telegraphTime + dt)
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
