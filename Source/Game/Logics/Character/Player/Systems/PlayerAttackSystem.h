#pragma once
// プレイヤーの入力意思を評価し、アニメーションの先行入力バッファ（トリガー）を登録するシステム
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Game/Logics/Character/Player/Components/CleaveComponent.h" // 切り払い中は通常攻撃を抑止
#include <SimpleMath.h>

class PlayerAttackSystem : public CCL::ECS::IfSystem<PlayerAttackSystem,
                                                     CCL::ECS::Read<PlayerInputComponent>,
                                                     CCL::ECS::Write<DashComponent>,
                                                     CCL::ECS::Write<AnimParametersComponent>,
                                                     CCL::ECS::Write<PlayerComponent>,
                                                     CCL::ECS::Read<CleaveComponent>>
{
public:
    PlayerAttackSystem()
        : IfSystem("PlayerAttackSystem")
    {
    }
    virtual ~PlayerAttackSystem() = default;

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // PlayerInput が書いた PlayerInputComponent を読む。
    std::vector<const char *> GetRunAfter() const override { return {"PlayerInputSystem"}; }

    void Update(float dt) override;

private:
    // 前方コーン内で最も近い「生存中の敵」を探す。ロックオンが無い時のフォールバック。
    // 敵＝HealthComponent を持ち PlayerComponent を持たない生存エンティティ。
    CCL::ECS::EntityID FindBestFrontEnemy(CCL::ECS::EntityID selfID, const DirectX::SimpleMath::Vector3 &selfPos,
                                          const DirectX::SimpleMath::Vector3 &forward, float range, float coneCos);

    // 攻撃対象へ向く yaw を求め、attackFaceTimer を立てる（対象が無ければ何もしない）。
    void SetAttackFacing(CCL::ECS::EntityID playerID, PlayerComponent &player);
};