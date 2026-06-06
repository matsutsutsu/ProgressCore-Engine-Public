// ===========================================================================
// @file   RF_BulletHitSystem.cpp
// @brief  弾の衝突イベント(BulletHitEvent)を購読し、OnHit フェーズ効果を適用する。
//         貫通が残っていれば貫通回数を減らし、尽きていれば弾を破棄する。
//
// @note   重要な仕様: 「衝突による消滅は継承を発動しない」。継承は寿命の時間切れ
//         （BulletLifetimeSystem）でのみ起きる。ここで破棄する弾は子を生まない。
// @note   衝突検出そのものは別系統（L05/L06 の衝突解決）が担い、本システムは
//         その結果イベントを受けて効果だけ適用する（検出と効果の疎結合）。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"        // @note 実パスに調整。
#include "Engine/ECS/System/CCL_SystemRegistry.h"   // @note 実パスに調整。
#include "Engine/Platform/Core/Logger.h"

#include "RF_BulletComponents.h"
#include "RF_BulletEvents.h"
#include "RF_WandProgram.h"
#include "RF_AttachmentEffects.h"

using namespace CCL::ECS;
using namespace CCL::ECS::Core;
using namespace RuneForge;


/**
 * @brief BulletHitEvent を購読し OnHit 効果適用と弾の貫通/破棄を行うシステム。
 *
 * @note 処理はチャンク走査ではなくイベント駆動（特定の弾IDへのランダムアクセス）。
 *       Update は空。Write<BulletState> を宣言してスケジューラに「BulletState を
 *       書く」ことを伝え、同フレームの他 BulletState 書き込みと直列化させる。
 * @warning このシステムは衝突解決ステージ(L06)で動かし、イベント発行元
 *          （衝突解決）と同じステージに置くこと。
 */
class BulletHitSystem
    : public IfSystem<BulletHitSystem,
                      Write<BulletState>,
                      Read<BulletProgramRef>>
{
public:
    BulletHitSystem() : IfSystem("BulletHitSystem") {}

    /// @brief EventBus 購読を登録（解除は自動）。
    void Initialize() override
    {
        ListenEvent<BulletHitEvent>([this](const BulletHitEvent& ev)
        {
            OnBulletHit(ev);
        });
    }

    /// @brief 本体はイベント駆動のため Update は空。
    void Update(float /*dt*/) override {}

private:
    /**
     * @brief 1件の衝突を処理する。OnHit 効果適用 → 貫通減算 or 破棄。
     * @note  コールバック内の破棄は PendingOps 経由で遅延されるため安全。
     */
    void OnBulletHit(const BulletHitEvent& ev)
    {
        BulletState*      st  = _world->GetComponent<BulletState>(ev.bullet);
        BulletProgramRef* ref = _world->GetComponent<BulletProgramRef>(ev.bullet);
        if (!st || !ref) return; // 既に破棄済み等

        if (ref->programId != InvalidProgramID &&
            _world->HasResource<WandProgramRegistry>())
        {
            const WandProgramRegistry& reg = _world->GetResource<WandProgramRegistry>();
            ApplyHitPhase(reg.Get(ref->programId), *st, ev);
        }

        if (st->pierceLeft > 0)
        {
            // 貫通: 1回消費して飛び続ける（破棄しない＝継承も起きない）
            --st->pierceLeft;
        }
        else
        {
            // 貫通切れ → 衝突消滅。leaf なので hard destroy。
            // ★ここでは子を生まない（継承は寿命切れ専用）。
            _world->RequestDestroyEntity(ev.bullet);
        }
    }
};

REGISTER_LOGIC_SYSTEM(BulletHitSystem, Priority::LogicStage::L06_Resolution);
