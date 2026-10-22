#pragma once
// ===========================================================================
// 概要: ムービー中の画面の額縁を作る System。上下の黒帯（レターボックス）を出し、HUD を薄くして隠す。
//
// 責務:
//   - GameFlowState::cinematic の間、上下から黒帯を出し、終わったら引っ込める
//   - 黒帯と同じ進み具合で、HUD（UIDrawList::hudSortEnd より奥の要素）を薄くして消す
//
// 備考:
//   黒帯はエンティティを作らず、ロックオンの印と同じく UIDrawList へ直接積む。
//   毎フレーム高さが変わるだけの2枚の四角形なので、部品を作って位置を書き換えるより簡単になる。
//
//   黒帯の描画順は、HUD より手前で、メニュー（ポーズなど）とテロップより奥にする。
//   ムービー中にポーズしても、ポーズ画面の上に黒帯が重ならない。
//
//   タイトル画面など、ムービーの無いシーンでも毎フレーム走る。そのときは HUD の不透明度に 1 を書くだけで、何も積まない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"

class CinematicFrameSystem : public CCL::ECS::IfSystem<CinematicFrameSystem,
                                                       CCL::ECS::Read<UITransformComponent>>
{
public:
    CinematicFrameSystem() : IfSystem("CinematicFrameSystem") {}
    virtual ~CinematicFrameSystem() = default;

    // 描画リストは収集 System と共有する。スケジューラは共有リソースを見ないので、収集の鎖の後ろに並べる
    std::vector<const char *> GetRunAfter() const override { return {"LockOnMarkerHudSystem"}; }

    void Update(float dt) override; // 黒帯の出入りを進め、黒帯を積み、HUD の不透明度を書く

private:
    static constexpr float kBarRatio  = 0.11f; // 黒帯1本の高さ（画面の高さに対する割合）
    static constexpr float kInTime    = 0.6f;  // 黒帯が出きるまでの時間（秒）
    static constexpr float kOutTime   = 0.5f;  // 黒帯が引っ込むまでの時間（秒）
    static constexpr int   kSortOrder = 190;   // 黒帯の描画順。HUD より手前、メニューとテロップより奥

    float m_amount = 0.0f; // 黒帯の出具合（0 = 無い、1 = 出きった）
};
