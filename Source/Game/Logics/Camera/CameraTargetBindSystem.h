#pragma once
// ===========================================================================
// 概要: カメラの追従・注視の対象を、Component の種類で探したエンティティへ繋ぎ直す System。
//
// 責務:
//   - PlayerComponent を持つ最初のエンティティをプレイヤーとして探す
//   - 追従・注視・TPS カメラの target が無効ならプレイヤーへ繋ぐ
//   - 有効な target（手で設定したものなど）は上書きしない
//
// 備考:
//   なぜゲーム側にあるか:
//   どのエンティティがプレイヤーかはゲーム側の知識で、Engine のカメラが PlayerComponent を
//   知ると Engine→Game の依存になる。そこでゲーム側で探し、Engine のカメラ Component の target へ書き込む。
//
//   EntityID を直接指定せずに探すので、プレイヤーとカメラを別々に配置しても、
//   保存・読み込みやリスポーンで ID が変わっても、カメラの target が自動で繋がる。
//
//   実行ステージは R00b_CameraTargetBind(75)。カメラ本体(R01_Camera=100)より前に置く。
//   Logic ステージに置くと、FixedUpdate が走らないフレームで繋ぎ直しが遅れるため、
//   Render ステージのカメラの直前に置いている。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Serialization/Core/EntityLink.h"

class CameraTargetBindSystem
    : public CCL::ECS::IfSystem<CameraTargetBindSystem,
                                CCL::ECS::Read<VirtualCamera>>
{
public:
    CameraTargetBindSystem() : IfSystem("CameraTargetBindSystem") {}
    virtual ~CameraTargetBindSystem() = default;

    void Update(float dt) override;

private:
    // PlayerComponent を持つ最初のエンティティ（プレイヤー）を探す。
    CCL::ECS::EntityID FindPlayer();

    /**
     * @brief target が無効なら entity へ繋ぎ直す。
     * @param target 繋ぎ直す参照。未設定・再読み込みで ID が変わった・リスポーンなどで無効なときだけ書き換える。
     *               有効な相手を指している場合（手で設定したものなど）は上書きしない。
     * @param entity 繋ぐ先。InvalidEntityID なら何もしない。
     */
    void BindIfInvalid(CCL::Serialization::EntityLink &target, CCL::ECS::EntityID entity);
};
