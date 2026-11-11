#pragma once
// ===========================================================================
// 概要: JPH::ShapeSettings から形状を作るときに必ず通す関数
//
// 責務:
//   - 形状の生成結果を確かめ、失敗したらログを出して nullptr を返す
//   - 基本形状を、ローカルの位置・回転でずらした形状を作る
//   - 度のオイラー角を、Jolt のクォータニオンに直す
//
// 備考: 失敗した JPH::Result に Get() を呼ぶと、構築されていない ShapeRefC をコピーし、野良ポインタに AddRef する
//       JPH_ASSERT は Release / Development で消えるので、警告なしにメモリを壊す
//       形状の生成は退化した三角形や半径0のカプセルなどで普通に失敗するので、Create().Get() を直接書かない
//
// @code
//   JPH::ShapeRefC shape = JoltShapeFactory::CreateChecked(settings, entityID, "Box");
//   if (shape == nullptr) return; // 失敗はログに出ている
// @endcode
// ===========================================================================

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <DirectXMath.h>
#include <cstdint>

namespace JoltShapeFactory
{

    // 形状を作り、結果を確かめてから取り出す。失敗したら ownerId と what をログに出して nullptr
    // JoltIntegration は ECS を知らないので、ID は uint64_t で受け取る
    JPH::ShapeRefC CreateChecked(
        const JPH::ShapeSettings &settings, uint64_t ownerId, const char *what);

    // 度の XYZ オイラー角を、Jolt のクォータニオンに直す（インスペクタはコライダーの回転を度で持つ）
    JPH::Quat EulerDegreeToJoltQuat(const DirectX::XMFLOAT3 &eulerDegree);

    // 基本形状（Box / Sphere / Capsule）を作り、ローカルの位置（m）と回転（度）でずらした形状で包む。失敗したら nullptr
    // 剛体を作るとき（JoltBodySetupSystem）と形を変えるとき（JoltShapeUpdateSystem）で共通に使う
    JPH::ShapeRefC CreateOffsetShape(
        const JPH::ShapeSettings &base, const DirectX::XMFLOAT3 &localOffset,
        const DirectX::XMFLOAT3 &localEuler, uint64_t ownerId, const char *what);

} // namespace JoltShapeFactory
