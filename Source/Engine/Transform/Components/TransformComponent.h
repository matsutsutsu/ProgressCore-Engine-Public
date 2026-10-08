#pragma once
// ===========================================================================
// 概要: Entity の位置・回転・スケールと親子関係を持つコンポーネント TransformComponent。
//
// 責務:
//   - ローカルの TRS（保存・編集する値）と、計算済みのワールド行列（キャッシュ）の保持
//   - 描画補間用の前ステップの値（prevPosition / prevRotation）と描画用ワールド行列の保持
//   - 親子関係（親の参照と、兄弟を連結リストでたどるための ID）
//
// 備考: 物理との同期ルール
//   - Jolt の剛体をワープさせたいときは、必ず SetPosition() を使う。
//     内部で isTeleported が立ち、JoltPushSystem が物理側の位置を上書きする。
//   - 物理で動く物体（弾、キャラクター）の移動に SetPosition を毎フレーム使ってはいけない。
//     毎ステップ物理側の位置を上書きしてしまい、物理の動き（速度・衝突応答）と競合する。
// ===========================================================================
#include <DirectXMath.h>
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Serialization/Core/EntityLink.h"

// ローカル（保存対象）とワールド行列（計算キャッシュ）を分けて持つ
struct TransformComponent
{
    // ============================================================
    // ローカル値（保存・編集する値）
    // ============================================================

    DirectX::XMFLOAT3 position = {0, 0, 0};    // 親からの相対位置
    DirectX::XMFLOAT4 rotation = {0, 0, 0, 1}; // 親からの相対回転（クォータニオン）
    DirectX::XMFLOAT3 scale    = {1, 1, 1};    // 親からの相対スケール

    // ============================================================
    // 描画補間用（Fix Your Timestep パターン）
    // ============================================================

    /**
     * @brief 直前の FixedUpdate 確定時点でのローカル位置。
     * @note TransformSnapshotSystem が毎 FixedUpdate の先頭で position をここへ退避する。
     *       描画側の補間専用データなので、ゲームロジックから参照してはならない。
     */
    DirectX::XMFLOAT3 prevPosition = {0, 0, 0};

    // 直前の FixedUpdate 確定時点でのローカル回転。役割は prevPosition と同じ
    DirectX::XMFLOAT4 prevRotation = {0, 0, 0, 1};

    // ============================================================
    // 状態フラグ
    // ============================================================

    bool isStatic = false; // true なら動かない物として行列の再計算と補間を省く

    bool isTeleported = false; // ワープした印。JoltPushSystem が物理側の位置を上書きする

    /**
     * @brief 行列の再計算を強制するフラグ。
     * @note 初期値を true にしているので、ロード直後や生成直後は isStatic = true でも
     *       必ず1回は worldMatrix が計算される。
     * @warning 実行時の一時データなので、シリアライズの対象に含めないこと。
     */
    bool isDirty = true;

    // ============================================================
    // 計算済みの値（System が書く。手で書き換えない）
    // ============================================================

    // ロジック用のワールド行列。TransformUpdateSystem が計算する
    DirectX::XMFLOAT4X4 worldMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    /**
     * @brief 描画専用の補間済みワールド行列。
     * @note TransformInterpolationSystem（Render ステージ R00）が毎描画フレーム、
     *       prevPosition/position を alpha で補間して書き込む。
     *       ゲームロジックは参照しないこと（worldMatrix を使う）。
     */
    DirectX::XMFLOAT4X4 renderWorldMatrix;

    // ============================================================
    // 親子関係
    // ============================================================
    //
    // 親だけは EntityLink で持つ。シーン保存時に、シリアライザが EntityID を
    // ファイル内のローカル番号へ自動で変換できるようにするため。
    //
    // firstChildID / prevSiblingID / nextSiblingID は、TransformUpdateSystem の
    // SetParent / DetachChild が張り替える実行時の値。親から復元できるので保存しない
    // （ComponentMeta の Fields() に含めていない）。

    CCL::Serialization::EntityLink parent; // 親 Entity（保存対象）。0 ならルート

    CCL::ECS::EntityID firstChildID  = 0; // 最初の子（子の連結リストの先頭）
    CCL::ECS::EntityID prevSiblingID = 0; // 前の兄弟
    CCL::ECS::EntityID nextSiblingID = 0; // 次の兄弟

    // ============================================================
    // 操作・取得ヘルパー
    // ============================================================

    // 位置をワープさせる。物理側へ反映させる印（isTeleported）も一緒に立てる
    void SetPosition(const DirectX::XMFLOAT3 &newPos)
    {
        position     = newPos;
        isTeleported = true;
        isStatic     = false; // 静的扱いを解除し、行列を再計算させる
    }

    void SetPosition(float x, float y, float z)
    {
        position     = {x, y, z};
        isTeleported = true;
        isStatic     = false;
    }

    // ワールド座標を取得する（ロジック・物理判定・音の発生源などに使う）
    DirectX::XMFLOAT3 GetWorldPosition() const
    {
        return DirectX::XMFLOAT3(worldMatrix._41, worldMatrix._42, worldMatrix._43);
    }

    /**
     * @brief 描画補間済みのワールド座標を返す。カメラ追従など「見た目に追従させたい」用途専用。
     * @return 描画補間済みのワールド座標。ロジックの判定には GetWorldPosition()（worldMatrix 基準）を使うこと。
     */
    DirectX::XMFLOAT3 GetRenderWorldPosition() const
    {
        return {renderWorldMatrix._41, renderWorldMatrix._42, renderWorldMatrix._43};
    }

    // ワールド回転（クォータニオン）を取得する。行列を分解するので毎回少し重い
    DirectX::XMFLOAT4 GetWorldRotation() const
    {
        DirectX::XMVECTOR scale, rotQuat, trans;
        DirectX::XMMatrixDecompose(&scale, &rotQuat, &trans, DirectX::XMLoadFloat4x4(&worldMatrix));

        DirectX::XMFLOAT4 outRot;
        DirectX::XMStoreFloat4(&outRot, rotQuat);
        return outRot;
    }

    // ワールド空間での前方ベクトル（+Z）を取得する（弾の発射方向などに使う）
    DirectX::XMFLOAT3 GetForward() const
    {
        // 行列の3行目が Z 軸の向き
        using namespace DirectX;
        XMVECTOR f = XMVectorSet(worldMatrix._31, worldMatrix._32, worldMatrix._33, 0.0f);
        f          = XMVector3Normalize(f); // スケールが入っている場合に備えて正規化
        XMFLOAT3 out;
        XMStoreFloat3(&out, f);
        return out;
    }

    // ワールド空間での右ベクトル（+X）
    DirectX::XMFLOAT3 GetRight() const
    {
        using namespace DirectX;
        XMVECTOR r = XMVectorSet(worldMatrix._11, worldMatrix._12, worldMatrix._13, 0.0f);
        r          = XMVector3Normalize(r);
        XMFLOAT3 out;
        XMStoreFloat3(&out, r);
        return out;
    }

    // ワールド空間での上ベクトル（+Y）
    DirectX::XMFLOAT3 GetUp() const
    {
        using namespace DirectX;
        XMVECTOR u = XMVectorSet(worldMatrix._21, worldMatrix._22, worldMatrix._23, 0.0f);
        u          = XMVector3Normalize(u);
        XMFLOAT3 out;
        XMStoreFloat3(&out, u);
        return out;
    }

    /**
     * @brief worldMatrix をその場で計算する。
     * @param parentMatrix 親のワールド行列。渡せば親子を反映する。
     * @note 通常は TransformUpdateSystem が計算するので、System の外で今すぐ行列が欲しいときだけ使う。
     */
    void UpdateMatrix(const DirectX::XMMATRIX &parentMatrix = DirectX::XMMatrixIdentity())
    {
        using namespace DirectX;

        XMMATRIX S           = XMMatrixScaling(scale.x, scale.y, scale.z);
        XMMATRIX R           = XMMatrixRotationQuaternion(XMLoadFloat4(&rotation));
        XMMATRIX T           = XMMatrixTranslation(position.x, position.y, position.z);
        XMMATRIX localMatrix = S * R * T;

        // DirectXMath は行ベクトルなので、ワールド = ローカル × 親 の順に掛ける
        XMMATRIX world = localMatrix * parentMatrix;

        XMStoreFloat4x4(&worldMatrix, world);
    }

    /**
     * @brief TRS からローカル行列（S * R * T）を合成する。
     * @note 親変換は含まない純粋なローカル。階層解決・ボーン追従で共用。
     */
    DirectX::XMMATRIX ComposeLocalMatrix() const
    {
        using namespace DirectX;
        return XMMatrixAffineTransformation(
            XMLoadFloat3(&scale), XMVectorZero(),
            XMLoadFloat4(&rotation), XMLoadFloat3(&position));
    }
};