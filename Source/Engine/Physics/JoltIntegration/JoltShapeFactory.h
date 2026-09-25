#pragma once
// ===========================================================================
// 概要: JPH::ShapeSettings から形状を生成するときに必ず通す関数群。
//
// 責務:
//   - 形状生成の Result を検証し、失敗したらログを出して nullptr を返す
//   - 基本形状をローカルの位置・回転でずらした形状の生成
//   - 度数のオイラー角から Jolt のクォータニオンへの変換
//
// なぜ必要か:
// JPH::Result::Get() の実体は次の通り（Jolt/Core/Result.h）:
//     const Type & Get() const { JPH_ASSERT(IsValid()); return mResult; }
// mResult は union のメンバで、エラー時は構築されていない。
// つまり失敗した Result に Get() を呼ぶと、未初期化の ShapeRefC をコピーすることになり、
// そのコピーコンストラクタが野良ポインタに AddRef() する。
//
// しかも JPH_ASSERT は JPH_ENABLE_ASSERTS が無い構成（Release / Development）では
// 完全に消えるため、この事故は何の警告も出さずにメモリを壊す。
//
// 形状生成はモデル次第で普通に失敗する（メッシュの退化三角形、三角形数の上限、
// 半径0のカプセルなど）。そのため Create().Get() を直書きせず、必ずここを通す。
//
// 使い方:
//   JPH::ShapeRefC shape = JoltShapeFactory::CreateChecked(settings, entityID, "Box");
//   if (shape == nullptr) return;   // 失敗はログに出た上で nullptr が返る
// ===========================================================================

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <DirectXMath.h>
#include <cstdint>

namespace JoltShapeFactory
{

    /**
     * @brief 形状を生成し、Result を必ず検証してから取り出す。
     *
     * @param settings 生成元の設定
     * @param ownerId 失敗ログに出す持ち主のID（EntityID をそのまま渡す）
     * @param what 失敗ログに出す形状の種類名（"Box" / "Mesh" など）
     * @return 成功なら形状、失敗なら nullptr（呼び側の nullptr チェックに合流させる）
     *
     * @note ECS に依存しないよう、ID は uint64_t で受け取る（JoltIntegration は ECS を知らない）。
     */
    JPH::ShapeRefC CreateChecked(
        const JPH::ShapeSettings &settings, uint64_t ownerId, const char *what);

    // 度数の XYZ オイラー角を Jolt のクォータニオンへ変換する。
    // コライダーのローカル回転はインスペクタで度数のまま持っているため。
    JPH::Quat EulerDegreeToJoltQuat(const DirectX::XMFLOAT3 &eulerDegree);

    /**
     * @brief 基本形状を作り、ローカルの位置・回転でずらした形状（RotatedTranslatedShape）で包む。
     *
     * Box / Sphere / Capsule の生成時（JoltBodySetupSystem）と更新時（JoltShapeUpdateSystem）で共通。
     * 2回の生成とも Result を検証する。
     *
     * @param base 基本形状の設定（Box/Sphere/Capsule）
     * @param localOffset 形状のローカル位置のずれ（m）
     * @param localEuler 形状のローカル回転（度）。球では {0,0,0} を渡す
     * @return 失敗時は nullptr（理由はログ済み）
     */
    JPH::ShapeRefC CreateOffsetShape(
        const JPH::ShapeSettings &base, const DirectX::XMFLOAT3 &localOffset,
        const DirectX::XMFLOAT3 &localEuler, uint64_t ownerId, const char *what);

} // namespace JoltShapeFactory
