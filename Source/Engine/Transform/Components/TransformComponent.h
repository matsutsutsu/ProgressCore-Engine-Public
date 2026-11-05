#pragma once
// ===========================================================================
// 概要: Entity の位置・回転・スケールと親子関係を持つコンポーネント TransformComponent
//
// 責務:
//   - ローカルの TRS（保存・編集する値）と、計算済みのワールド行列（キャッシュ）の保持
//   - 描画補間用の前ステップの値（prevPosition / prevRotation）と描画用ワールド行列の保持
//   - 親子関係（親の参照と、兄弟を連結リストでたどるための ID）
//
// 備考: Jolt の剛体をワープさせるときは SetPosition を使う。isTeleported が立ち、JoltPushSystem が物理側の位置を上書きする
//       物理で動く物（弾・キャラクター）を SetPosition で毎フレーム動かさない。物理の速度や衝突の応答と競合する
// ===========================================================================
#include <DirectXMath.h>
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Serialization/Core/EntityLink.h"

// ローカル（保存対象）とワールド行列（計算キャッシュ）を分けて持つ
struct TransformComponent
{
    // --- ローカル値（保存・編集する値） ---

    DirectX::XMFLOAT3 position = {0, 0, 0};    // 親からの相対位置
    DirectX::XMFLOAT4 rotation = {0, 0, 0, 1}; // 親からの相対回転（クォータニオン）
    DirectX::XMFLOAT3 scale    = {1, 1, 1};    // 親からの相対スケール

    // --- 描画補間用（TransformSnapshotSystem が書き、TransformInterpolationSystem だけが読む。ロジックから参照しない） ---

    DirectX::XMFLOAT3 prevPosition = {0, 0, 0};    // 1つ前の固定ステップで確定したローカル位置
    DirectX::XMFLOAT4 prevRotation = {0, 0, 0, 1}; // 1つ前の固定ステップで確定したローカル回転

    // --- 状態フラグ ---

    bool isStatic     = false; // 動かない物の印。行列の再計算と補間を省く
    bool isTeleported = false; // ワープした印。JoltPushSystem が物理側の位置を上書きする
    bool isDirty      = true;  // 行列を次の更新で必ず計算させる印。実行時の値なので保存しない

    // --- 計算済みの値（System が書く。手で書き換えない） ---

    // ロジック用のワールド行列（TransformUpdateSystem が書く）
    DirectX::XMFLOAT4X4 worldMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    // 描画用の補間済みワールド行列（TransformInterpolationSystem が書く）。ロジックは worldMatrix を使う
    DirectX::XMFLOAT4X4 renderWorldMatrix;

    // --- 親子関係 ---

    // 親だけは EntityLink で持つ
    // シーン保存時に、シリアライザが EntityID をファイル内のローカル番号へ自動で変換できるようにするため
    CCL::Serialization::EntityLink parent; // 親 Entity（保存対象。0 ならルート）

    // 子と兄弟は、TransformUpdateSystem の SetParent / DetachChild が張り替える実行時の値
    // parent から復元できるので保存しない（ComponentMeta の Fields() に含めていない）
    CCL::ECS::EntityID firstChildID  = 0; // 最初の子（子の連結リストの先頭）
    CCL::ECS::EntityID prevSiblingID = 0; // 前の兄弟
    CCL::ECS::EntityID nextSiblingID = 0; // 次の兄弟

    // --- 操作・取得ヘルパー ---

    // 位置をワープさせ、物理へ反映させる印（isTeleported）も立てる。newPos はローカルの位置
    // prevPosition は変えないので、そのステップの描画では旧位置との間が補間される。途中を見せたくなければ prevPosition もそろえる
    void SetPosition(const DirectX::XMFLOAT3 &newPos)
    {
        position     = newPos;
        isTeleported = true;
        isStatic     = false; // 静的扱いを解除し、行列を再計算させる
    }

    // SetPosition の成分指定版
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

    // 描画の補間を済ませたワールド座標（カメラの追従など見た目用）。ロジックの判定には GetWorldPosition を使う
    DirectX::XMFLOAT3 GetRenderWorldPosition() const
    {
        return {renderWorldMatrix._41, renderWorldMatrix._42, renderWorldMatrix._43};
    }

    // 描画補間済みのワールド回転（クォータニオン）。GetRenderWorldPosition と対。行列を分解するので毎回少し重い
    DirectX::XMFLOAT4 GetRenderWorldRotation() const
    {
        DirectX::XMVECTOR scale, rotQuat, trans;
        DirectX::XMMatrixDecompose(&scale, &rotQuat, &trans, DirectX::XMLoadFloat4x4(&renderWorldMatrix));

        DirectX::XMFLOAT4 outRot;
        DirectX::XMStoreFloat4(&outRot, rotQuat);
        return outRot;
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

    // worldMatrix をその場で求める。ふだんは TransformUpdateSystem が求めるので、System の外ですぐ欲しいときだけ使う
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

    // TRS からローカル行列（S * R * T）を作る。親の変換は含まない
    DirectX::XMMATRIX ComposeLocalMatrix() const
    {
        using namespace DirectX;
        return XMMatrixAffineTransformation(
            XMLoadFloat3(&scale), XMVectorZero(),
            XMLoadFloat4(&rotation), XMLoadFloat3(&position));
    }
};