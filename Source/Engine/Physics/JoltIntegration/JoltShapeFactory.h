#pragma once
// ===================================================================================
// ファイル名: JoltShapeFactory.h
// 役割: JPH::ShapeSettings から形状を生成する「唯一の窓口」。
//
// 【なぜ存在するか】
// JPH::Result::Get() の実体は次の通り（Jolt/Core/Result.h）:
//     const Type & Get() const { JPH_ASSERT(IsValid()); return mResult; }
// mResult は union のメンバで、エラー時は【構築されていない】。
// つまり失敗した Result に Get() を呼ぶと、未初期化の ShapeRefC をコピーすることになり、
// そのコピーコンストラクタが野良ポインタに AddRef() する。
//
// しかも JPH_ASSERT は JPH_ENABLE_ASSERTS が無い構成（Release / Development）では
// 完全に消えるため、この事故は何の警告も出さずにメモリを壊す。
//
// 形状生成はモデル次第で普通に失敗する（メッシュの退化三角形、三角形数の上限、
// 半径0のカプセルなど）。そのため Create().Get() を直書きせず、必ずここを通す。
//
// 【使い方】
//   JPH::ShapeRefC shape = JoltShapeFactory::CreateChecked(settings, entityID, "Box");
//   if (shape == nullptr) return;   // 失敗はログに出た上で nullptr が返る
// ===================================================================================

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <DirectXMath.h>
#include <cstdint>

namespace JoltShapeFactory {

/**
 * @brief 形状を生成し、Result を必ず検証してから取り出す。
 *
 * @param settings 生成元の設定。
 * @param ownerId  失敗ログに出す持ち主のID（EntityID をそのまま渡す）。
 * @param what     失敗ログに出す形状の種類名（"Box" / "Mesh" など）。
 * @return 成功なら形状、失敗なら nullptr（呼び側の nullptr チェックに合流させる）。
 *
 * @note ECS に依存しないよう、ID は uint64_t で受け取る（Manager 層の非ECS原則）。
 */
JPH::ShapeRefC CreateChecked(
    const JPH::ShapeSettings &settings, uint64_t ownerId, const char *what);

/**
 * @brief Degree(度)の XYZ オイラー角を Jolt のクォータニオンへ変換する。
 *
 * @note インスペクタが度数で持っているため、形状のローカル回転を作る箇所で必ず要る。
 *       JoltBodySetupSystem と JoltShapeUpdateSystem に同じ関数が二重定義されていたので
 *       ここへ寄せた（片方は匿名namespace、もう片方はグローバルの inline だった）。
 */
JPH::Quat EulerDegreeToJoltQuat(const DirectX::XMFLOAT3 &eulerDegree);

/**
 * @brief 「基本形状 → ローカルオフセット/回転で包む」までを一括で行う。
 *
 * @details
 *  Box / Sphere / Capsule のどれも
 *      基本形状を作る → RotatedTranslatedShape で包む → 2回とも Result を検証する
 *  という同じ骨格で、生成時(BodySetup)と更新時(ShapeUpdate)の計6箇所に写経されていた。
 *
 * @param base        基本形状の設定（Box/Sphere/Capsule）。
 * @param localOffset 形状のローカル位置ズレ。
 * @param localEuler  形状のローカル回転（度）。球など回転が無意味な形状では無視される値を渡してよい。
 * @return 失敗時は nullptr（理由はログ済み）。
 */
JPH::ShapeRefC CreateOffsetShape(
    const JPH::ShapeSettings &base, const DirectX::XMFLOAT3 &localOffset,
    const DirectX::XMFLOAT3 &localEuler, uint64_t ownerId, const char *what);

} // namespace JoltShapeFactory
