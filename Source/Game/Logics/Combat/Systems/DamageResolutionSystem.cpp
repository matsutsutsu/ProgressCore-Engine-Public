#include "Game/Logics/Combat/Systems/DamageResolutionSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Game/Core/GameEvents.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Time/TimeService.h"
#include "Engine/Physics/Components/Character/KnockbackComponent.h"
#include "Game/Logics/Combat/Feedback/ScreenImpactComponent.h"
#include "Engine/Camera/CameraEvents.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Components/EntityLabel.h"

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    // のけぞりのアニメーションのパラメータ。再生は Hit トリガー、向きは HitDirX / HitDirY
    // 向きは攻撃が来た方向（前 = 正面から当たった、後 = 背後から当たった）
    const uint32_t HASH_HIT       = CCL::Utils::HashString("Hit");
    const uint32_t HASH_HIT_DIR_X = CCL::Utils::HashString("HitDirX");
    const uint32_t HASH_HIT_DIR_Y = CCL::Utils::HashString("HitDirY");

    // 向きの番号（0 = 前 / 1 = 後 / 2 = 左 / 3 = 右）ごとの、ブレンドスペースの点の位置
    //   プレイヤーとボスの Hit_Reaction は、この4点に前後左右ののけぞりを置いたブレンドスペース
    const DirectX::XMFLOAT2 kHitDirPos[4] = {{0.0f, 1.0f}, {0.0f, -1.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f}};

    /**
     * @brief 攻撃が来た方向から前後左右ののけぞりを選んで再生する
     *
     * 方向は攻撃の基準の位置から求め、なければノックバックの向きを逆にして使う
     *
     * @param world 対象を引く World。所有しない
     * @param ev    受けたダメージのイベント。AnimParametersComponent を持たない相手（ドローンなど）には何もしない
     */
    void FireHitReaction(World *world, const DamageEventComponent &ev)
    {
        auto *anim = world->GetComponent<AnimParametersComponent>(ev.targetID);
        if (!anim) return;

        auto *vtrans = world->GetComponent<TransformComponent>(ev.targetID);
        if (!vtrans) return;

        // 受けた側の水平の前方と右方を rotation から求める（左手系: right = up × forward）
        Vector3 fwd = Vector3::Transform(Vector3::UnitZ, vtrans->rotation);
        fwd.y       = 0.0f;
        if (fwd.LengthSquared() < 1e-6f)
            fwd = Vector3::UnitZ;
        else
            fwd.Normalize();
        const Vector3 right(fwd.z, 0.0f, -fwd.x);

        // 攻撃が来た方向（受けた側から攻撃の基準の位置へ）を求める
        // attackerID（誰の攻撃か）ではなく hitOriginID（実際に当たった物）を使う。ドローンの攻撃は
        // ボスの攻撃として扱うので、attackerID を使うと背後から当たっても正面から当たったのけぞりになる
        // 位置は親を持つ相手でもずれないよう、ワールド座標で比べる
        const CCL::ECS::EntityID originID =
            (ev.hitOriginID != CCL::ECS::InvalidEntityID) ? ev.hitOriginID : ev.attackerID;

        Vector3 fromAttacker = Vector3::Zero;
        if (auto *atk = world->GetComponent<TransformComponent>(originID))
            fromAttacker = Vector3(atk->GetWorldPosition()) - Vector3(vtrans->GetWorldPosition());
        else if (ev.knockbackDir.LengthSquared() > 1e-6f)
            fromAttacker = -ev.knockbackDir;
        fromAttacker.y = 0.0f;

        // 方向を向きの番号に変える。方向が分からないとき（環境からのダメージなど）は前（0）にする
        int dirIndex = 0;
        if (fromAttacker.LengthSquared() > 1e-6f)
        {
            fromAttacker.Normalize();
            const float f = fromAttacker.Dot(fwd);   // 正なら前
            const float r = fromAttacker.Dot(right); // 正なら右
            if (std::abs(f) >= std::abs(r))
                dirIndex = (f >= 0.0f) ? 0 : 1;
            else
                dirIndex = (r >= 0.0f) ? 3 : 2;
        }

        // 向きを先に書いてからトリガーを立てる
        //   ブレンドスペースは入った瞬間の向きをそのまま使うので、後から書くと最初の1ステップだけ別の向きが混ざる
        anim->SetFloat(HASH_HIT_DIR_X, kHitDirPos[dirIndex].x);
        anim->SetFloat(HASH_HIT_DIR_Y, kHitDirPos[dirIndex].y);
        anim->SetTrigger(HASH_HIT, 0.15f);
    }
}

void DamageResolutionSystem::Update(float dt)
{
    ForEachWithID([this](EntityID eventEntity, DamageEventComponent &event) {
        if (auto *health = m_world->GetComponent<HealthComponent>(event.targetID))
        {
            // 無敵中はダメージもノックバックもなし（イベントは消す）
            if (health->invincibilityTimer <= 0.0f)
            {
                // --- とどめか ---

                // 攻撃した側の今のクリップのとどめ区間で見る
                // アニメを持たない攻撃（ドローンなど）はとどめにならない
                const auto *attackerAnim = m_world->GetComponent<AnimatorComponent>(event.attackerID);
                const bool  isFinisher   = attackerAnim && attackerAnim->currentSequence &&
                                        IsInEventWindow(*attackerAnim->currentSequence, AnimEventType::Finisher,
                                                        attackerAnim->currentTimer);

                // --- HP を減らす ---

                // とどめ以外は finisherFloor より下げない（ボスの形態の線）
                // もともと下にいるときは、引き上げずに今のまま
                float nextHealth = health->currentHealth - event.finalDamage;
                if (!isFinisher && nextHealth < health->finisherFloor)
                    nextHealth = (std::min)(health->currentHealth, health->finisherFloor);

                // デバッグで HP を減らさない間は、HP だけ据え置く（とどめでも線を越えない）
                // 下の演出・被弾の通知は普段どおり出すので、コンボや怒りの連発はそのまま試せる
                if (health->debugNoDamage) nextHealth = health->currentHealth;

                // とどめが線を越えたか。線の上から下へ落としたこの一撃だけを数える
                // 0 は下限なしなので、越える線がない
                const bool hasFloor   = health->finisherFloor > 0.0f;
                const bool brokeFloor = isFinisher && hasFloor && health->currentHealth >= health->finisherFloor &&
                                        nextHealth < health->finisherFloor;

                health->currentHealth      = nextHealth;
                health->invincibilityTimer = health->invincibilityDuration;

                CCL_LOG_INFO(LogCategory::Game,
                             "%s took %f damage. Remaining HP: %f",
                             CCL::EntityLabel(*m_world, event.targetID).c_str(), event.finalDamage,
                             health->currentHealth);

                // --- ヒットストップ（受けた側と攻撃した側の両方） ---
                // TimeScaleSystem（L01）が次のフレームから hitStopTimer を減らし、その間 localDt を 0 にする
                if (event.hitStopDuration > 0.0f)
                {
                    Time::HitStopEntity(*m_world, event.targetID, event.hitStopDuration, event.hitStopScale);
                    Time::HitStopEntity(*m_world, event.attackerID, event.hitStopDuration, event.hitStopScale);
                }

                // --- ノックバック ---
                // 受けた側に KnockbackComponent があれば、向き × 強さの速度を書く（なければ何もしない）
                // コンポーネントを付け外しせず、最初から付けておいた物に書くだけにするのは、
                // 付け外しでアーキタイプの移動が起き、同時に多数が被弾したときに重くなるため
                // 実際に動かして減らすのは JoltCharacterUpdateSystem（CharacterVirtual なので壁を抜けない）
                if (event.knockbackForce > 0.0f)
                {
                    if (auto *kb = m_world->GetComponent<KnockbackComponent>(event.targetID))
                    {
                        kb->velocity = event.knockbackDir * event.knockbackForce;
                        kb->timer    = kb->duration;

                        // 操作を受け付けない硬直。滑る時間（timer）とは別に持つ
                        // 続けて当たったら長い方を残す（後から来た弱い攻撃で硬直が短くならないように）
                        if (kb->stunDuration > kb->stunTimer)
                            kb->stunTimer = kb->stunDuration;
                    }
                }

                // --- 画面の演出 ---
                // ScreenImpactComponent があれば、ダメージ量に応じた強さで演出を始める
                // （ScreenImpactSystem が減らしながらポストプロセスに反映する）
                if (auto *impact = m_world->GetSingletonComponent<ScreenImpactComponent>())
                {
                    constexpr float kRefDamage = 25.0f; // power が 1.0 になるダメージ
                    const float     power      = std::clamp(event.finalDamage / kRefDamage, 0.3f, 1.5f);
                    impact->Trigger(power);

                    // 画面の揺れはカメラへ要求する。1フレームに何回当たっても受け取る側が最大の値だけを使うので、
                    // たくさんのドローンに同時に当たっても揺れが大きくなりすぎない
                    m_world->GetEventBus().Publish(
                        CameraShakeRequestEvent{std::min(impact->shakeTrauma * power, 1.0f)});
                }

                // --- 被弾の通知 ---
                // 実際に当たったときだけ出す（無敵ですり抜けたときは出さない）
                // PlayerFeedbackSystem（プレイヤーの被弾演出）と BossPerceptionSystem が受ける
                const bool isPlayer = m_world->HasComponent<PlayerComponent>(event.targetID);
                m_world->GetEventBus().Publish(DamageTakenEvent{event.targetID, event.finalDamage, isPlayer});

                // --- 命中の通知（誰の攻撃が当たったか） ---
                m_world->GetEventBus().Publish(
                    AttackHitConfirmedEvent{
                        event.attackerID,
                        event.targetID,
                        event.finalDamage,
                        event.hitStopDuration,
                        event.hitStopScale,
                        isFinisher});

                // --- とどめの通知 ---
                // 形態の移行か撃破かは、受けた側が次のフレームに決める
                if (brokeFloor)
                {
                    m_world->GetEventBus().Publish(FinishBlowEvent{
                        event.attackerID,
                        event.targetID,
                        DirectX::XMFLOAT3{event.hitPosition.x, event.hitPosition.y, event.hitPosition.z}});
                }

                // --- のけぞり（前後左右） ---
                // 倒れなかったときだけのけぞらせる。倒れたときは倒れるアニメーションに任せる（両方を再生しない）
                if (health->currentHealth > 0.0f)
                    FireHitReaction(m_world, event);

                // --- 当たった位置に火花を出す（HitEffectSystem が受けて GPU パーティクルを出す） ---
                // どのパーティクルにするかは HitEffectSystem が決めるので、攻撃した側を渡すだけにする
                m_world->GetEventBus().Publish(HitImpactEvent{
                    DirectX::XMFLOAT3{event.hitPosition.x, event.hitPosition.y, event.hitPosition.z},
                    event.finalDamage,
                    event.attackerID});
            }
        }

        // 無敵で何もしなかったときも、イベントのエンティティは必ず消す
        m_world->Destroy(eventEntity);
    });
}



REGISTER_LOGIC_SYSTEM(DamageResolutionSystem, Priority::LogicStage::L06_DamageApply);
