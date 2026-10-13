#pragma once
// ===========================================================================
// 概要: ムービー中の画面の額縁を作る System。上下の黒帯（レターボックス）を出し、HUD を薄くして隠す。
//
// 責務:
//   - GameFlowState::cinematic の間、上下から黒帯を出す（終わったら引っ込める）
//   - 同じ進み具合で HUD（UIDrawList::hudSortEnd より奥の要素）を薄くして消す
//
// 備考:
//   - 黒帯はエンティティを作らず、ロックオンの印と同じく UIDrawList へ直接積む。
//     毎フレーム高さが変わるだけの2枚の矩形なので、部品を作って位置を書き換えるより簡単。
//   - 黒帯の描画順は HUD より手前・メニュー（ポーズ 200〜）とテロップ（230）より奥。
//     ムービー中にポーズしても、ポーズ画面の上に黒帯は重ならない。
//   - 進み具合は Time::PresentationDt で数えるので、ポーズ中は黒帯も止まる。
//   - タイトル画面など、ムービーの無いシーンでは何も積まない（HUD の不透明度は 1 のまま書く）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"

class CinematicFrameSystem : public CCL::ECS::IfSystem<CinematicFrameSystem,
                                                       CCL::ECS::Read<UITransformComponent>>
{
public:
    CinematicFrameSystem() : IfSystem("CinematicFrameSystem") {}
    virtual ~CinematicFrameSystem() = default;

    // 描画リストは収集 System で共有する（スケジューラは共有リソースを見ないので、収集の鎖の後ろに並べる）
    std::vector<const char *> GetRunAfter() const override { return {"LockOnMarkerHudSystem"}; }

    void Update(float dt) override; // 黒帯の出入りを進め、黒帯を積み、HUD の不透明度を書く

private:
    static constexpr float kBarRatio  = 0.11f; // 黒帯1本の高さ（画面の高さに対する割合）
    static constexpr float kInTime    = 0.6f;  // 黒帯が出きるまでの秒数
    static constexpr float kOutTime   = 0.5f;  // 黒帯が引っ込むまでの秒数
    static constexpr int   kSortOrder = 190;   // HUD（〜121）より手前、メニュー（200〜）より奥

    float m_amount = 0.0f; // 黒帯の出具合（0 = 無い、1 = 出きった）
};
