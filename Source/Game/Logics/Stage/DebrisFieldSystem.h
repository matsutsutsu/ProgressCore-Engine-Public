#pragma once
// ===========================================================================
// 概要: がれきの帯（DebrisFieldComponent）を回し、岩を RenderQueue に直接積む System（R08_Main）。
//
// 責務:
//   - フォルダのプレハブから岩の種類を読み、岩1個ごとの乱数を作る
//   - 経過時間に TimeStateComponent の倍率を掛け、公転・自転・上下の揺れを計算する
//   - 岩ごとのワールド行列を RenderCommand にして積む
//
// 備考: カリングと LOD は OpaquePass / NormalPrepass がメッシュごとに行うので、ここではしない。
//       影は積まない。帯はステージの外にあり、影のカスケードに入らないため。
//       ModelRenderSystem と同じステージで RenderQueue に積むので、GetRunAfter で直列にする
//       （型が競合しないと並列に走り、RenderQueue の配列への push_back が競合する）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Stage/DebrisFieldComponent.h"

#include <cstdint>

class DebrisFieldSystem : public CCL::ECS::IfSystem<DebrisFieldSystem,
                                                    CCL::ECS::Write<DebrisFieldComponent>,
                                                    CCL::ECS::Read<TransformComponent>,
                                                    CCL::ECS::Read<TimeStateComponent>>
{
public:
    DebrisFieldSystem() : IfSystem("DebrisFieldSystem") {}

    void Update(float dt) override;

    void OnGui() override;

    std::vector<const char *> GetRunAfter() const override { return {"ModelRenderSystem"}; }

private:
    // フォルダ・数・シードが変わっていれば、岩の種類と乱数を作り直す
    void RebuildIfChanged(DebrisFieldComponent &field);

    // prefabFolder のプレハブから、Model を持つものを岩の種類として読む
    void LoadKinds(DebrisFieldComponent &field);

    // 岩1個ごとの乱数を作る。起動のたびに同じ並びになるよう、番号とシードから作る
    void BuildRocks(DebrisFieldComponent &field);

    // 帯の岩を全部 RenderQueue に積む
    void SubmitRocks(const DebrisFieldComponent &field, const TransformComponent &transform);

    // 番号から 0〜1 の疑似乱数を作る（TitleSwarmSystem と同じ式）
    static float Hash01(uint32_t seed);

    uint32_t m_submittedLastFrame = 0; // 前のフレームに積んだ岩の数（OnGui の表示用）
};
