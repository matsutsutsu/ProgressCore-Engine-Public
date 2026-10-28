#pragma once
// ===========================================================================
// 概要: JoltCharacterSetupSystem が生成した CharacterVirtual を持つ Component と、
//       キャラを動かす側が共通で使う補助関数。
//
// 責務:
//   - CharacterVirtual の参照カウント付きポインタを持つ
//   - 物理の位置の取得・向きの書き込み・積分の dt を、全キャラで同じ形にする
//
// 備考: 手で付けない。JoltCharacterSetupSystem が生成時に付ける。
//       JPH::Ref なので、Component が破棄されると CharacterVirtual も解放される。
// ===========================================================================
#include <algorithm>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <SimpleMath.h>

#include "Engine/Transform/Components/TransformComponent.h"

struct JoltCharacterHandleComponent
{
    JPH::Ref<JPH::CharacterVirtual> character;
};

// CharacterVirtual の積分に使う dt の範囲 [秒]
inline constexpr float kCharacterMinStepDt = 0.0001f;
inline constexpr float kCharacterMaxStepDt = 0.0333f;

// dt を、物理（JoltCharacterUpdateSystem）が積分に使う範囲へ丸める
// 「居るべき位置へ届く速度」を出す処理も、物理と同じこの値で割る
inline float CharacterStepDt(float dt)
{
    return (std::max)(kCharacterMinStepDt, (std::min)(dt, kCharacterMaxStepDt));
}

// キャラクターの今の位置（ワールド座標）を返す。物理はここから次を積分する
// テレポート直後や親を持つキャラでは Transform と食い違うので、
// 位置で動かす計算はこちらを使う
inline DirectX::SimpleMath::Vector3 GetCharacterWorldPosition(const JoltCharacterHandleComponent &handle)
{
    const JPH::RVec3 p = handle.character->GetPosition();
    return {static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())};
}

// キャラクターの向きを、描画と物理で分けて書く
// 旋回時の傾き（roll）のように、見た目だけに付けたい回転があるときに使う
// 物理には physicsRotation（ふつうは yaw だけ）を渡し、カプセルを傾けない
inline void SetCharacterRotation(TransformComponent &trans, const JoltCharacterHandleComponent &handle,
                                 const DirectX::SimpleMath::Quaternion &visualRotation,
                                 const DirectX::SimpleMath::Quaternion &physicsRotation)
{
    trans.rotation = visualRotation;
    trans.isDirty  = true;

    if (handle.character)
        handle.character->SetRotation(JPH::Quat(physicsRotation.x, physicsRotation.y, physicsRotation.z, physicsRotation.w));
}

// キャラクターの向きを、Transform（描画）と CharacterVirtual（物理）の両方に書く
// 片方だけ書くと描画と物理の向きがずれるので、向きを変えるときはこれを使う
inline void SetCharacterRotation(TransformComponent &trans, const JoltCharacterHandleComponent &handle,
                                 const DirectX::SimpleMath::Quaternion &rotation)
{
    SetCharacterRotation(trans, handle, rotation, rotation);
}
