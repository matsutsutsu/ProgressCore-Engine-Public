#pragma once
// ===========================================================================
// CombatComponents.h
// ------------------
// 役割:
//   戦闘の当たり判定(Hitbox/Hurtbox)と、判定結果として一時生成される
//   ダメージイベント(DamageEventComponent)のデータ構造を定義する。
//
// 責務:
//   - 攻撃側(Hitbox)・被弾側(Hurtbox)が持つべき素のパラメータの置き場。
//   - 「誰が誰にどれだけ・どの向きに」という出来事(Event)のデータ表現。
//
// アーキテクチャ上の位置付け:
//   HitboxCollisionSystem(L05) が Hitbox×Hurtbox を検証して DamageEvent を生成し、
//   DamageResolutionSystem(L06) がそれを消費して HP減算・ノックバック適用を行う。
//   本ファイルはその3者が共有する純粋データのみを持ち、ロジックは持たない(DOD)。
//
// 設計方針:
//   - 全て POD。多段ヒット防止は固定長配列で表現し、ヒープ確保を避ける。
//   - クリティカル判定やノックバック「方向」は判定System側で確定し、結果のみを
//     DamageEvent に載せる。被弾側Systemは出来事を消費するだけで判定方法を知らない。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Common.h"
#include "SimpleMath.h"

// --- 1. 攻撃側の判定データ ---
/// @brief 攻撃判定。剣・弾など「ダメージを与える側」が持つ。
struct HitboxComponent {
    CCL::ECS::EntityID ownerID;          ///< 誰の攻撃か(弾なら撃ったプレイヤーのID)
    float damageAmount;                  ///< 基礎ダメージ量(クリティカル前)
    bool  isActive = false;              ///< 判定が生きているか(弾は常時true)

    // --- 戦闘ステータス(弾の基礎パラメータをここに焼き込む) ---
    float critRate = 0.0f;           ///< クリティカル率 [0,1]
    float critMult = 1.0f;           ///< クリティカル時のダメージ倍率
    float knockback = 0.0f;           ///< ノックバックの強さ(0で無効)

    // ★多段ヒット防止用の固定長配列
    static constexpr uint8_t MAX_HIT_TARGETS = 8;
    CCL::ECS::EntityID hitTargets[MAX_HIT_TARGETS]; ///< ヒット済み対象のIDリスト
    uint8_t hitCount = 0;                ///< 現在ヒットした数

    // =========================================================
    // ★追加: CombatAnimationSyncSystem による Hitbox 駆動用
    //
    // ownerID（ダメージ帰属）とは別物。
    // ownerEntity = Animator + CombatRoster を持つルートエンティティ。
    //   "剣のエンティティ" から "キャラクターのエンティティ" を逆引きするために使う。
    // tagHash     = roster.entries[i].tag を HashString したもの("RightHand"など)。
    //   AnimNotifyEvent.stringParam のハッシュと毎フレーム比較することで
    //   文字列比較コストをゼロにする。
    //
    // 初期化タイミング: 武器エンティティのスポーン時（Rosterへの登録と同箇所）。
    // =========================================================
    CCL::ECS::EntityID ownerEntity = CCL::ECS::InvalidEntityID; ///< Animatorを持つルートのID
    uint32_t           tagHash     = 0;
};

// --- 2. 被弾側の判定データ ---
/// @brief 被弾判定。敵・プレイヤーなど「ダメージを受ける側」が持つ。
struct HurtboxComponent {
    float damageMultiplier = 1.0f;       ///< 弱点なら2.0など
};

// --- 3. ダメージイベント・エンティティ用コンポーネント ---
/// @brief 1回の命中という「出来事」。判定Systemが生成し、解決Systemが消費して破棄する。
struct DamageEventComponent {
    CCL::ECS::EntityID targetID;         ///< 誰がダメージを受けるか
    CCL::ECS::EntityID attackerID;       ///< 誰が攻撃したか(キルログ・吸収などに使用)
    float finalDamage;                   ///< 計算済みの最終ダメージ量(クリティカル適用後)

    bool  isCritical = false;            ///< クリティカルだったか(VFX/SE分岐用)

    // --- ノックバック(判定時に方向と強さを確定し、解決Systemが適用) ---
    DirectX::SimpleMath::Vector3 knockbackDir = DirectX::SimpleMath::Vector3::Zero; ///< 正規化済み方向(XZ)
    float knockbackForce = 0.0f;         ///< ノックバックの強さ(0で無効)
};