// ===========================================================================
// @file   RF_BulletLifetimeSystem.cpp
// @brief  寿命が時間切れになった弾を検出し、「継承(Inheritance)」を発動させる。
//         子プログラムがあれば子弾を生成し、その後で親弾を破棄する。
//
// @note   これが Inheritance フェーズの実行主体。継承は『時間切れ』限定であり、
//         衝突消滅（BulletHitSystem）では決して起きない。
//
// @warning ループ防止の二重の担保:
//          (1) 子プログラムは継承スロットを空にしてコンパイル済み
//              （WandProgramRegistry::Compile）→ 子は childProgramId=Invalid。
//          (2) 親は生成と同フレームに RequestDestroyEntity で hard-destroy。
//              次フレームの ScrutinyAndApply で実体が消えるため、寿命切れ弾が
//              翌フレームに再評価されて子を無限生成することはない
//              （soft DestroyTag の1フレーム遅延による多重発火を避ける狙い）。
//          → ForEach は1フレームに各弾を1度しか訪れないので同フレーム多重も無い。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"        // @note 実パスに調整。
#include "Engine/ECS/System/CCL_SystemRegistry.h"   // @note 実パスに調整。
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Platform/Core/Logger.h"

#include "RF_BulletComponents.h"
#include "RF_WandProgram.h"
#include "RF_AttachmentEffects.h"

using namespace CCL::ECS;
using namespace CCL::ECS::Core;
using namespace RuneForge;

namespace
{
    /// @brief 継承で生まれる子弾の基準寿命（秒）。
    /// @todo  本来は親プログラム/ゲーム設定から引く。プロト用の暫定定数。
    constexpr float kInheritedLife = 3.0f;
}


/**
 * @brief 寿命切れ弾の継承発動と破棄を行うシステム（L07_Cleanup）。
 *
 * @note 子弾生成という構造変更を伴うため ForEachWithID（非並列）を使う。
 */
class BulletLifetimeSystem
    : public IfSystem<BulletLifetimeSystem,
                      Write<BulletState>,
                      Read<TransformComponent>,
                      Read<BulletProgramRef>>
{
public:
    BulletLifetimeSystem() : IfSystem("BulletLifetimeSystem") {}

    void Update(float /*dt*/) override
    {
        if (!_world->HasResource<WandProgramRegistry>()) return;
        WandProgramRegistry& reg = _world->GetResource<WandProgramRegistry>();

        // ForEachWithID（非並列）: 生成/破棄＝構造変更を伴うため。
        ForEachWithID([&](EntityID id,
                          BulletState&             st,
                          const TransformComponent& xform,
                          const BulletProgramRef&  ref)
        {
            if (st.lifeRemaining > 0.0f) return; // まだ生存
            if (ref.programId == InvalidProgramID)
            {
                _world->RequestDestroyEntity(id);
                return;
            }

            const CompiledWandProgram& prog = reg.Get(ref.programId);

            // 継承: 子プログラムがあれば、現在位置・進行方向で子弾を生成。
            if (prog.childProgramId != InvalidProgramID)
            {
                Vector3 dir = st.velocity;
                const float speed = dir.Length();
                if (speed > 1e-5f) dir /= speed; else dir = Vector3(0, 0, 1);

                SpawnBulletsFromProgram(*_world, reg,
                                        prog.childProgramId,
                                        xform.position,
                                        dir,
                                        ref.faction,
                                        /*baseSpeed=*/ (speed > 1e-5f ? speed : 1.0f),
                                        /*baseLife =*/ kInheritedLife);

                CCL_LOG_INFO(LogCategory::Game,
                    "Bullet inherited: parent prog=%u -> child prog=%u",
                    ref.programId, prog.childProgramId);
            }

            // 親弾は必ず破棄（継承の有無に関わらず寿命切れなので）。
            _world->RequestDestroyEntity(id);
        });
    }
};

REGISTER_LOGIC_SYSTEM(BulletLifetimeSystem, Priority::LogicStage::L07_Cleanup);
