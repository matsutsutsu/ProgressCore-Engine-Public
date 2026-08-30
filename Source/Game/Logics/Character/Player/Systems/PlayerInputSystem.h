#pragma once
/// @file PlayerInputSystem.h
/// @brief プレイヤーの入力デバイスの状態を読み取り、移動・アクションの「意思データ」として
///        コンポーネントに書き込むシステム。
/// @note ハードウェア依存（InputFacade）はすべてこのシステムに集約させる。
/// @note 「押した瞬間」の解釈もここに集約する。ロジックは同じ描画フレーム内で複数回
///       走ることがあり、各システムが個別にエッジを取ると挙動が揃わないため。
///       詳しい理由は PlayerInputComponent.h を参照。
/// @note 「操作を受け付けない状態」の判断もここに集約する。UI操作中・カットシーン中・
///       被弾硬直中で理由は違うが、結果は同じ「全ての意思を中立にする」なので、
///       行動側に条件を配ると同じ判定が散らばる。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"


class PlayerInputSystem : public CCL::ECS::IfSystem<PlayerInputSystem,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Write<PlayerInputComponent>>
{
public:
    PlayerInputSystem();
    virtual ~PlayerInputSystem() = default;

    void Update(float dt) override;
};
