// 役割・設計方針は CinematicSignalSystem.h を参照。
#include "Game/Logics/Cinematic/CinematicSignalSystem.h"
#include "Game/UI/Common/ScreenHeadingUi.h" // テロップの部品（滑り込む見出し・伸びる線・打ち込まれる1行）
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Time/TimeService.h" // Time::PresentationDt（ポーズ中はテロップも止める）

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>
#include <utility>

using namespace CCL::ECS;
using namespace CCL::GameUI;

namespace
{
    constexpr float kDefaultTelopSeconds = 3.0f;   // value を省略したときの表示時間(秒)
    constexpr float kTelopFadeSeconds    = 0.5f;   // 消えるのにかける時間(秒)
    constexpr float kSubtitlePx          = 30.0f;  // 下の1行の文字の高さ(px)
    constexpr float kSubtitleDelay       = 0.35f;  // 名前が滑り込んでから下の1行を打ち始めるまで(秒)
    constexpr int   kTelopSortOrder      = 230;    // HUD（220 前後）より手前に出す

    // テロップの入れ物。画面の左下にそろえる。見出しの部品は入れ物の左上からの位置で並ぶので、
    // 入れ物ごと左下へ置けば、解像度が変わっても画面の下端からの距離が保たれる
    constexpr DirectX::XMFLOAT2 kTelopBoxSize   = {1400.0f, 360.0f};
    constexpr DirectX::XMFLOAT2 kTelopBoxOffset = {0.0f, -40.0f}; // 下端から少し浮かせる

    constexpr const char *kBgmPrefix = "bgm:/"; // この名前で始まる音源を BGM とみなす
} // namespace

// ---------------------------------------------------------------------------
// 入口とシグナルの振り分け
// ---------------------------------------------------------------------------

void CinematicSignalSystem::Initialize()
{
    // シグナルはロジックステージから同期で届く。ここでは溜めるだけにして、UI は自分の Update で作る
    ListenEvent<TimelineSignalEvent>([this](const TimelineSignalEvent &ev) { m_pending.push_back(ev); });

    // スキップで終わったら、出ているテロップを残さない（本編が始まっているのに名前が居座らないように）
    ListenEvent<TimelineFinishedEvent>([this](const TimelineFinishedEvent &ev) {
        if (ev.skipped) m_dismissTelops = true;
    });
}

void CinematicSignalSystem::Update(float dt)
{
    dt = Time::PresentationDt(*m_world, dt);

    std::vector<TimelineSignalEvent> pending;
    pending.swap(m_pending);
    for (const auto &ev : pending)
        HandleSignal(ev);

    if (m_dismissTelops)
    {
        for (auto &telop : m_telops)
            telop.remain = 0.0f;
        m_dismissTelops = false;
    }

    UpdateTelops(dt);
}

void CinematicSignalSystem::HandleSignal(const TimelineSignalEvent &ev)
{
    if (ev.name == "Telop")
    {
        SpawnTelop(ev.param, ev.param2, ev.value > 0.0f ? ev.value : kDefaultTelopSeconds);
        return;
    }

    if (ev.name == "BGM")
    {
        SwitchBgm(ev.param);
        return;
    }

    // 黙って捨てると、名前の打ち間違いに気付けない
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
    std::vector<EntityID> heading;
    SpawnScreenHeading(w, box, title.c_str(), ScreenColor::kRed, heading, kTelopSortOrder);
    for (EntityID id : heading)
    {
        telop.entities.push_back(id);
        telop.parts.push_back({id});
    }

    if (!subtitle.empty())
    {
        const EntityID sub = SpawnTypedText(w, "TelopSubtitle", subtitle, ScreenLayout::kLeft, ScreenLayout::kSubY,
                                            kSubtitlePx, ScreenColor::kSub, kSubtitleDelay, box);
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

// ---------------------------------------------------------------------------
// BGM
// ---------------------------------------------------------------------------

void CinematicSignalSystem::SwitchBgm(const std::string &eventName)
{
    int switched = 0;
    ForEachWithID([&](EntityID id, AudioEmitterComponent &emitter) {
        const bool isBgm = m_bgmEmitters.count(id) != 0 || emitter.eventName.rfind(kBgmPrefix, 0) == 0;
        if (!isBgm) return;
        m_bgmEmitters.insert(id);

        // 名前とハッシュを書き換えるだけ。AudioSystem がハッシュの食い違いを見て、止めて鳴らし直す。
        // 空にすると、ハッシュが 0 のまま食い違うので止まり、autoPlay を倒すので鳴り直さない
        emitter.eventName = eventName;
        emitter.eventHash = 0;
        emitter.autoPlay  = !eventName.empty();
        ++switched;
    });

    if (switched == 0)
        CCL_LOG_WARN(LogCategory::Game, "[CinematicSignal] BGM の音源（%s で始まる AudioEmitter）が見つかりません",
                     kBgmPrefix);
}

// UI の状態を決める帯。テロップを作るのも消すのもここで行う
REGISTER_RENDER_SYSTEM(CinematicSignalSystem, Priority::RenderStage::R10_UI);
