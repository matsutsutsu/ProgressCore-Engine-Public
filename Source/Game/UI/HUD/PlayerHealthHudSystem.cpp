#include "Game/UI/HUD/PlayerHealthHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h" // Damp: フレームレートに依らない追従の係数
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Serialization/Factory/Prefab.h"

#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/HUD/PlayerHudStyleComponent.h"
#include "Game/UI/Common/UiPrefabUtil.h"         // FindChildByName
#include "Game/UI/Debug/GaugeLabGui.h"           // OnGui の HP ボタン
#include "Game/Logics/AI/Battle/BattleContext.h" // OnGui で HP を書き換えるプレイヤーを引く

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace CCL::ECS;

namespace
{
    // --- プレハブ ---
    constexpr const char *kPrefabPath = "Assets/Prefabs/UI/PlayerHud.json";
    constexpr const char *kRootName   = "PlayerHud";     // ルート（Canvas）
    constexpr const char *kGaugeName  = "PlayerHpGauge"; // ゲージの子
    constexpr const char *kValueName  = "PlayerHpValue"; // 残り HP の数値の子

    DirectX::XMFLOAT4 Lerp(const DirectX::XMFLOAT4 &a, const DirectX::XMFLOAT4 &b, float t)
    {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    }
} // namespace



void PlayerHealthHudSystem::Update(float dt)
{
    // 処理フロー:
    // 1. プレイヤーの HP を読む（居なければゲージを隠して終わる）
    // 2. 初めてプレイヤーが居たフレームに、PlayerHud プレハブを生成する
    // 3. 割合と、瀕死の色・脈打ち・外側の光を書く
    // 4. 被弾・回復を見つけ、揺れと遅れて減るバーを進める
    // 5. 残り HP の数値を書く

    // ---------------------------------------------------------
    // 1. プレイヤーの HP
    // ---------------------------------------------------------

    // 参照でなく値で写す
    // 2 のプレハブ生成で World の予約が確定し、プレイヤーのデータが動くことがあるため
    bool  found = false;
    float curHp = 0.0f;
    float maxHp = 0.0f;
    ForEach([&](const HealthComponent &hp, const PlayerComponent & /*tag*/) {
        if (found) return; // プレイヤーは1体の前提
        found = true;
        curHp = hp.currentHealth;
        maxHp = hp.maxHealth;
    });

    auto *bar = m_world->GetComponent<UIProgressBarComponent>(m_barID);
    auto *txt = m_world->GetComponent<UITextComponent>(m_valueID);

    // プレイヤーが居ないシーン（タイトルなど）では作らない。作ってあれば隠す
    if (!found)
    {
        if (bar) bar->visible = false;
        if (txt) txt->visible = false;
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
        txt = m_world->GetComponent<UITextComponent>(m_valueID);
    }
    if (!bar) return;

    // プレハブのルートにスタイルが無ければ、既定値で動かす
    static const PlayerHudStyleComponent kDefaultStyle{};
    const auto *stylePtr = m_world->GetComponent<PlayerHudStyleComponent>(m_rootID);
    const auto &style    = stylePtr ? *stylePtr : kDefaultStyle;

    // ---------------------------------------------------------
    // 3. 割合と瀕死の演出
    // ---------------------------------------------------------
    const float ratio = (maxHp > 0.0f) ? std::clamp(curHp / maxHp, 0.0f, 1.0f) : 0.0f;

    // 最初は遅れて減るバーもすぐに合わせる
    // 起動した瞬間にバーが減っていくように見えないように
    if (!m_init)
    {
        bar->trailValue = ratio;
        m_prevRatio     = ratio;
        m_init          = true;
    }

    bar->visible = true;
    bar->value   = ratio;

    // 残りが少ないほど赤へ寄せ、赤くなった分だけ明るさを脈打たせる
    // 2つの割合をインスペクタで同じにしても 0 で割らないよう、幅に下限を付ける
    const float lowRange = std::max(style.lowHpStart - style.lowHpFull, 1e-4f);
    const float low      = std::clamp((style.lowHpStart - ratio) / lowRange, 0.0f, 1.0f);
    m_pulseTime += dt;
    bar->animTime += dt; // 光沢の位相。ゲームの dt で進めるのでポーズ中は止まる
    const float pulse = 0.5f - 0.5f * std::cos(m_pulseTime * style.pulseHz * 6.2831853f); // 0〜1

    // 暗くする方向に脈打たせる（1 を超える色は出せないため）
    DirectX::XMFLOAT4 fill = Lerp(style.fillNormal, style.fillLow, low);
    const float       dim  = 1.0f - low * style.pulseDepth * pulse;
    fill.x *= dim;
    fill.y *= dim;
    fill.z *= dim;
    bar->fillColor = fill;

    // 瀕死の外側の光は、脈打ちと同じ周期で明滅させる
    bar->auraLevel = low * (0.4f + 0.6f * pulse);

    // ---------------------------------------------------------
    // 4. 被弾・回復
    // ---------------------------------------------------------

    // HP が減ったら、遅れて減るバーを少し止めて失った量を見せ、ゲージを揺らす
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

    // ---------------------------------------------------------
    // 5. 残り HP の数値
    // ---------------------------------------------------------

    // 最大値は出さない。割合はゲージの長さが伝える
    if (txt)
    {
        txt->visible = true;
        char      buf[16];
        const int cur = static_cast<int>(std::max(0.0f, curHp) + 0.5f);
        std::snprintf(buf, sizeof(buf), "%d", cur);
        txt->text = buf;
    }
}



void PlayerHealthHudSystem::OnGui()
{
    if (!m_world->GetComponent<UIProgressBarComponent>(m_barID))
    {
        ImGui::TextDisabled("ゲージがまだ作られていない（プレイヤーが居るシーンで作る）");
        return;
    }

    // 値の調整はインスペクタ、ここは被弾・回復・瀕死を起こして確かめる場所
    ImGui::TextDisabled("見た目は Hierarchy の %s をインスペクタで調整し、プレハブ保存で %s へ残す", kRootName, kPrefabPath);

    const auto *battle = m_world->HasResource<BattleContext>() ? &m_world->GetResource<BattleContext>() : nullptr;
    auto       *hp     = (battle && battle->HasPlayer()) ? m_world->GetComponent<HealthComponent>(battle->playerEntity) : nullptr;
    if (hp)
    {
        CCL::GameUI::DrawHealthButtons(*hp, 0.1f);
    }
    else
    {
        ImGui::TextDisabled("プレイヤーが居ない");
    }
}



// PlayerHud プレハブを生成し、ゲージと数値の子を名前で引く
void PlayerHealthHudSystem::SpawnHud()
{
    // 失敗しても試すのは1回だけにする
    // プレハブが壊れているとき、毎フレーム読み直してエラーログが流れ続けないように
    m_spawned = true;

    m_rootID = Prefab::SpawnPrefab(*m_world, kPrefabPath);
    if (m_rootID == InvalidEntityID) return; // 読めない理由は Prefab がログに出す

    m_barID   = CCL::GameUI::FindChildByName(*m_world, m_rootID, kGaugeName);
    m_valueID = CCL::GameUI::FindChildByName(*m_world, m_rootID, kValueName);

    // エディタで子の名前を変えると引けなくなる
    // 黙ってゲージが消えると原因が分からないので、ログに出す
    if (m_barID == InvalidEntityID)
    {
        CCL_LOG_ERROR(LogCategory::Game, "[PlayerHud] %s に '%s' が見つかりません", kPrefabPath, kGaugeName);
    }
    if (m_valueID == InvalidEntityID)
    {
        CCL_LOG_ERROR(LogCategory::Game, "[PlayerHud] %s に '%s' が見つかりません", kPrefabPath, kValueName);
    }
}

REGISTER_LOGIC_SYSTEM(PlayerHealthHudSystem, Priority::LogicStage::L06_HudSync);
