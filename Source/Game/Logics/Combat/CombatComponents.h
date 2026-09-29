#pragma once
// ===========================================================================
// 概要: 戦闘の当たり判定（攻撃側の Hitbox・受ける側の Hurtbox）と、命中1回ぶんのイベントのコンポーネント。
//
// 責務:
//   - 攻撃側と受ける側のパラメータを持つ
//   - 命中1回ぶんの結果（DamageEventComponent）を持つ
//   - SpawnHitboxSystem が作る判定の寿命を持つ
//
// 備考: HitboxCollisionSystem（L05）が命中を判定して DamageEvent を作り、
//       DamageResolutionSystem（L06）が HP を減らしてノックバックを書く。
//       すべて POD。多段ヒットの履歴はヒープを使わないよう固定長の配列にする。
//       1対1のボス戦なので、クリティカルは持たない。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include "SimpleMath.h"

// 攻撃判定。剣・ドローンなどダメージを与える側が持つ
struct HitboxComponent
{
    CCL::ECS::EntityID ownerID      = CCL::ECS::InvalidEntityID; // 誰の攻撃か
    float              damageAmount = 0.0f;                      // 基本のダメージ量
    bool               isActive     = false;                     // 判定が開いているか

    float knockback = 0.0f; // ノックバックの強さ（0 なら押さない）

    // 多段ヒットを防ぐための、当てた相手の履歴
    static constexpr uint8_t MAX_HIT_TARGETS             = 8;
    CCL::ECS::EntityID       hitTargets[MAX_HIT_TARGETS] = {}; // 当てた相手
    uint8_t                  hitCount                    = 0;  // hitTargets に入っている数

    // ノックバックとのけぞりの向きを決めるときの基準の位置。
    //   false（既定）: ownerID。剣は持ち主の子なので、持ち主の前方を基準にすると振った向きへ飛ぶ。
    //   true         : この判定自身。ドローンのように持ち主から遠く離れて当たる場合、ownerID を基準にすると
    //                  背後から当たってもボスの正面から当たった向きに飛んでしまう。
    // どちらでも ownerID（誰の攻撃か）は変えない。変えるのは向きの基準だけ
    bool useSelfAsHitOrigin = false;

    // 親をたどって見つけた、Animator を持つ持ち主（剣ならキャラ）。CombatAnimationSyncSystem が使う。
    // 持ち主の HitBox 区間はどれでもこの判定を開く（1キャラが持つ攻撃判定は1つの前提）。
    CCL::ECS::EntityID ownerEntity = CCL::ECS::InvalidEntityID;

    // ownerEntity を探したか。見つからなかったことも覚えておく。覚えないと、親を持たない判定
    // （SpawnHitboxSystem の球・ドローン）が毎フレーム親をたどり直すことになる
    bool ownerResolved = false;

    // --- ヒットストップ（DamageResolutionSystem が攻撃した側と受けた側の両方にかける） ---
    float hitStopDuration = 0.0f; // 長さ（秒）
    float hitStopScale    = 1.0f; // その間の時間の速さ
};

// 被弾判定。敵・プレイヤーなどダメージを受ける側が持つ
struct HurtboxComponent
{
    float damageMultiplier = 1.0f; // 受けるダメージの倍率（弱点なら 2.0 など）
};

// 命中1回ぶんの結果。HitboxCollisionSystem がこれを持つエンティティを作り、
// DamageResolutionSystem が処理して消す
struct DamageEventComponent
{
    CCL::ECS::EntityID targetID    = CCL::ECS::InvalidEntityID; // ダメージを受ける相手
    CCL::ECS::EntityID attackerID  = CCL::ECS::InvalidEntityID; // 誰の攻撃か
    float              finalDamage = 0.0f;                      // 倍率をかけた後のダメージ

    // 攻撃が来た位置の基準（剣なら持ち主、ドローンならその機体）。
    // attackerID と別に持つのは、ドローンの攻撃をボスの攻撃として扱いつつ、
    // のけぞりの向きは実際に当たった機体から求めたいため
    CCL::ECS::EntityID hitOriginID = CCL::ECS::InvalidEntityID;

    DirectX::SimpleMath::Vector3 knockbackDir   = DirectX::SimpleMath::Vector3::Zero;
    float                        knockbackForce = 0.0f;

    // --- ヒットストップ（判定したときに決め、DamageResolutionSystem が両方にかける） ---
    float hitStopDuration = 0.0f; // 長さ（秒）
    float hitStopScale    = 0.0f; // その間の時間の速さ（0 で止まる）

    // 当たった位置（物理の接触点。ワールド座標）。DamageResolutionSystem が HitImpactEvent に載せる
    DirectX::SimpleMath::Vector3 hitPosition = DirectX::SimpleMath::Vector3::Zero;
};

// SpawnHitboxSystem が作る球の攻撃判定の寿命。
// SpawnHitboxSystem が付け、毎フレーム remainingLife を減らして 0 以下になったら消す。
// 剣のように子のコライダを開け閉めする判定には付けない（そちらはアニメーションの区間で開け閉めする）。
// 保存せず、インスペクタにも出さない
struct HitboxLifetimeComponent
{
    float remainingLife = 0.0f; // 残りの時間（秒）
};
