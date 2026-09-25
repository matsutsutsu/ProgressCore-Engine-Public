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

struct JoltCharacterHandleComponent
{
    JPH::Ref<JPH::CharacterVirtual> character;
};
