// IFormation 方式の陣形計算。registry から陣形を引いて targetPosition を計算。
#include "DroneFormationSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Attack.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Squad.h"

#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"

#include <algorithm>

using namespace CCL::ECS;
using namespace CCL::AI::Formation;

namespace
{
// 予告の溜めの上限 [秒]。振幅は DroneMovementSystem 側で早々に頭打ちになるので、
// ここは「長く構えっぱなしにしても値が際限なく育たない」ための蓋にすぎない。
constexpr float kWindupTimerCap = 10.0f;
} // namespace

void DroneFormationSystem::Initialize()
{
    // このシステムが評価する陣形クラス（IFormation）を登録する。
    // 「DroneFormationSystem が動く」ということは「陣形クラスが必要」
    // ということなので、ここに書くのが最も自然。
    //
    // 陣形を追加した時も、
    // Formations_Basic.h の RegisterAll() に1行足すだけで、
    // このシステムを持つ全シーンに自動反映される。
    CCL::AI::Formation::Basic::RegisterAll();
    CCL::AI::Formation::Attack::RegisterAll(); // 攻撃陣形（縦割り等）
    CCL::AI::Formation::Squad::RegisterAll();  // 小隊陣形（5機編隊）

    CCL_LOG_INFO(LogCategory::AI, "[DroneFormationSystem] 陣形クラス登録完了");
}

FormationContext DroneFormationSystem::MakeContext(float dt)
{
    FormationContext ctx;
    ctx.time = m_elapsed;
    ctx.dt   = dt; // 攻撃の進行度（ヒンジ回転など）を進めるのに使う

    // 「今撃て」信号。狙いを焼くタイミングの判定に使う。
    bool attackActive = false;

    // ボス位置と攻撃信号（BossCommandComponent を持つ最初のEntity）
    {
        auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
        for (auto b : bossView)
        {
            if (auto *t = m_world->GetComponent<TransformComponent>(b))
                ctx.bossPos = t->GetWorldPosition();   // position はローカル。陣形の中心はワールドで置く
            if (auto *c = m_world->GetComponent<BossCommandComponent>(b))
                attackActive = c->droneAttackActive;
            break;
        }
    }

    // プレイヤー位置（BattleContext から）
    if (m_world->HasResource<BattleContext>())
    {
        auto &battle       = m_world->GetResource<BattleContext>();
        ctx.playerPos      = battle.playerPos;
        ctx.playerCenter   = battle.playerCenter;
        ctx.playerVelocity = battle.playerVelocity;
    }

    // 小隊の頭の位置（SquadUpdateSystem が今フレーム更新済み）。
    //   ここでは読むだけ。書き手を1つに固定しておくと、並列化しても壊れない。
    if (m_world->HasResource<SquadRegistry>())
        ctx.squads = &m_world->GetResource<SquadRegistry>();

    // --- 狙う向きを「撃つと決めた瞬間」に1回だけ焼く ---
    //
    //   予備動作の間（信号が下りている間）は追い続ける。形がプレイヤーの方を
    //   向くからこそ、プレイヤーは「どちらへ避けるか」を読める。
    //   信号が立った瞬間に焼き、以降は動かさない。ここから先は避けた者勝ちにする。
    //   信号が下りたら解除して、次の技のためにまた追えるようにする。
    if (attackActive && !m_prevAttackActive)
    {
        DirectX::SimpleMath::Vector3 aim = ctx.playerPos - ctx.bossPos;
        aim.y = 0.0f;
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

void DroneFormationSystem::Update(float dt)
{
    ZoneScopedN("DroneFormationSystem::Update");

    m_elapsed += dt;

    auto &registry = FormationRegistry::Instance();

    const FormationContext ctx = MakeContext(dt);

    // --- 各ドローンの目標座標を計算 ---
    //   ポリモーフィズムは「陣形を引くとき」だけ。ループ自体は連続アクセス。
    ForEach([&](DroneComponent &drone) {
        IFormation *formation = registry.Get(drone.currentFormation);
        if (!formation)
        {
            // 未登録の陣形（Hidden等）は何もしない。
            // 溜めだけは戻しておく。収納された機が前の技の溜めを抱えたままになると、
            // 次に出てきた瞬間から全開で震える。
            drone.windupTimer = 0.0f;
            return;
        }

        formation->Calculate(drone, ctx); // targetPosition を書き込む

        // --- 予告（震え）の溜めを進める ---
        //
        //   技であり、持ち場に着いていて、まだ撃つ合図が来ていない。
        //   この3つが揃っている時間帯が予備動作そのものにあたる。
        //   撃った瞬間 wantsAttack が立って 0 に落ちるので、震えは合図と同時に止む。
        //
        //   hasReachedSlot は「一度でも持ち場へ着いたか」のラッチなので、
        //   陣形を切り替えた直後（新しい形へ寄っていく途中）も true のままになる。
        //   つまり溜めは BT が技の陣形を指示した時点から始まり、Wait(予告) の
        //   長さとそのまま一致する。狙ってこうしてある。
        //   突撃から帰る途中だけは false に落ちるので、帰りながらは震えない。
        const bool telegraphing =
            formation->UsesShiverTelegraph() && drone.hasReachedSlot && !drone.wantsAttack;

        drone.windupTimer = telegraphing
                                ? (std::min)(kWindupTimerCap, drone.windupTimer + dt)
                                : 0.0f;
    });
}

REGISTER_LOGIC_SYSTEM(DroneFormationSystem, Priority::LogicStage::L02_Update)
