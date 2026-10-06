#pragma once
// ===========================================================================
// 概要: UIRevealComponent を持つ UI の部品を、出方に合わせて毎フレーム動かす System。
//
// 責務:
//   - Type: 打ち始めてからの秒数 × 速さの分だけ、文字列を先頭から出す
//   - SlideIn: 左から減速しながら滑り込ませ、同時に濃くする
//   - Grow: 左端を止めたまま幅を伸ばし、先端の部品をついていかせる
//
// 備考: 描画ステージで走るのは、ポーズ中もメニューの演出を進める必要があるため。
//       UILayoutSystem の後に走るので、書いた位置と幅は次のフレームの配置から効く（1フレームの遅れは見えない）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Game/UI/Common/UIRevealComponent.h"

class UIRevealSystem : public CCL::ECS::IfSystem<UIRevealSystem,
                                                 CCL::ECS::Write<UIRevealComponent>,
                                                 CCL::ECS::Write<UITransformComponent>>
{
public:
    UIRevealSystem()
        : IfSystem("UIRevealSystem")
    {
    }

    // 配置を確定させる System と同じ帯なので、順番を決めておく
    std::vector<const char *> GetRunAfter() const override
    {
        return {"UILayoutSystem"};
    }

    void Update(float dt) override;
};
