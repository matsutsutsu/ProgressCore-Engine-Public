// ===========================================================================
// DamageResolutionSystem.cpp
// --------------------------
// 役割・責務・パイプライン上の位置付けは DamageResolutionSystem.h を参照。
//
// 実装の意図(Why):
//   - DamageEvent を消費して HP を減算し、イベントを即破棄する「燃やして捨てる」
//     方式を踏襲。攻撃ソース(弾/剣/罠)に依存しない一点集約の解決点とする。
//   - ノックバックは常設の KnockbackComponent に velocity/timer を書き込むだけ。
//     AddComponent/RemoveComponent による構造変更(アーキタイプ移動)を避け、
//     多数同時被弾しても再配置コストが発生しないようにする。実際の移動・減衰は
//     JoltCharacterUpdateSystem が担う（CharacterVirtual 経由で壁も考慮）。
//   - 無敵中はダメージもノックバックも無効化する(多段ハメ防止の一貫性)。
// ===========================================================================
#include "DamageResolutionSystem.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/GameEvent/GameEvents.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Time/TimeService.h" // ヒットストップは意図ファサード経由
#include "Engine/Physics/Components/Character/KnockbackComponent.h"
#include "Game/Logics/Effects/Components/ScreenImpactComponent.h"
#include "Engine/Camera/CameraEvents.h" // 命中時の画面揺れ要求（カメラは直接触らない）
#include "Engine/Transform/Components/TransformComponent.h"       // 被弾者の向き（前後左右判定）
#include "Engine/Animation/Components/AnimParametersComponent.h"  // ヒットリアクションのトリガー発行
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
    // ヒットリアクション：方向は HitDir(0=前/1=後/2=左/3=右)、再生合図は Hit トリガー1本。
    //   規約：攻撃が「来た方向」を基準（前=正面から食らった / 後=背後から / 右=右側から / 左=左側から）。
    const uint32_t HASH_HIT     = CCL::Utils::HashString("Hit");
    const uint32_t HASH_HIT_DIR = CCL::Utils::HashString("HitDir");

    // 方向別トリガ（HitDir と同じ並び：0=前 / 1=後 / 2=左 / 3=右）。
    //   プレイヤーのグラフは Hit + HitDir で HitReaction サブマシンへ入るが、
    //   ボスのグラフは方向別トリガで4状態へ直接入る古い形のまま。両方立てておく。
    //   （ボスは Hit/HitDir を持たないため、のけぞりが一度も再生されていなかった）
    const uint32_t HASH_HIT_DIRECTIONAL[4] = {
        CCL::Utils::HashString("HitFront"),
        CCL::Utils::HashString("HitBack"),
        CCL::Utils::HashString("HitLeft"),
        CCL::Utils::HashString("HitRight")};
    
    // 被弾方向から前後左右のヒットリアクションを選んで再生する。
    // アニメーターを持たない相手（ドローン等）は自動でスキップされる。
    // 方向元は「攻撃者の位置」を最優先、無ければノックバック方向の反転を使う。
    void FireHitReaction(World *world, const DamageEventComponent &ev)
    {
        // アニメーターが無ければ何もしない（1000機のドローン等はここで弾かれる）。
        auto *anim = world->GetComponent<AnimParametersComponent>(ev.targetID);
        if (!anim) return;
    
        auto *vtrans = world->GetComponent<TransformComponent>(ev.targetID);
        if (!vtrans) return;
    
        // 被弾者の水平 forward / right を rotation から求める（LH: right = up × forward）。
        Vector3 fwd = Vector3::Transform(Vector3::UnitZ, vtrans->rotation);
        fwd.y = 0.0f;
        if (fwd.LengthSquared() < 1e-6f) fwd = Vector3::UnitZ;
        else                             fwd.Normalize();
        const Vector3 right(fwd.z, 0.0f, -fwd.x);
    
        // 攻撃が「来た方向」（被弾者→攻撃者側）を求める。
        //   基準は attackerID（帰属先）ではなく hitOriginID（実際に当てた体）。
        //   ドローンの攻撃は帰属がボスなので、attackerID を使うと背後から
        //   突っ込まれても「正面から食らった」のけぞりになる。
        //   優先: 原点の位置 → 無ければ -knockbackDir（攻撃者から離れる向き）。
        //   座標はローカルではなくワールドで揃える（親を持つ相手だとずれる）。
        const CCL::ECS::EntityID originID =
            (ev.hitOriginID != CCL::ECS::InvalidEntityID) ? ev.hitOriginID : ev.attackerID;

        Vector3 fromAttacker = Vector3::Zero;
        if (auto *atk = world->GetComponent<TransformComponent>(originID))
            fromAttacker = Vector3(atk->GetWorldPosition()) - Vector3(vtrans->GetWorldPosition());
        else if (ev.knockbackDir.LengthSquared() > 1e-6f)
            fromAttacker = -ev.knockbackDir;
        fromAttacker.y = 0.0f;
    
        // 方向 → HitDir(0=前/1=後/2=左/3=右)。方向不明（環境ダメージ等）は前(0)で代替。
        float hitDir = 0.0f;
        if (fromAttacker.LengthSquared() > 1e-6f)
        {
            fromAttacker.Normalize();
            const float f = fromAttacker.Dot(fwd);   // +で前
            const float r = fromAttacker.Dot(right); // +で右
            if (std::abs(f) >= std::abs(r))
                hitDir = (f >= 0.0f) ? 0.0f : 1.0f; // 前 / 後
            else
                hitDir = (r >= 0.0f) ? 3.0f : 2.0f; // 右 / 左
        }

        // 方向を先に確定させてから再生合図（HitReaction サブマシンが HitDir で分岐する）。
        anim->SetFloat(HASH_HIT_DIR, hitDir);
        anim->SetTrigger(HASH_HIT, 0.15f);

        // 方向別トリガしか持たないグラフ（ボス）向け。持っていないパラメータへの
        // SetTrigger は無視されるので、両形式のグラフが混在していても安全。
        const int dirIndex = static_cast<int>(hitDir);
        if (dirIndex >= 0 && dirIndex < 4)
            anim->SetTrigger(HASH_HIT_DIRECTIONAL[dirIndex], 0.15f);
    }
} // namespace

void DamageResolutionSystem::Update(float dt) {

    ForEachWithID([this](EntityID eventEntity, DamageEventComponent& event) {

        if (auto* health = m_world->GetComponent<HealthComponent>(event.targetID)) {

            // 無敵中はダメージ・ノックバックともに無効(イベントは破棄する)。
            if (health->invincibilityTimer <= 0.0f) {

                // --- HP減算 ---
                health->currentHealth -= event.finalDamage;
                health->invincibilityTimer = health->invincibilityDuration;

                CCL_LOG_INFO(LogCategory::Game,
                    "%s took %f damage. Remaining HP: %f",
                    CCL::EntityLabel(*m_world, event.targetID).c_str(), event.finalDamage,
                    health->currentHealth);

                // --- ヒットストップ適用（被弾側＋攻撃側の双方を止める）---
                // TimeScaleSystem(L01) が次フレームから hitStopTimer を消費し localDt=0 にする。
                if (event.hitStopDuration > 0.0f)
                {
                    // 被弾側＋攻撃側の双方を止める（意図ファサード経由）。
                    Time::HitStopEntity(*m_world, event.targetID, event.hitStopDuration, event.hitStopScale);
                    Time::HitStopEntity(*m_world, event.attackerID, event.hitStopDuration, event.hitStopScale);
                }

                // --- ノックバック適用 ---
                // 被弾側に KnockbackComponent が「常設」で付いていれば、命中方向×強さの
                // 速度を書き込む。実際の移動と減衰は JoltCharacterUpdateSystem が担う
                // （CharacterVirtual 経由なので壁抜けしない）。無ければ何もしない。
                if (event.knockbackForce > 0.0f)
                {
                    if (auto *kb = m_world->GetComponent<KnockbackComponent>(event.targetID))
                    {
                        kb->velocity = event.knockbackDir * event.knockbackForce;
                        kb->timer    = kb->duration;

                        // 操作を受け付けない硬直。滑る時間(timer)とは別枠で持つ。
                        // 連続被弾では長い方を残す（後から来た弱い攻撃で硬直が短縮されない）。
                        if (kb->stunDuration > kb->stunTimer)
                            kb->stunTimer = kb->stunDuration;
                    }
                }

                // --- 命中演出（画面）のトリガ ---
                // シーンに ScreenImpact singleton があれば、ダメージ量に応じた強さで
                // PPバーストを起動する（VFXは直接呼ばずデータで流す方針）。
                // ScreenImpactSystem(R02) が減衰させ PostProcess へ反映する。
                if (auto *impact = m_world->GetSingletonComponent<ScreenImpactComponent>())
                {
                    constexpr float kRefDamage = 25.0f; // これで power=1.0 の基準ダメージ
                    const float power = std::clamp(event.finalDamage / kRefDamage, 0.3f, 1.5f);
                    impact->Trigger(power);

                    // 画面の揺れはカメラ側の一元経路へ回す。1フレームに何発命中しても
                    // 受け側が最大値だけを採るので、多数のドローンに同時命中しても
                    // 揺れが一気に飽和することはない。
                    m_world->GetEventBus().Publish(
                        CameraShakeRequestEvent{std::min(impact->shakeTrauma * power, 1.0f)});
                }

                // --- 被弾フィードバックの通知 ---
                // 実際に食らった時だけ DamageTakenEvent を発火する（無敵ですり抜けた場合は出さない）。
                // 購読側が各自の演出を行う：PlayerFeedbackSystem（プレイヤーのカメラシェイク＋被弾ビネット）、
                // JustEvadeSystem（被弾でジャスト回避チェイン切断）など。isPlayer は受け取り側の判定用フラグ。
                const bool isPlayer = m_world->HasComponent<PlayerComponent>(event.targetID);
                m_world->GetEventBus().Publish(DamageTakenEvent{event.targetID, event.finalDamage, isPlayer});

                // --- 攻撃命中確定の通知 ---
                // AttackHitConfirmedEvent:「誰の攻撃が命中したか」
                m_world->GetEventBus().Publish(
                    AttackHitConfirmedEvent{
                        event.attackerID,
                        event.targetID,
                        event.finalDamage,
                        event.hitStopDuration,
                        event.hitStopScale});

                // --- ヒットリアクション（前後左右）の再生 ---
                // 生き残った時だけ怯ませる。致命打は死亡アニメ側に任せる（怯みと二重再生しない）。
                // プレイヤーも敵も共通：AnimParametersComponent を持つ相手だけが反応する。
                if (health->currentHealth > 0.0f)
                    FireHitReaction(m_world, event);

                // --- 命中エフェクトの通知（確定ヒットの接触点に衝突VFXを出す） ---
                // HitEffectSystem が購読して GPUパーティクルのバーストを生成する。
                m_world->GetEventBus().Publish(HitImpactEvent{
                    DirectX::XMFLOAT3{event.hitPosition.x, event.hitPosition.y, event.hitPosition.z},
                    event.finalDamage});
            }
        }

        // 処理が終わったイベントエンティティは必ず破棄する。
        m_world->Destroy(eventEntity);
        });
}

REGISTER_LOGIC_SYSTEM(DamageResolutionSystem, Priority::LogicStage::L06_DamageApply);
