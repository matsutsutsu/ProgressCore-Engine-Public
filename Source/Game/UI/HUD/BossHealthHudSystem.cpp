#include "Game/UI/HUD/BossHealthHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h" // Damp: フレームレートに依らない追従の係数
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/Transform/Components/TransformComponent.h" // UI の親子関係に使う
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/Logics/AI/Battle/BattleContext.h"                       // ドローンの生存数
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h" // 第2形態へ移る HP の割合
#include "Game/UI/Common/MenuUiFactory.h"                                    // SpawnCanvas / MakeUINode
#include "Game/UI/Debug/GaugeLabGui.h"                                     // OnGui の演出切り替え・HP ボタン

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace CCL::ECS;

namespace
{
    // ボスの HP バーは残りの量で色を変えず、深紅のままにする（ドローンと同じ敵側の色）。失った分は琥珀色で見せる
    const DirectX::XMFLOAT4 kBossFill  = {0.80f, 0.15f, 0.18f, 1.0f};
    const DirectX::XMFLOAT4 kBossTrail = {1.00f, 0.82f, 0.35f, 0.95f};
    const DirectX::XMFLOAT4 kBossBg    = {0.00f, 0.00f, 0.00f, 0.55f};
    const DirectX::XMFLOAT4 kSubText   = {0.86f, 0.86f, 0.88f, 0.80f}; // ドローン数（名前より控えめにする）

    // 名前はプレハブ（Muriel.json）の名前に合わせる。シーンに置いたエンティティ名は
    // 「Muriel (1)」のように変わり得るので、NameComponent からは取らない
    constexpr const char *kBossName = "MURIEL";

    // --- 配置（画面上辺の中央） ---
    constexpr float kBarTop  = 56.0f;  // 画面の上端からゲージの上辺まで
    constexpr float kBarW    = 900.0f; // ゲージの幅
    constexpr float kBarH    = 14.0f;  // ゲージの高さ
    constexpr float kNamePx  = 22.0f;  // 名前の文字の高さ
    constexpr float kNameGap = 6.0f;   // 名前の下端からゲージの上辺まで
    constexpr float kSubPx   = 15.0f;  // ドローン数の文字の高さ
    constexpr float kSubGap  = 8.0f;   // ゲージの下辺からドローン数の上端まで
} // namespace

void BossHealthHudSystem::Initialize()
{
    // ゲージと飾りはこの Canvas の子にする
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "BossHpCanvas");

    // --- ゲージ（画面上部の中央） ---
    auto barArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                             UIProgressBarComponent>();
    auto bar     = EntityFactory::SpawnNamed(*m_world, "BossHpGauge", barArch);

    UITransformComponent t;
    t.anchor = {0.5f, 0.0f}; // 画面の上辺の中央
    t.pivot  = {0.5f, 0.0f}; // 自分の上辺の中央をアンカーに合わせる
    t.offset = {0.0f, kBarTop};
    t.size   = {kBarW, kBarH};

    UIProgressBarComponent b;
    b.sortOrder  = 100;
    b.showTrail  = true; // 失った分を遅れて減らすバーを出す
    b.bgColor    = kBossBg;
    b.fillColor  = kBossFill;
    b.trailColor = kBossTrail;
    b.gradient   = 0.45f; // 下端を暗くして、一色の塗りに見えないようにする
    b.frame      = 2.0f;  // 明るい空の上でもゲージの範囲が分かるよう、暗い縁で囲む
    b.slant      = 12.0f; // 両端を斜めに切る
    b.segments   = 5.0f;  // 2割ずつ区切る（プレイヤーのゲージと揃える）
    b.glowColor  = {1.0f, 0.70f, 0.35f, 0.9f}; // 削った先端を琥珀色に光らせる（遅延バーと同じ系統）
    b.auraWidth  = 10.0f; // 第2形態の間、外側に深紅の光をまとう
    b.auraColor  = {0.95f, 0.10f, 0.12f, 0.7f};
    b.value      = 1.0f;
    b.trailValue = 1.0f;
    b.visible    = false; // ボスが出るまで隠す

    bar.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(b);
    m_barID = bar.ID();

    auto textArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UITextComponent>();

    // --- 名前（ゲージの左端に揃えて上に置く） ---
    //   中央揃えにしないのは、文字の幅を測って寄せる仕組みが無く、手で合わせた offset は
    //   名前や文字の大きさを変えるたびにずれるため。左端揃えなら名前が変わってもずれない
    {
        auto e = EntityFactory::SpawnNamed(*m_world, "BossHpName", textArch);

        UITransformComponent nt;
        nt.anchor = {0.5f, 0.0f};
        nt.pivot  = {0.0f, 1.0f}; // 左下をゲージの左上（から少し上）に合わせる
        nt.offset = {-kBarW * 0.5f, kBarTop - kNameGap};
        nt.size   = {kBarW, kNamePx};

        UITextComponent nx;
        nx.text      = kBossName;
        nx.pixelSize = kNamePx;
        nx.sortOrder = 110;
        nx.shadow    = true; // 空の上でも読めるように
        nx.visible   = false;

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(nt).Set(nx);
        m_nameID = e.ID();
    }

    // --- ドローンの残り数（ゲージの左端に揃えて下に置く） ---
    //   ボスの体力とは別に「群れの規模」を数字で見せ、画面のドローンが1機ずつ数えられた個体だと伝える
    {
        auto e = EntityFactory::SpawnNamed(*m_world, "BossSwarmCount", textArch);

        UITransformComponent st;
        st.anchor = {0.5f, 0.0f};
        st.pivot  = {0.0f, 0.0f};
        st.offset = {-kBarW * 0.5f, kBarTop + kBarH + kSubGap};
        st.size   = {kBarW, kSubPx};

        UITextComponent sx;
        sx.text      = "";
        sx.pixelSize = kSubPx;
        sx.color     = kSubText;
        sx.sortOrder = 110;
        sx.shadow    = true;
        sx.visible   = false;

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(st).Set(sx);
        m_swarmID = e.ID();
    }
}

void BossHealthHudSystem::SetDecorVisible(bool visible)
{
    if (auto *txt = m_world->GetComponent<UITextComponent>(m_nameID)) txt->visible = visible;
    if (auto *txt = m_world->GetComponent<UITextComponent>(m_swarmID)) txt->visible = visible;
}

void BossHealthHudSystem::Update(float dt)
{
    auto *bar = (m_barID != InvalidEntityID) ? m_world->GetComponent<UIProgressBarComponent>(m_barID) : nullptr;
    if (!bar) return;

    // HP が 0 より大きいボスを1体選ぶ。倒したボスは対象にせず、戦闘の後はゲージを隠す
    const HealthComponent *boss   = nullptr;
    EntityID               bossID = InvalidEntityID;
    ForEachWithID([&](EntityID id, const HealthComponent &hp, const BossActionComponent & /*tag*/) {
        if (boss) return;
        if (hp.currentHealth > 0.0f)
        {
            boss   = &hp;
            bossID = id;
        }
    });

    if (!boss)
    {
        bar->visible = false;
        SetDecorVisible(false);
        m_init  = false; // 次のボスが出たら値を合わせ直す
        m_shake = 0.0f;
        return;
    }

    const float ratio = (boss->maxHealth > 0.0f)
                            ? std::clamp(boss->currentHealth / boss->maxHealth, 0.0f, 1.0f)
                            : 0.0f;

    // 最初は遅れて減るバーもすぐに合わせる（出た瞬間にバーが減っていくように見えないように）
    if (!m_init)
    {
        bar->trailValue = ratio;
        m_prevRatio     = ratio;
        m_init          = true;
    }

    bar->visible = true;
    bar->value   = ratio;
    SetDecorVisible(true);

    // --- 形態移行の目印: ボスの閾値の位置に線を引き、HP がそこを下回ったら消す（もう起きたことは目印にしない） ---
    {
        const auto *phase = m_world->GetComponent<BossPhaseComponent>(bossID);
        // 第2形態の BT が無いボスは形態移行しない（BossPhaseComponent の phase2AssetPath の説明）ので線も引かない
        const bool  hasPhase2 = phase && !phase->phase2AssetPath.empty();
        const float threshold = hasPhase2 ? std::clamp(phase->phase2HealthThreshold / 100.0f, 0.0f, 1.0f) : 0.0f;
        bar->marker           = (hasPhase2 && ratio > threshold) ? threshold : -1.0f;

        // 第2形態に入ったら、外側の深紅の光をゆっくり明滅させる（ボスが本気になったことを伝える）
        const bool inPhase2 = hasPhase2 && ratio <= threshold;
        bar->auraLevel      = inPhase2 ? 0.6f + 0.4f * std::sin(bar->animTime * 2.0f) : 0.0f;
    }

    // --- ドローンの残り数 ---
    if (auto *swarm = m_world->GetComponent<UITextComponent>(m_swarmID))
    {
        const int alive = m_world->HasResource<BattleContext>()
                              ? m_world->GetResource<BattleContext>().droneAliveCount
                              : 0;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "SWARM  %d", alive);
        swarm->text = buf;
    }

    // HP が減ったら、遅れて減るバーを少し止めて失った分を見せ、ゲージを揺らす。
    //   被弾してからの経過時間はシェーダーのグリッチが使う（ボスは既定でグリッチを出さないが、設定すれば出る）
    bar->hitAge += dt;
    bar->healAge += dt;
    if (ratio < m_prevRatio - 1e-4f)
    {
        m_trailHold  = kTrailHold;
        m_shake      = 1.0f;
        m_shakeTime  = 0.0f; // 毎回同じ揺れ方から始める
        bar->hitAge  = 0.0f;
    }
    // HP が増えたら、増えた分に左から光が満ちる演出を始める
    else if (ratio > m_prevRatio + 1e-4f)
    {
        bar->healFrom = m_prevRatio;
        bar->healAge  = 0.0f;
    }

    bar->animTime += dt; // 光沢と外側の光の明滅の位相。ゲームの dt で進めるのでポーズ中は止まる

    // 揺れを指数関数的に減らす。ウィジェットは揺れの量を持つだけで、減らすのはこの System
    m_shakeTime += dt;
    m_shake -= m_shake * CCL::Math::Damp(kShakeDecay, dt);
    if (m_shake < 0.02f) m_shake = 0.0f; // わずかな揺れが 1px 残り続けないように 0 にする
    bar->shake        = m_shake;
    bar->shakeElapsed = m_shakeTime;

    // 遅れて減るバー: 回復・変化なしならすぐ合わせ、被弾したら少し止めてから value へ近づける
    if (ratio >= bar->trailValue)
    {
        bar->trailValue = ratio;
    }
    else if (m_trailHold > 0.0f)
    {
        m_trailHold -= dt;
    }
    else
    {
        const float k = CCL::Math::Damp(kTrailSpeed, dt);
        bar->trailValue += (ratio - bar->trailValue) * k;
    }

    m_prevRatio = ratio;
}

void BossHealthHudSystem::OnGui()
{
    auto *bar = (m_barID != InvalidEntityID) ? m_world->GetComponent<UIProgressBarComponent>(m_barID) : nullptr;
    if (!bar)
    {
        ImGui::TextDisabled("ゲージがまだ作られていない");
        return;
    }

    ImGui::TextDisabled("ボスの HP ゲージの演出の見比べ用（保存はされない。既定値は Initialize）");
    CCL::GameUI::DrawGaugeEffectControls(*bar);

    ImGui::Separator();
    const auto *battle = m_world->HasResource<BattleContext>() ? &m_world->GetResource<BattleContext>() : nullptr;
    auto       *hp     = (battle && battle->HasBoss()) ? m_world->GetComponent<HealthComponent>(battle->bossEntity) : nullptr;
    if (hp)
        CCL::GameUI::DrawHealthButtons(*hp, 0.05f);
    else
        ImGui::TextDisabled("ボスが居ない");
}

REGISTER_LOGIC_SYSTEM(BossHealthHudSystem, Priority::LogicStage::L06_HudSync);
