#pragma once
// ===========================================================================
// 概要: 攻撃ボタンの入力から、アニメーションの Attack トリガーを出す System。
//
// 責務:
//   - 切り払いが Idle のときだけ攻撃入力を受け、Attack トリガーを先行入力付きで出す
//   - 攻撃の瞬間に、ロックオン先か前方の最も近い敵の方へ向く yaw を書く
//   - ダッシュ中に攻撃したら、ダッシュを打ち切ってクールダウンに入れる
//
// 備考: 通常/ダッシュ/空中/コンボのどの攻撃になるかはアニメグラフが決める。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include <SimpleMath.h>

class PlayerAttackSystem : public CCL::ECS::IfSystem<PlayerAttackSystem,
                                                     CCL::ECS::Read<PlayerInputComponent>,
                                                     CCL::ECS::Write<DashComponent>,
                                                     CCL::ECS::Write<AnimParametersComponent>,
                                                     CCL::ECS::Write<PlayerComponent>>
{
public:
    PlayerAttackSystem()
        : IfSystem("PlayerAttackSystem")
    {
    }
    virtual ~PlayerAttackSystem() = default;

    // PlayerInputSystem が書いた PlayerInputComponent を読むので、同じ L01_Input の中でその後に走る。
    std::vector<const char *> GetRunAfter() const override { return {"PlayerInputSystem"}; }

    void Update(float dt) override;

private:
    /**
     * @brief 前方の円錐内で最も近い生存中の敵を探す。ロックオンしていないときに使う。
     *
     * 敵 = HealthComponent を持ち、PlayerComponent を持たない、生存中のエンティティ。
     *
     * @param selfID  自分のエンティティ。候補から除く。
     * @param selfPos 自分の位置（ワールド）。
     * @param forward 円錐の向き（正規化済み）。
     * @param range   探す距離の上限（m）。
     * @param coneCos 円錐の半角の cos。forward との内積がこれ未満の敵は除く。
     * @return 見つかった敵。居なければ InvalidEntityID。
     */
    CCL::ECS::EntityID FindBestFrontEnemy(CCL::ECS::EntityID selfID, const DirectX::SimpleMath::Vector3 &selfPos,
                                          const DirectX::SimpleMath::Vector3 &forward, float range, float coneCos);

    // 攻撃対象へ向く yaw を求め、attackFaceTimer を立てる（対象が無ければ何もしない）。
    void SetAttackFacing(CCL::ECS::EntityID playerID, PlayerComponent &player);
};