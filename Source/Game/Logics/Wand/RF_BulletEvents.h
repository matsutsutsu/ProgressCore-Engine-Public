#pragma once
// ===========================================================================
// @file   RF_BulletEvents.h
// @brief  弾システムが EventBus を介してやり取りするイベント型を定義する。
//
// @note   弾の衝突は「衝突判定システム(L05)/解決システム(L06)」が検出し、
//         BulletHitEvent として Publish する想定。OnHit フェーズの効果適用は
//         BulletHitSystem がこのイベントを購読して行う（衝突検出と効果適用の疎結合）。
//         こうすることで、衝突検出の実装（Grid/物理）を差し替えても OnHit 効果
//         ロジックに影響しない。
// ===========================================================================

#include "SimpleMath.h" // @note 実環境のパスに合わせて調整。
#include "Engine/ECS/Common/CCL_Types.h" // @note EntityID。実環境のパスに合わせて調整。

namespace RuneForge
{
    using Vector3 = DirectX::SimpleMath::Vector3;

    /**
     * @brief 弾が何か（敵・壁）に衝突した瞬間を表すイベント。
     *
     * @note 発行元は衝突解決システム。bullet が弾エンティティ、victim が衝突相手
     *       （壁の場合は InvalidEntityID でも可）。OnHit 効果（貫通加速・爆発生成・
     *       反射など）は BulletHitSystem がこの情報をもとに適用する。
     * @warning このイベントのコールバックは Publish と同期的に呼ばれる。コールバック
     *          内での構造変更（Destroy/Spawn）は PendingOps 経由で遅延されるため安全。
     */
    struct BulletHitEvent
    {
        CCL::ECS::EntityID bullet = CCL::ECS::InvalidEntityID; ///< 衝突した弾
        CCL::ECS::EntityID victim = CCL::ECS::InvalidEntityID; ///< 衝突相手（壁ならInvalid可）
        Vector3            point  = { 0, 0, 0 };               ///< 衝突点（ワールド）
        Vector3            normal = { 0, 0, 1 };               ///< 衝突面の法線（反射用）
    };

} // namespace RuneForge
