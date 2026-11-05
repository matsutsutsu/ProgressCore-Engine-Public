#pragma once
// ===========================================================================
// 概要: カメラの追従・注視の対象を、Component の種類で探したエンティティへ繋ぎ直す System
//
// 責務:
//   - PlayerComponent を持つ最初のエンティティをプレイヤーとして探す
//   - 追従・注視・TPS カメラの target が無効ならプレイヤーへ繋ぐ
//   - 有効な target（手で設定したものなど）は上書きしない
//
// 備考: どれがプレイヤーかはゲーム側の知識なので、Engine のカメラではなくここで探す
//       ID を直接指定しないので、保存・読み込みやリスポーンで ID が変わっても繋がる
//       FixedUpdate が走らないフレームでも遅れないよう、Render の R00b（カメラ本体の前）で走る
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Serialization/Core/EntityLink.h"

class CameraTargetBindSystem : public CCL::ECS::IfSystem<
    CameraTargetBindSystem,
    CCL::ECS::Read<VirtualCamera>>
{
public:
    CameraTargetBindSystem() : IfSystem("CameraTargetBindSystem") {}
    virtual ~CameraTargetBindSystem() = default;

    void Update(float dt) override;

private:
    // PlayerComponent を持つ最初のエンティティ（プレイヤー）を探す
    CCL::ECS::EntityID FindPlayer();

    // target が無効なら entity へ繋ぎ直す。手で設定した有効な相手は上書きしない
    // entity が InvalidEntityID なら何もしない
    void BindIfInvalid(CCL::Serialization::EntityLink &target, CCL::ECS::EntityID entity);
};
