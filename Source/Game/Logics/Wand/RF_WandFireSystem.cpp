// ===========================================================================
// @file   RF_WandFireSystem.cpp
// @brief  クールダウンを消化し、発射入力があれば現プログラムに従って弾を撃つ。
//         これが「Spawn フェーズ」の実行主体。
//
// @note   発射は構造変更（弾エンティティ生成）を伴うため、本システムは
//         シングルスレッドの ForEach を使う（ForEachParallel 不可）。
//         生成自体は PendingOps 経由で遅延適用されるため、ループ内でも安全。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"        // @note 実パスに調整。
#include "Engine/ECS/System/CCL_SystemRegistry.h"   // @note 実パスに調整。
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"
#include "Engine/Platform/Core/Logger.h"

#include "RF_WandComponents.h"
#include "RF_WandProgram.h"
#include "RF_AttachmentEffects.h"

using namespace CCL::ECS;
using namespace CCL::ECS::Core;
using namespace RuneForge;


/**
 * @brief 杖のクールダウン管理と弾の発射を行うシステム。
 *
 * @note テンプレート引数の順序＝ForEach ラムダ引数の順序。const/参照を厳密一致させること。
 */
class WandFireSystem
    : public IfSystem<WandFireSystem,
                      Write<WandComponent>,
                      Read<TransformComponent>,
                      Read<TimeStateComponent>>
{
public:
    WandFireSystem() : IfSystem("WandFireSystem") {}

    void Update(float /*dt*/) override
    {
        if (!_world->HasResource<WandProgramRegistry>()) return;
        WandProgramRegistry& reg = _world->GetResource<WandProgramRegistry>();

        // ※ ForEach（非並列）: 発射が構造変更を伴うため。
        ForEach([&](WandComponent& wand,
                    const TransformComponent& xform,
                    const TimeStateComponent& time)
        {
            // クールダウンは個別時間で消化（ヒットストップ中は発射も止まる）。
            if (wand.cooldownTimer > 0.0f)
                wand.cooldownTimer -= time.localDt;

            const bool ready = (wand.cooldownTimer <= 0.0f);
            const bool valid = (wand.activeProgramId != InvalidProgramID);

            if (!(wand.fireRequested && ready && valid)) return;

            // 発射位置 = 杖の位置 + 銃口オフセット。方向は入力系が与えた照準。
            const Vector3 origin = xform.position + wand.muzzleOffset;

            SpawnBulletsFromProgram(*_world, reg,
                                    wand.activeProgramId,
                                    origin,
                                    wand.aimDirection,
                                    /*faction=*/0,
                                    wand.baseBulletSpeed,
                                    wand.baseBulletLife);

            wand.cooldownTimer = wand.cooldownMax; // 次弾まで待機
        });
    }
};

REGISTER_LOGIC_SYSTEM(WandFireSystem, Priority::LogicStage::L02_Update);
