// ===========================================================================
// @file   RF_BulletTravelSystem.cpp
// @brief  飛行中の全弾を毎フレーム更新する。Travel フェーズ効果の適用 → 移動の
//         積分 → 寿命の減算、を行う。弾処理の主戦場で、並列化される。
//
// @note   構造変更を一切行わない純粋な更新のみ。だから ForEachParallel が使える。
//         （生成＝発射/継承、破棄＝衝突/寿命切れ、は別システムが担当）
// @warning レジストリは読み取り専用参照（並列読み）。本システムが走る L02 では
//          コンパイル（書き換え）が起きないことが前提（コンパイルは L01 で完了）。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"        // @note 実パスに調整。
#include "Engine/ECS/System/CCL_SystemRegistry.h"   // @note 実パスに調整。
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"

#include "RF_BulletComponents.h"
#include "RF_WandProgram.h"
#include "RF_AttachmentEffects.h"

using namespace CCL::ECS;
using namespace CCL::ECS::Core;
using namespace RuneForge;


/**
 * @brief 全弾の飛行更新（効果適用＋積分＋寿命減算）を並列実行するシステム。
 */
class BulletTravelSystem
    : public IfSystem<BulletTravelSystem,
                      Write<BulletState>,
                      Write<TransformComponent>,
                      Read<BulletProgramRef>,
                      Read<TimeStateComponent>>
{
public:
    BulletTravelSystem() : IfSystem("BulletTravelSystem") {}

    void Update(float /*dt*/) override
    {
        if (!_world->HasResource<WandProgramRegistry>()) return;
        // ループ外で1回だけ取得し、参照を並列クロージャへ渡す（読み取り専用）。
        WandProgramRegistry& reg = _world->GetResource<WandProgramRegistry>();

        ForEachParallel([&reg](BulletState&             st,
                               TransformComponent&      xform,
                               const BulletProgramRef&  ref,
                               const TimeStateComponent& time)
        {
            if (ref.programId != InvalidProgramID)
            {
                const CompiledWandProgram& prog = reg.Get(ref.programId);

                // 1. Travel 効果（加減速・揺らぎ等）で状態を更新
                ApplyTravelPhase(prog, st, time.localDt);
            }

            // 2. 積分: 方向×基準速度(velocity) に倍率(speedMul) を掛けて前進
            xform.position = xform.position + st.velocity * (st.speedMul * time.localDt);

            // 3. 寿命減算（0以下で BulletLifetimeSystem が継承判定する）
            st.lifeRemaining -= time.localDt;
        });
    }
};

REGISTER_LOGIC_SYSTEM(BulletTravelSystem, Priority::LogicStage::L02_Update);
