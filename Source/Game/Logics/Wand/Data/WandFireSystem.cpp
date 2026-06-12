// ===========================================================================
// WandFireSystem.cpp
// ------------------
// @file
// @brief WandFireSystem の実体。Spawnフェーズ命令の解釈と弾生成を行う。
// ===========================================================================
#include "WandFireSystem.h"
#include "Game/Logics/Wand/Registry/WandProgramRegistry.h"
#include "Game/Logics/Wand/Data/CompiledWandProgram.h"
#include "BulletComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/Components/RigidBody/JoltRigidBodyComponent.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"    
#include "Engine/Platform/Core/Logger.h"

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

// 翻訳単位内に閉じる（外部リンケージを汚さない）
// 別のcppファイルとの競合を起こさないように囲む
// VSの仕様として実際にコンパイルするときに全て一つのコードファイルにまとめるため
// まとめないと別のcppファイル同士であっても名前競合が起きてエラーが出る
namespace 
{
    /// @brief Spawnフェーズ解釈の作業用。弾1発の初期パラメータを組み立てる。
    struct SpawnParams
    {
        float   speed = 0.0f;
        Vector3 direction = { 0.0f, 0.0f, 1.0f };
    };

    /**
     * @brief Spawnフェーズの命令列を解釈し、初期パラメータを確定する。
     * @note 現状は Accelerate のみ実装。他概念はこの switch に case を足すだけで拡張できる。
     */
    void ApplySpawnOps(const CompiledWandProgram& prog, SpawnParams& sp)
    {
        for (uint8_t i = 0; i < prog.spawnCount; ++i)
        {
            const EffectOp& op = prog.spawnOps[i];
            switch (op.type)
            {
            case EffectType::Accelerate:
                // params[0] = 初速倍率。重ねがけ数だけ累乗（重ねるほど速くなる）。
                for (uint16_t s = 0; s < op.stackCount; ++s) sp.speed *= op.params[0];
                break;

                // TODO(後工程): Split(弾数+拡散) / Focus(拡散0・射程延長) / Slow(初速0.5・重弾)
                //               / Reflection(逆方向にも発射) / Explosion(前方小爆発) ...
            default:
                break;
            }
        }
    }

    /**
     * @brief 弾1発の生成を予約する（構造変更は遅延適用される）。
     * @note Transform への位置書き込みはフィールド名が環境依存。実 TransformComponent に合わせること。
     */
    void SpawnOneBullet(Core::World* world, const Archetype& arch, ProgramID pid,
        const CompiledWandProgram& prog, const Vector3& muzzle,
        const SpawnParams& sp, EntityID ownerId)
    {
        BulletStateComponent state;
        state.direction = sp.direction;
        state.speed = sp.speed;
        state.age = 0.0f;
        state.lifetime = prog.baseLifetime;
        state.inheritanceDepth = 0;
        state.owner = ownerId;  // ★撃ち主
        state.prevPosition = muzzle;   // ★初回の線分長を0にし、誤ヒットを防ぐ
        state.lastHitTarget = InvalidEntityID;

        world->Spawn(arch)
            .Set(BulletProgramRef{ pid })
            .Set(state)
            .Patch<TransformComponent>([&](TransformComponent& t)
                {
                    t.position = muzzle; // ※実際の TransformComponent のフィールド名に合わせる
                    t.isDirty = true;   // ※dirty 契約がある場合のみ（無ければ削除）
                });
        // BulletTag は archetype 側で付与済み（値は不要）。
    }

} // anonymous namespace

void WandFireSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<WandProgramRegistry>())
    {
        //CCL_LOG_ERROR(LogCategory::Game, "WandProgramRegistry 未登録。発射をスキップします。");
        return;
    }
    WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();

    // 全弾が共有する単一 Archetype。毎フレーム作らずキャッシュ（初回のみ構築）。
    // ★ RuntimeOnlyTag が存在するなら、ここに足してシリアライザに弾を無視させると良い。
    static const Archetype bulletArch = ArchetypeHelper::Generate<
        TransformComponent, JoltSphereColliderComponent, JoltRigidbodyComponent,
        BulletProgramRef, BulletStateComponent, BulletTag > ();

    // Spawn は共有 PendingOps に積むため並列化しない（直列 ForEach）。
    ForEachWithID([&](EntityID id,
        WandRuntimeComponent& rt,
        const WandBuildComponent& build,
        const WandFireIntentComponent& intent,
        const TimeStateComponent& time)
        {
            // クールダウン消化。ヒットストップ等に追従するため localDt を使用（rawDt 厳禁）。
            if (rt.cooldownTimer > 0.0f) rt.cooldownTimer -= time.localDt;

            if (!intent.wantFire)                          return; // 発射意図なし
            if (rt.cooldownTimer > 0.0f)                   return; // クールダウン中
            if (build.cachedProgramId == InvalidProgramID) return; // 未コンパイル
            if (!registry.IsValid(build.cachedProgramId))  return; // ID不正（保険）

            // 杖のビルド番号を元に杖のアタッチメント情報を取得
            const CompiledWandProgram& prog = registry.Get(build.cachedProgramId);

            // Spawnフェーズ解釈：初速・方向を確定（現状は Accelerate のみ反映）。
            SpawnParams sp;
            sp.speed = prog.baseSpeed;
            sp.direction = intent.aimDirection;
            sp.direction.Normalize();
            ApplySpawnOps(prog, sp);

            // 弾生成（現状1発。Split による複数化・拡散は後工程）。
            SpawnOneBullet(_world, bulletArch, build.cachedProgramId, prog,
                intent.muzzlePosition, sp, id);

            // 次弾までのクールダウンを再設定。
            rt.cooldownTimer = prog.cooldown;

            CCL_LOG_INFO(LogCategory::Game,
                "弾発射 (ProgramID=%u, speed=%.1f)", build.cachedProgramId, sp.speed);
        });
}

// ファイル末尾で自動登録（コンパイル L01 の後の L02 で実行）
REGISTER_LOGIC_SYSTEM(WandFireSystem, Priority::LogicStage::L02_Update);