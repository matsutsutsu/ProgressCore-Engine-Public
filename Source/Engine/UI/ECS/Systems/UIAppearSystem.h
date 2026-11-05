#pragma once
// ===========================================================================
// 概要: UIAppearComponent の出現アニメ（フェードイン + 下からのスライド）を進める System（R10_UI）
//
// 責務:
//   - UITransform の alpha と offset.y を、経過時間に合わせて開始状態から定位置へ動かす
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIAppearComponent.h"

class UIAppearSystem : public CCL::ECS::IfSystem<
    UIAppearSystem,
    CCL::ECS::Write<UITransformComponent>,
    CCL::ECS::Write<UIAppearComponent>>
{
public:
    UIAppearSystem() : IfSystem("UIAppearSystem") {}

    // UIAnimationSystem と同じ UITransform を書くので直列に走る。触るのは scale と
    // alpha/offset.y で重ならず、順序はどちらでも結果は同じだが、
    // 宣言がないと「順序不定」の警告が出続けるので固定しておく
    std::vector<const char *> GetRunAfter() const override { return {"UIAnimationSystem"}; }

    void Update(float dt) override;
};
