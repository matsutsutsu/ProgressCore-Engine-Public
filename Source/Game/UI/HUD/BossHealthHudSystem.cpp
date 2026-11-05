#include "Game/UI/HUD/BossHealthHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Serialization/Factory/Prefab.h"

#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/UI/HUD/BossHudStyleComponent.h"
#include "Game/UI/Common/UiPrefabUtil.h"
#include "Game/UI/Debug/GaugeLabGui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace CCL::ECS;

namespace
{
    // --- プレハブ ---
    constexpr const char *kPrefabPath = "Assets/Prefabs/UI/BossHud.json";
    constexpr const char *kRootName   = "BossHud";        // ルート（Canvas）
    constexpr const char *kGaugeName  = "BossHpGauge";    // ゲージの子
    constexpr const char *kNameName   = "BossHpName";     // ボスの名前の子
    constexpr const char *kSwarmName  = "BossSwarmCount"; // ドローンの残り数の子
} // namespace



void BossHealthHudSystem::Update(float dt)
{
    // 処理フロー:
    // 1. HP が残っているボスを1体選ぶ（いなければまとめて隠して終わる）
    // 2. ボスが初めて出たフレームに、BossHud プレハブを生成する
    // 3. 割合と、第2形態の目印・外側の光を書く
    // 4. ドローンの残り数を書く
    // 5. 被弾・回復を見つけ、揺れと遅れて減るバーを進める

    // ---------------------------------------------------------
    // 1. ボスを選ぶ
    // ---------------------------------------------------------

    // 参照でなく値で写す
    // 2 のプレハブ生成で World の予約が確定し、ボスのデータが動くことがあるため
    bool     found  = false;
    float    curHp  = 0.0f;
    float    maxHp  = 0.0f;
    EntityID bossID = InvalidEntityID;
    ForEachWithID([&](EntityID id, const HealthComponent &hp, const BossActionComponent & /*tag*/) {
        if (found) return;

        // 倒したボスは対象にしない。戦闘の後はゲージを隠す
        if (hp.currentHealth > 0.0f)
        {
            found  = true;
            curHp  = hp.currentHealth;
            maxHp  = hp.maxHealth;
            bossID = id;
        }
    });

    auto *bar = m_world->GetComponent<UIProgressBarComponent>(m_barID);

    if (!found)
    {
        if (bar) bar->visible = false;
        SetDecorVisible(false);
        m_init  = false; // 次のボスが出たら値を合わせ直す
        m_shake = 0.0f;
        return;
    }

    // ---------------------------------------------------------
    // 2. 生成
    // ---------------------------------------------------------
    if (!m_spawned)
    {
        SpawnHud();
        bar = m_world->GetComponent<UIProgressBarComponent>(m_barID);
    }
    if (!bar) return;

    // プレハブのルートにスタイルがなければ、既定値で動かす
    static const BossHudStyleComponent kDefaultStyle{};
    const auto *stylePtr = m_world->GetComponent<BossHudStyleComponent>(m_rootID);
    const auto &style    = stylePtr ? *stylePtr : kDefaultStyle;

    // ---------------------------------------------------------
    // 3. 割合と第2形態の演出
    // ---------------------------------------------------------
    const float ratio = (maxHp > 0.0f) ? std::clamp(curHp / maxHp, 0.0f, 1.0f) : 0.0f;

    // 最初は遅れて減るバーもすぐに合わせる
    // 出た瞬間にバーが減っていくように見えないように
    if (!m_init)
    {
        bar->trailValue = ratio;
        m_prevRatio     = ratio;
        m_init          = true;
    }

    bar->visible = true;
    bar->value   = ratio;
    SetDecorVisible(true);

    // 光沢と外側の光の明滅の位相。ゲームの dt で進めるのでポーズ中は止まる
    bar->animTime += dt;

    // --- 第2形態の目印と光 ---
    {
        // 第2形態の BT がないボスは形態移行しないので、線も引かない
        const auto *phase     = m_world->GetComponent<BossPhaseComponent>(bossID);
        const bool  hasPhase2 = phase && !phase->phase2AssetPath.empty();
        const float threshold = hasPhase2 ? std::clamp(phase->phase2HealthThreshold / 100.0f, 0.0f, 1.0f) : 0.0f;

        // 移る位置に線を引き、HP がそこを下回ったら消す
        bar->marker = (hasPhase2 && ratio > threshold) ? threshold : -1.0f;

        // 第2形態に入ったら、外側の光をゆっくり明滅させる（ボスが本気になったことを伝える）
        const bool inPhase2 = hasPhase2 && ratio <= threshold;
        if (inPhase2)
        {
            const float mid  = (style.phase2AuraMin + style.phase2AuraMax) * 0.5f;
            const float half = (style.phase2AuraMax - style.phase2AuraMin) * 0.5f;
            bar->auraLevel   = mid + half * std::sin(bar->animTime * style.phase2AuraPulse * 6.2831853f);
        }
        else
        {
            bar->auraLevel = 0.0f;
        }
    }

    // ---------------------------------------------------------
    // 4. ドローンの残り数
    // ---------------------------------------------------------
    if (auto *swarm = m_world->GetComponent<UITextComponent>(m_swarmID))
    {
        const int alive = m_world->HasResource<BattleContext>()
                              ? m_world->GetResource<BattleContext>().droneAliveCount
                              : 0;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "SWARM  %d", alive);
        swarm->text = buf;
    }

    // ---------------------------------------------------------
    // 5. 被弾・回復
    // ---------------------------------------------------------

    // HP が減ったら、遅れて減るバーを少し止めて失った分を見せ、ゲージを揺らす
    // 被弾してからの経過時間（hitAge）はシェーダーのグリッチが使う
    bar->hitAge += dt;
    bar->healAge += dt;
    if (ratio < m_prevRatio - 1e-4f)
    {
        m_trailHold = style.trailHold;
        m_shake     = 1.0f;
        m_shakeTime = 0.0f; // 毎回同じ揺れ方から始める
        bar->hitAge = 0.0f;
    }
    // HP が増えたら、増えた分に左から光が満ちる演出を始める
    else if (ratio > m_prevRatio + 1e-4f)
    {
        bar->healFrom = m_prevRatio;
        bar->healAge  = 0.0f;
    }

    // 揺れを減らすのはこの System。ウィジェットは揺れの量を持つだけ
    // わずかな揺れが 1px 残り続けないよう、小さくなったら 0 にする
    m_shakeTime += dt;
    m_shake -= m_shake * CCL::Math::Damp(style.shakeDecay, dt);
    if (m_shake < 0.02f) m_shake = 0.0f;
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
        const float k = CCL::Math::Damp(style.trailSpeed, dt);
        bar->trailValue += (ratio - bar->trailValue) * k;
    }
    m_prevRatio = ratio;
}



void BossHealthHudSystem::OnGui()
{
    if (!m_world->GetComponent<UIProgressBarComponent>(m_barID))
    {
        ImGui::TextDisabled("ゲージがまだ作られていない（ボスが居るシーンで作る）");
        return;
    }

    // 値の調整はインスペクタ、ここは被弾・回復・第2形態を起こして確かめる場所
    ImGui::TextDisabled("見た目は Hierarchy の %s をインスペクタで調整し、プレハブ保存で %s へ残す", kRootName, kPrefabPath);

    const auto *battle = m_world->HasResource<BattleContext>() ? &m_world->GetResource<BattleContext>() : nullptr;
    auto       *hp     = (battle && battle->HasBoss()) ? m_world->GetComponent<HealthComponent>(battle->bossEntity) : nullptr;
    if (hp)
    {
        CCL::GameUI::DrawHealthButtons(*hp, 0.05f);
    }
    else
    {
        ImGui::TextDisabled("ボスが居ない");
    }
}



void BossHealthHudSystem::SpawnHud()
{
    // 失敗しても試すのは1回だけにする
    // プレハブが壊れているとき、毎フレーム読み直してエラーログが流れ続けないように
    m_spawned = true;

    m_rootID = Prefab::SpawnPrefab(*m_world, kPrefabPath);
    if (m_rootID == InvalidEntityID) return; // 読めない理由は Prefab がログに出す

    m_barID   = CCL::GameUI::FindChildByName(*m_world, m_rootID, kGaugeName);
    m_nameID  = CCL::GameUI::FindChildByName(*m_world, m_rootID, kNameName);
    m_swarmID = CCL::GameUI::FindChildByName(*m_world, m_rootID, kSwarmName);

    // エディタで子の名前を変えると引けなくなる
    // 黙って消えると原因が分からないので、ログに出す
    const auto reportMissing = [](EntityID id, const char *name) {
        if (id == InvalidEntityID)
        {
            CCL_LOG_ERROR(LogCategory::Game, "[BossHud] %s に '%s' が見つかりません", kPrefabPath, name);
        }
    };
    reportMissing(m_barID, kGaugeName);
    reportMissing(m_nameID, kNameName);
    reportMissing(m_swarmID, kSwarmName);
}



void BossHealthHudSystem::SetDecorVisible(bool visible)
{
    if (auto *txt = m_world->GetComponent<UITextComponent>(m_nameID)) txt->visible = visible;
    if (auto *txt = m_world->GetComponent<UITextComponent>(m_swarmID)) txt->visible = visible;
}



REGISTER_LOGIC_SYSTEM(BossHealthHudSystem, Priority::LogicStage::L06_HudSync);
