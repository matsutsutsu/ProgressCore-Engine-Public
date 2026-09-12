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

} // namespace JoltShapeFactory
