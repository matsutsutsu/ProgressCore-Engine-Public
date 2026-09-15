#pragma once
// ===========================================================================
// CombatComponents.h
// ------------------
// 役割:   戦闘の当たり判定(Hitbox/Hurtbox)とダメージイベントのデータ定義。
// 責務:   攻撃側/被弾側の素パラメータと、命中という出来事(Event)のデータ表現。
// 位置付け:
//   HitboxCollisionSystem(L05) が判定し DamageEvent を生成、
//   DamageResolutionSystem(L06) が消費して HP減算・ノックバック適用。
// 設計方針: 全て POD。多段ヒット防止は固定長配列(ヒープ確保回避)。
//           1対1アクションボス戦のためクリティカルの概念は持たない。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include "SimpleMath.h"

// --- 1. 攻撃側の判定データ ---
// 攻撃判定。剣・弾など「ダメージを与える側」が持つ。
struct HitboxComponent
{
    CCL::ECS::EntityID ownerID      = CCL::ECS::InvalidEntityID; // ダメージ帰属
    float              damageAmount = 0.0f;                      // 基礎ダメージ量
    bool               isActive     = false;                     // 判定が生きているか

    float knockback = 0.0f; // ノックバックの強さ(0で無効)

    // 多段ヒット防止用の固定長配列
    static constexpr uint8_t MAX_HIT_TARGETS             = 8;
    CCL::ECS::EntityID       hitTargets[MAX_HIT_TARGETS] = {}; // ヒット済み対象のID
    uint8_t                  hitCount                    = 0;  // 現在ヒットした数

    // ノックバック方向・のけぞり方向を決めるときの幾何の原点。
    //   false(既定) = ownerID。剣は持ち主の子なので、持ち主の前方を主軸にすると
    //                 「振った向きへ飛ぶ」が成立する。
    //   true        = この判定自身。ドローンのように攻撃体が持ち主から遠く離れる
    //                 場合、ownerID を原点にすると背後から突っ込まれても
    //                 「ボスの正面から食らった」向きに吹き飛ぶ。
    //   ダメージの帰属(ownerID)は変えない。分けたいのは幾何だけ。
    bool useSelfAsHitOrigin = false;

    // --- CombatAnimationSyncSystem による Hitbox 駆動用 ---
    // ownerEntity = Animator + CombatRoster を持つルート(剣→キャラの逆引き)。
    // tagHash     = roster.entries[i].tag のハッシュ("RightHand"等)。
    CCL::ECS::EntityID ownerEntity = CCL::ECS::InvalidEntityID;
    uint32_t           tagHash     = 0;

    // ownerEntity の逆引きを試したか。
    //   「解決できなかった」も結果なので覚えておく。覚えないと、親を持たない判定
    //   （空間生成の弾・ドローン）は毎フレーム親を辿り直すことになる。
    bool ownerResolved = false;

    // --- ヒットストップ(命中時に時間制御側へ伝える受け皿。配線は別途) ---
    float hitStopDuration = 0.0f; // 命中時のヒットストップ時間(秒)
    float hitStopScale    = 1.0f; // ヒットストップ中のタイムスケール
};

// --- 2. 被弾側の判定データ ---
// 被弾判定。敵・プレイヤーなど「ダメージを受ける側」が持つ。
struct HurtboxComponent
{
    float damageMultiplier = 1.0f; // 弱点なら2.0など
};

// --- 3. ダメージイベント・エンティティ用コンポーネント ---
// 1回の命中という「出来事」。判定Systemが生成し、解決Systemが消費して破棄する。
struct DamageEventComponent
{
    CCL::ECS::EntityID targetID    = CCL::ECS::InvalidEntityID; // 誰がダメージを受けるか
    CCL::ECS::EntityID attackerID  = CCL::ECS::InvalidEntityID; // 誰が攻撃したか（帰属）
    float              finalDamage = 0.0f;                      // 計算済み最終ダメージ

    // 「どこから来た攻撃か」の幾何の原点。剣なら持ち主、ドローンならその機体。
    //   attackerID（帰属先）と別に持つのは、ドローンの攻撃をボスの手柄にしたまま
    //   のけぞり方向だけは実際に当てた機体から取りたいため。
    CCL::ECS::EntityID hitOriginID = CCL::ECS::InvalidEntityID;

    DirectX::SimpleMath::Vector3 knockbackDir   = DirectX::SimpleMath::Vector3::Zero;
    float                        knockbackForce = 0.0f;

    // --- ヒットストップ(判定時に確定、解決System が双方の時計へ適用) ---
    float hitStopDuration = 0.0f; // ヒットストップ時間(秒)
    float hitStopScale    = 0.0f; // 停止中のタイムスケール(0で完全停止)

    // --- 命中エフェクト用の接触点(判定時に確定) ---
    // 攻撃判定と被弾者の中点あたり。解決System が確定ヒット時に HitImpactEvent へ載せる。
    DirectX::SimpleMath::Vector3 hitPosition = DirectX::SimpleMath::Vector3::Zero;
};

// --- 4. 生成型ヒットボックスの寿命(ランタイム専用) ---
// アニメ由来で生成された球状 Hitbox の自動消滅タイマー。
//
// SpawnHitboxSystem が生成時に付与し、毎フレーム remainingLife を減らして
// 0 以下で自身を Destroy する。剣など「既存の子コライダを ON/OFF する
// Hitbox」には付与しない(そちらは寿命を持たず、アニメ区間で駆動される)。
//
// 転がり続ける弾ではなく一瞬の空間判定用。保存対象外・非表示の内部タグ。
struct HitboxLifetimeComponent
{
    float remainingLife = 0.0f; // 残り生存時間(秒)。0以下で Destroy 対象。
};
