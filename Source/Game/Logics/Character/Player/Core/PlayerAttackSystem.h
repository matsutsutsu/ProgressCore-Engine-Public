#pragma once
// ===========================================================================
// 概要: 攻撃ボタンの入力から、アニメーションの Attack トリガーを出す System
//
// 責務:
//   - 攻撃入力を受け、Attack トリガーを先行入力付きで出す
//   - 攻撃の瞬間に、ロックオン先か前方の最も近い敵の方へ向く yaw を書く
//
// 備考: 通常/ダッシュ/空中/コンボのどの攻撃になるかはアニメグラフが決める
//       ラッシュ中は通常攻撃を出さない。攻撃入力は PlayerRushSystem が受け取り、
//       ラッシュの段（アニメグラフの RushCombo）として出す
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerInputComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include <SimpleMath.h>

class PlayerAttackSystem : public CCL::ECS::IfSystem<
    PlayerAttackSystem,
    CCL::ECS::Read<PlayerInputComponent>,
    CCL::ECS::Write<AnimParametersComponent>,
    CCL::ECS::Write<PlayerComponent>,
    CCL::ECS::Read<PlayerRushComponent>>
{
public:
    PlayerAttackSystem() : IfSystem("PlayerAttackSystem") {}
    virtual ~PlayerAttackSystem() = default;

    // PlayerInputSystem が書いた PlayerInputComponent を読むので、同じ L01_Input の中でその後に走る
    std::vector<const char *> GetRunAfter() const override { return {"PlayerInputSystem"}; }

    void Update(float dt) override;

private:
    // ロックオンしていないとき、前方の円錐の中で最も近い生きている敵を探す。いなければ InvalidEntityID
    // 敵は、HealthComponent を持ち PlayerComponent を持たないエンティティ。coneCos は円錐の半角の cos
    CCL::ECS::EntityID FindBestFrontEnemy(CCL::ECS::EntityID selfID, const DirectX::SimpleMath::Vector3 &selfPos,
                                          const DirectX::SimpleMath::Vector3 &forward, float range, float coneCos);

    // 攻撃対象へ向く yaw を求め、attackFaceTimer を立てる（対象がなければ何もしない）
    void SetAttackFacing(CCL::ECS::EntityID playerID, PlayerComponent &player);
};