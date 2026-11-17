#include "Game/Logics/Cinematic/CinematicSignalSystem.h"
#include "Game/UI/Common/ScreenHeadingUi.h"
#include "Game/Logics/AI/Drone/Systems/DroneManagerSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Time/TimeService.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>
#include <utility>

using namespace CCL::ECS;
using namespace CCL::GameUI;

namespace
{
    constexpr float kDefaultTelopSeconds = 3.0f;  // value を省略したときの表示時間（秒）
    constexpr float kTelopFadeSeconds    = 0.5f;  // 消えるのにかける時間（秒）
    constexpr float kSubtitlePx          = 30.0f; // 下の1行の文字の高さ（px）
    constexpr float kSubtitleDelay       = 0.35f; // 名前が滑り込んでから下の1行を打ち始めるまで（秒）
    constexpr int   kTelopSortOrder      = 230;   // HUD（220 前後）より手前に出す

    // テロップの入れ物。画面の左下にそろえる。見出しの部品は入れ物の左上からの位置で並ぶので、
    // 入れ物ごと左下へ置けば、解像度が変わっても画面の下端からの距離が保たれる
    constexpr DirectX::XMFLOAT2 kTelopBoxSize   = {1400.0f, 360.0f};
    constexpr DirectX::XMFLOAT2 kTelopBoxOffset = {0.0f, -40.0f}; // 下端から少し浮かせる
}

// ---------------------------------------------------------------------------
// 入口とシグナルの振り分け
// ---------------------------------------------------------------------------

void CinematicSignalSystem::Initialize()
{
    // シグナルはロジックの段から同期で届く。ここでは溜めるだけにして、UI は自分の Update で作る
    ListenEvent<CinematicSignalEvent>([this](const CinematicSignalEvent &ev) { m_pending.push_back(ev); });

    // スキップで終わったら、出ているテロップを残さない（本編が始まっているのに名前が居座らないように）
    ListenEvent<CinematicFinishedEvent>([this](const CinematicFinishedEvent &ev) {
        if (ev.skipped) m_dismissTelops = true;
    });
}



void CinematicSignalSystem::Update(float dt)
{
    dt = Time::PresentationDt(*m_world, dt);

    // 届いたシグナルを1つずつ処理し、テロップを出す
    std::vector<CinematicSignalEvent> pending;
    pending.swap(m_pending);
    for (const auto &ev : pending) HandleSignal(ev);

    // スキップで終わったら、出ているテロップをすぐ消え始めさせる
    if (m_dismissTelops)
    {
        for (auto &telop : m_telops) telop.remain = 0.0f;
        m_dismissTelops = false;
    }

    // テロップの残り時間を進め、消えきったものを破棄する
    UpdateTelops(dt);
}



void CinematicSignalSystem::HandleSignal(const CinematicSignalEvent &ev)
{
    if (ev.name == "Telop")
    {
        SpawnTelop(ev.param, ev.param2, ev.value > 0.0f ? ev.value : kDefaultTelopSeconds);
        return;
    }

    // 増援は DroneManagerSystem が受ける
    if (ev.name == DroneManagerSystem::kReinforceSignal) return;

    // エンジンは名前の意味を知らないので、台本の読み込みでは打ち間違いに気付けない
    // 黙って捨てると気付く場所がなくなるので、受け手のここで知らせる
    CCL_LOG_WARN(LogCategory::Game, "[CinematicSignal] 知らないシグナルです: %s", ev.name.c_str());
}



// ---------------------------------------------------------------------------
// テロップ
// ---------------------------------------------------------------------------

void CinematicSignalSystem::SpawnTelop(const std::string &title, const std::string &subtitle, float seconds)
{
    World &w = *m_world;

    Telop telop;
    telop.remain = seconds;

    const EntityID canvas = SpawnCanvas(w, "TelopCanvas");
    const EntityID box    = SpawnBox(w, "TelopBox", {0.0f, 1.0f}, kTelopBoxOffset, kTelopBoxSize, canvas);
    telop.entities.push_back(canvas);
    telop.entities.push_back(box);

    // 名前（滑り込む）・線（伸びる）・線の先の◆。リザルト・ポーズの見出しと同じ部品を、入れ物を親にして使う
    const MenuStyleComponent style = GetMenuStyle(w);
    std::vector<EntityID>    heading;
    SpawnScreenHeading(w, box, title.c_str(), style.red, heading, kTelopSortOrder);
    for (EntityID id : heading)
    {
        telop.entities.push_back(id);
        telop.parts.push_back({id});
    }

    if (!subtitle.empty())
    {
        const EntityID sub = SpawnTypedText(w, "TelopSubtitle", subtitle, style.left, style.SubY(), kSubtitlePx,
                                            style.sub, kSubtitleDelay, box);
        telop.entities.push_back(sub);
        telop.parts.push_back({sub});
    }

    m_telops.push_back(std::move(telop));
}



void CinematicSignalSystem::UpdateTelops(float dt)
{
    for (auto &telop : m_telops)
    {
        if (telop.remain > 0.0f)
        {
            telop.remain -= dt;
            continue;
        }

        // 消え始め。出現演出（UIRevealSystem）が UITransform の alpha を毎フレーム 1 に書き戻すので、
        // フェードは文字・画像の色の不透明度のほうで行う。元の値（線は 0.7 など）を基準に控えておく
        if (!telop.fadeCaptured)
        {
            for (auto &part : telop.parts)
                part.baseAlpha = ReadPartAlpha(part.id);
            telop.fadeCaptured = true;
        }

        telop.fade += dt;
        const float k = std::clamp(1.0f - telop.fade / kTelopFadeSeconds, 0.0f, 1.0f);
        for (const auto &part : telop.parts)
            ApplyPartAlpha(part, k);
    }

    // 消えきったものを破棄する（実際に消えるのは、次に構造の変更を反映したとき）
    auto done = [](const Telop &t) { return t.fadeCaptured && t.fade >= kTelopFadeSeconds; };
    for (const auto &telop : m_telops)
    {
        if (!done(telop)) continue;
        for (EntityID id : telop.entities)
            m_world->Destroy(id);
    }
    m_telops.erase(std::remove_if(m_telops.begin(), m_telops.end(), done), m_telops.end());
}



float CinematicSignalSystem::ReadPartAlpha(EntityID id) const
{
    if (const auto *tx = m_world->GetComponent<UITextComponent>(id)) return tx->color.w;
    if (const auto *img = m_world->GetComponent<UIImageComponent>(id)) return img->color.w;
    return 1.0f;
}



void CinematicSignalSystem::ApplyPartAlpha(const TelopPart &part, float k) const
{
    if (auto *tx = m_world->GetComponent<UITextComponent>(part.id))
    {
        tx->color.w = part.baseAlpha * k;
        return;
    }
    if (auto *img = m_world->GetComponent<UIImageComponent>(part.id))
        img->color.w = part.baseAlpha * k;
}



REGISTER_RENDER_SYSTEM(CinematicSignalSystem, Priority::RenderStage::R10_UI);
