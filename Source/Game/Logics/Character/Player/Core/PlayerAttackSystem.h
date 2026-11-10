#pragma once
// ===========================================================================
// 概要: 攻撃ボタンの入力から、アニメーションの Attack トリガーを出す System
//
// 責務:
//   - 攻撃入力を受け、Attack トリガーを先行入力付きで出す
//   - 押した瞬間のスティックの向き（敵に対して前・後ろ・それ以外）を AttackDir に書く
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
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include <SimpleMath.h>

class PlayerAttackSystem : public CCL::ECS::IfSystem<
    PlayerAttackSystem,
    CCL::ECS::Read<PlayerInputComponent>,
    CCL::ECS::Write<AnimParametersComponent>,
    CCL::ECS::Write<PlayerComponent>,
    CCL::ECS::Read<PlayerRushComponent>,
    CCL::ECS::Read<CharacterMovementComponent>> // スティックの向き（PlayerInputSystem が書く）
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

    // 攻撃対象（ロックオン先か前方の最も近い敵）への水平の向きを outDir に返す
    // 対象がいない・attackFaceMaxDistance より遠いときは false
    bool FindAttackTargetDir(CCL::ECS::EntityID playerID, const PlayerComponent &player,
                             DirectX::SimpleMath::Vector3 &outDir);

    // スティックの向きを、baseDir（敵の方か今の正面）に対する前・後ろ・それ以外に分けて AttackDir の値で返す
    float ClassifyAttackDir(const DirectX::SimpleMath::Vector3 &baseDir, const CharacterMovementComponent &movement) const;
};