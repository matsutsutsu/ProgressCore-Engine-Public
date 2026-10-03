#pragma once
// ===========================================================================
// 概要: ゲーム側が物理エンジンを操作するためのインターフェース（EntityID で剛体を指定する）。
//
// 責務:
//   - 剛体への力・衝撃・速度の設定と取得
//   - 位置・回転の強制設定
//   - レイキャストと接地判定
//
// 備考: 実装は JoltPhysicsFacade。World のリソース std::shared_ptr<IPhysicsAPI> として
//       BaseScene が登録する。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include <DirectXMath.h>

// レイキャストの結果
struct PhysicsHitInfo
{
    bool               hit      = false;
    CCL::ECS::EntityID entity   = CCL::ECS::InvalidEntityID; // 当たった Entity（RayCastStatic では設定しない）
    DirectX::XMFLOAT3  position = {0.0f, 0.0f, 0.0f};        // 当たった点のワールド座標
    DirectX::XMFLOAT3  normal   = {0.0f, 1.0f, 0.0f};        // 当たった面の法線（RayCastStatic では設定しない）
    float              distance = 0.0f;                      // 始点からの距離（m）
};

class IPhysicsAPI
{
public:
    virtual ~IPhysicsAPI() = default;

    // ============================================================
    // 並進の力と速度
    // ============================================================
    // 継続的な力を加える（推進力、風など）
    virtual void AddForce(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &force) = 0;
    // 瞬間的な衝撃を加える（ジャンプ、爆発など）
    virtual void AddImpulse(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &impulse) = 0;

    virtual void              SetLinearVelocity(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &velocity) = 0;
    virtual DirectX::XMFLOAT3 GetLinearVelocity(CCL::ECS::EntityID entity)                                    = 0;

    // ============================================================
    // 回転の力と速度
    // ============================================================
    // 継続的な回転力を加える
    virtual void AddTorque(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &torque) = 0;
    // 瞬間的な回転の衝撃を加える
    virtual void AddAngularImpulse(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &impulse) = 0;

    virtual void              SetAngularVelocity(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &velocity) = 0;
    virtual DirectX::XMFLOAT3 GetAngularVelocity(CCL::ECS::EntityID entity)                                    = 0;

    // ============================================================
    // 位置・回転の強制設定
    // ============================================================
    // 物理計算を通さず指定座標へ移動させる（リスポーン時など）
    virtual void SetPosition(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &position) = 0;
    virtual void SetRotation(CCL::ECS::EntityID entity, const DirectX::XMFLOAT4 &rotation) = 0;

    // 水平方向（Y 軸回転）だけで指定方向を向かせる
    virtual void SetLookDirection(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &direction) = 0;

    // ============================================================
    // 空間クエリ
    // ============================================================
    /**
     * @brief 剛体（JoltHandleComponent）から真下へレイを飛ばし、地形（NON_MOVING）に当たるかを返す。
     * @param entity   レイを飛ばす剛体のエンティティ。
     * @param distance レイの長さ（m）。
     * @return 地形に当たれば true。
     * @note CharacterVirtual を持つキャラの接地判定には使わず、IsCharacterGrounded() を使う。
     */
    virtual bool CheckGrounded(CCL::ECS::EntityID entity, float distance) = 0;

    // 任意の始点・方向へレイを飛ばす。センサーと ignoreEntity は無視する（弾や視界判定向け）
    virtual PhysicsHitInfo RayCast(const DirectX::XMFLOAT3 &origin, const DirectX::XMFLOAT3 &direction, float maxDistance, CCL::ECS::EntityID ignoreEntity = CCL::ECS::InvalidEntityID) = 0;

    /**
     * @brief 静止物（NON_MOVING: 壁/建物/地面）だけに当てるレイキャスト。
     *
     * プレイヤー/ボス/ドローン等の動的ボディには反応しない。
     * 用途: TPSカメラの衝突回避（地形とだけ交差させ、間に何も無ければ素通しにしたい）など。
     *
     * @param origin      レイの始点（ワールド）。
     * @param direction   レイの向き。
     * @param maxDistance レイの長さ（m）。
     * @return 当たった結果。
     */
    virtual PhysicsHitInfo RayCastStatic(const DirectX::XMFLOAT3 &origin, const DirectX::XMFLOAT3 &direction, float maxDistance) = 0;

    // ============================================================
    // キャラクター専用クエリ（CharacterVirtual）
    // ============================================================
    /**
     * @brief CharacterVirtual 自身が持つ接地状態を返す。
     * @details 剛体レイキャスト(CheckGrounded)より正確。ジャンプ可否の判定や
     *          着地アニメ遷移には必ずこちらを使う。対象が CharacterVirtual を
     *          持たない場合は false を返す。
     */
    virtual bool IsCharacterGrounded(CCL::ECS::EntityID entity) = 0;
};