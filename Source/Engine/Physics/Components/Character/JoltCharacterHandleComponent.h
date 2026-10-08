#pragma once
// ===========================================================================
// 概要: JoltCharacterSetupSystem が生成した CharacterVirtual を保持するコンポーネント。
//
// 責務:
//   - CharacterVirtual の参照カウント付きポインタを持つ
//
// 備考: 手で付けない。JoltCharacterSetupSystem が生成時に付ける。
//       JPH::Ref なので、コンポーネントが破棄されると CharacterVirtual も解放される。
// ===========================================================================
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <SimpleMath.h>

struct JoltCharacterHandleComponent
{
    JPH::Ref<JPH::CharacterVirtual> character;
};

/**
 * @brief キャラクターの今の位置（ワールド座標）。物理が正とする位置で、次の物理更新はここから積分する。
 *
 * 「今あるべき位置へ届く速度」を計算する処理は、Transform ではなくこちらを使う。
 * 速度を積分する起点と同じ値でないと、式がずれるため。Transform は普段なら同じ値だが、
 *   - テレポート直後（Transform は書き換わったが、Jolt へ反映されるのは次の物理更新）
 *   - 親を持つキャラ（TransformComponent::position はローカル座標の欄）
 * では食い違う。
 */
inline DirectX::SimpleMath::Vector3 GetCharacterWorldPosition(const JoltCharacterHandleComponent &handle)
{
    const JPH::RVec3 p = handle.character->GetPosition();
    return {static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())};
}
