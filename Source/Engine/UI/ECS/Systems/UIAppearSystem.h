#pragma once
// UIAppearComponent を駆動し、オーバーレイの出現アニメ（フェード＋上スライド）を再生する。
// 位置付け: RenderStage::R10_UI（UpdateRender中・毎フレーム1回、dtで進行）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIAppearComponent.h"

class UIAppearSystem : public CCL::ECS::IfSystem<UIAppearSystem,
                                                 CCL::ECS::Write<UITransformComponent>,
                                                 CCL::ECS::Write<UIAppearComponent>>
{
public:
    UIAppearSystem()
        : IfSystem("UIAppearSystem")
    {
    }

    // UIAnimationSystem と同じ UITransform を書くので直列化される（触る中身は
    // scale と alpha/offset.y で重ならないため、順序はどちらでも結果は同じ）。
    // ただし宣言が無いと「順序不定」の警告に出続けるので、意図として固定しておく。
    std::vector<const char *> GetRunAfter() const override { return {"UIAnimationSystem"}; }

    void Update(float dt) override;
};
