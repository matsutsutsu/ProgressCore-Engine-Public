#pragma once
// ===========================================================================
// 概要: UIRevealComponent を持つ UI の部品を、出方に合わせて毎フレーム動かす System
//
// 責務:
//   - Type: 文字列を先頭から1文字ずつ出す
//   - SlideIn: 左から滑り込ませながら濃くする
//   - Grow: 左端を止めたまま幅を伸ばし、先端の部品をついていかせる
//
// 備考: ポーズ中もメニューの演出を進めるため、描画ステージで動かす
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Game/UI/Common/UIRevealComponent.h"

class UIRevealSystem : public CCL::ECS::IfSystem<
    UIRevealSystem,
    CCL::ECS::Write<UIRevealComponent>,
    CCL::ECS::Write<UITransformComponent>>
{
public:
    UIRevealSystem() : IfSystem("UIRevealSystem") {}

    // 配置を確定させる UILayoutSystem と同じ帯なので、その後に動かす
    // 書いた位置と幅は次のフレームの配置から効く（1フレームの遅れは見て分からない）
    std::vector<const char *> GetRunAfter() const override { return {"UILayoutSystem"}; }

    void Update(float dt) override;
};
