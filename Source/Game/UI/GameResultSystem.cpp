#include "GameResultSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/MenuUiFactory.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Game/Logics/Combat/EnemyTag.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/AI/Battle/BattleContext.h"           // プレイヤーとボスの EntityID
#include "Game/Logics/AI/Drone/Components/DroneComponent.h" // 群れの機数を数える
#include "Game/Logics/GameEvent/GameEvents.h"              // 命中・被弾・ジャスト回避のイベント

#include <algorithm> // std::clamp / std::max
#include <cmath>     // std::lround
#include <cstdint>
#include <cstdio>    // std::snprintf

#include <imgui.h>

using namespace CCL::ECS;

namespace
{
    // --- 配置（画面の左上からの px。ローディング画面と同じく、見出し・線・行の左端を kLeft にそろえる） ---
    constexpr float kLeft    = 120.0f; // 左の余白
    constexpr float kHeadY   = 96.0f;  // 見出しの上端
    constexpr float kHeadPx  = 88.0f;  // 見出しの文字の高さ
    constexpr float kRuleY   = kHeadY + kHeadPx + 20.0f; // 見出しの下の線
    constexpr float kRuleW   = 820.0f; // 線の長さ
    constexpr float kSubY    = kRuleY + 16.0f;           // 行き先の行（線のすぐ下）
    constexpr float kStatY   = kSubY + 72.0f;            // 記録の1行目
    constexpr float kStatH   = 46.0f;  // 記録の行の間隔
    constexpr float kStatPx  = 26.0f;  // 記録の文字の高さ
    constexpr float kValueX  = kLeft + 300.0f; // 記録の値の列の左端（項目名の後ろをそろえる）
    constexpr float kMenuGap = 60.0f;  // 記録の最後の行からボタンの上端まで

    // --- 演出の時間（リザルトを出してからの秒） ---
    constexpr float kTypeSpeed = 70.0f; // 1秒に打ち込む文字数
    constexpr float kSubStart  = 0.35f; // 行き先の行を打ち始める
    constexpr float kStatStart = 0.6f;  // 記録の1行目を打ち始める
    constexpr float kStatStep  = 0.16f; // 記録の行ごとの時間差
    constexpr float kMenuDelay = 0.6f;  // 記録の最後の行を打ち始めてから、ボタンが出るまで

    // --- 色（文字は少し温かみのある白、飾りと勝ちは白金、ボスと負けは深紅。ローディング画面にそろえる） ---
    constexpr DirectX::XMFLOAT4 kGold{0.96f, 0.88f, 0.62f, 1.0f};
    constexpr DirectX::XMFLOAT4 kRed{0.95f, 0.18f, 0.22f, 1.0f};
    constexpr DirectX::XMFLOAT4 kLabel{0.93f, 0.91f, 0.86f, 0.55f}; // 記録の項目名（沈める）
    constexpr DirectX::XMFLOAT4 kValue{0.93f, 0.91f, 0.86f, 1.0f};  // 記録の値
} // namespace

void GameResultSystem::Initialize()
{
    // ポーズなどと共有するゲーム進行の状態。無ければ作る
    if (!m_world->HasResource<GameFlowState>())
        m_world->AddResource<GameFlowState>();

    // ボタンのクリックを購読する。ポーズのボタンも同じイベントを出すので、actionHash で見分ける
    const uint32_t kRetry = CCL::Utils::HashString("retry");
    const uint32_t kTitle = CCL::Utils::HashString("result_title");

    ListenEvent<UIButtonClickedEvent>(
        [this, kRetry, kTitle](const UIButtonClickedEvent &e) {
            if (m_transitioning) return;

            if (e.actionHash == kRetry)
            {
                // ゲームを読み直す。フェードと裏での読み込みは SceneManager が行う
                m_transitioning = SceneManager::Instance().RequestTransition("Game", true);
            }
            else if (e.actionHash == kTitle)
            {
                // タイトルへ戻る。アプリの終了はタイトル画面の QUIT だけにしてある。
                // この購読はワーカースレッドで走ることがあるので、World を片付けるのは
                // ここではなく SceneManager に任せる
                m_transitioning = SceneManager::Instance().RequestTransition("Title", true);
            }
        });

    // --- 戦いの記録。結果が出た後の分は数えない ---
    ListenEvent<AttackHitConfirmedEvent>([this](const AttackHitConfirmedEvent &e) {
        if (!m_resultShown && e.attackerID == m_playerID.load()) ++m_hits;
    });
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &e) {
        if (!m_resultShown && e.isPlayer) m_damageX10 += static_cast<int>(std::lround(e.damageAmount * 10.0f));
    });
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &) {
        if (!m_resultShown) ++m_justEvades;
    });
}

void GameResultSystem::Update(float dt)
{
    if (!m_debugEnabled) return; // ステージを確かめている間などに判定を止める（デバッグ用）

    if (m_resultShown)
    {
        AnimateResult(dt);
        return;
    }

    // 勝った後の待ち。dt はスローの影響を受けない実時間なので、数え終わればスローも終わっている
    if (m_pendingWin)
    {
        m_winDelay -= dt;
        if (m_winDelay <= 0.0f)
        {
            m_resultShown = true;
            ShowResultUI(true); // MISSION COMPLETE
            CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Clear. Result UI shown (after defeat slow-mo).");
        }
        return; // 待っている間は判定し直さない
    }

    bool playerPresent = false;
    bool playerDead    = false;
    bool anyEnemy      = false;
    int  aliveEnemies  = 0;

    // HealthComponent を持つエンティティを、プレイヤーと敵に分けて数える。
    // 敵かどうかは EnemyTag だけで決める（理由は EnemyTag.h を参照）
    ForEachWithID([&](EntityID id, const HealthComponent &hp) {
        const bool dead = (hp.currentHealth <= 0.0f) || m_world->HasComponent<Tag::DeadTag>(id);

        if (m_world->HasComponent<PlayerComponent>(id))
        {
            playerPresent = true;
            if (dead) playerDead = true;
        }
        else if (m_world->HasComponent<Tag::EnemyTag>(id))
        {
            anyEnemy = true;
            if (!dead) ++aliveEnemies;
        }
    });

    if (anyEnemy) m_sawEnemy = true;

    // OnGui で表示するために控えておく
    m_dbgPlayerPresent = playerPresent;
    m_dbgAliveEnemies  = aliveEnemies;

    // プレイヤーが居ないシーンでは判定しない。ステージを確かめるためのシーン（プレイヤーを置かない）で、
    // 敵として数えた HealthComponent が 0 になった瞬間に GAME CLEAR が出てしまうため
    if (!playerPresent) return;

    CountBattleStats(dt);

    // 負けを先に判定する
    if (playerDead)
    {
        m_resultShown = true;
        ShowResultUI(false); // MISSION FAILED
        CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Over. Result UI shown.");
    }
    else if (m_sawEnemy && aliveEnemies == 0)
    {
        // すぐには出さず、スローが終わってから出す（出すのは上の待ちの処理）
        m_pendingWin = true;
        m_winDelay   = kResultDelay;
        CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Clear detected. Waiting for defeat slow-mo before showing result.");
    }
}

void GameResultSystem::CountBattleStats(float dt)
{
    // Update はロジックの帯で走るので、ポーズ中は呼ばれない（時間も数えない）
    m_playTime += dt;

    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();
    m_playerID.store(battle.playerEntity);

    // 切り払い: 突進に入った瞬間を1回と数え、チェインの最大段数を控える
    if (const auto *cleave = battle.HasPlayer() ? m_world->GetComponent<CleaveComponent>(battle.playerEntity) : nullptr)
    {
        if (cleave->phase == CleavePhase::Lunging && m_prevCleave != CleavePhase::Lunging) ++m_cleaves;
        m_maxChain   = std::max(m_maxChain, cleave->chainCount);
        m_prevCleave = cleave->phase;
    }
}

CCL::ECS::EntityID GameResultSystem::SpawnText(const char *name, const std::string &text, float x, float y, float px,
                                               DirectX::XMFLOAT4 color, CCL::ECS::EntityID parent, float alpha)
{
    using namespace CCL::GameUI;
    auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UITextComponent>();
    auto e    = EntityFactory::SpawnNamed(*m_world, name, arch);

    UITransformComponent t;
    t.anchor = {0.0f, 0.0f};
    t.pivot  = {0.0f, 0.0f};
    t.offset = {x, y};
    t.size   = {1200.0f, px};
    t.alpha  = alpha;

    UITextComponent tx;
    tx.text      = text;
    tx.pixelSize = px;
    tx.color     = color;
    tx.sortOrder = 220;

    e.Set(MakeUINode(parent)).Set(t).Set(tx);
    return e.ID();
}

CCL::ECS::EntityID GameResultSystem::SpawnRect(const char *name, float x, float y, DirectX::XMFLOAT2 size, float rotation,
                                               DirectX::XMFLOAT4 color, CCL::ECS::EntityID parent, float alpha)
{
    using namespace CCL::GameUI;
    auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent>();
    auto e    = EntityFactory::SpawnNamed(*m_world, name, arch);

    UITransformComponent t;
    t.anchor   = {0.0f, 0.0f};
    t.pivot    = {0.5f, 0.5f};
    t.offset   = {x, y};
    t.size     = size;
    t.rotation = rotation;
    t.alpha    = alpha;

    UIImageComponent img;
    img.color     = color;
    img.sortOrder = 220;

    e.Set(MakeUINode(parent)).Set(t).Set(img);
    return e.ID();
}

void GameResultSystem::ShowResultUI(bool win)
{
    using namespace CCL::GameUI;
    World &w = *m_world;

    // --- 結果が出た時点の値 ---
    int swarm = 0;
    w.ViewEach<DroneComponent>([&swarm](EntityID) { ++swarm; });
    m_swarmMax = std::max(m_swarmMax, swarm);

    if (w.HasResource<BattleContext>())
    {
        const auto &battle = w.GetResource<BattleContext>();
        if (const auto *hp = battle.HasBoss() ? w.GetComponent<HealthComponent>(battle.bossEntity) : nullptr)
            m_bossHpRatio = std::clamp(hp->currentHealth / std::max(hp->maxHealth, 1.0f), 0.0f, 1.0f);
    }

    // 以降の UI はすべてこの Canvas の子にする
    const EntityID canvas = SpawnCanvas(w, "ResultCanvas");

    // 暗転。下ほど濃くして、左上の文字を読みやすくする（フェードだけ・スライドしない）
    const DirectX::XMFLOAT4 dimBottom{0.0f, 0.0f, 0.0f, 0.85f};
    SpawnPanel(w, "ResultDim", {0.0f, 0.0f}, {4000.0f, 4000.0f}, {0.0f, 0.0f, 0.0f, 0.55f}, 200,
               0.00f, 0.0f, canvas, &dimBottom);

    // --- 見出しと線（AnimateResult が滑り込ませ、伸ばす。最初は見えない） ---
    m_headingID = SpawnText("ResultHeading", win ? "MISSION COMPLETE" : "MISSION FAILED", kLeft, kHeadY, kHeadPx,
                            win ? kGold : kRed, canvas, 0.0f);

    m_ruleID    = SpawnRect("ResultRule", kLeft, kRuleY, {0.0f, 2.0f}, 0.0f, {kGold.x, kGold.y, kGold.z, 0.7f}, canvas);
    m_ruleTipID = SpawnRect("ResultRuleTip", kLeft, kRuleY, {9.0f, 9.0f}, 45.0f, kGold, canvas, 0.0f);

    // --- 打ち込む行（最初は空。AnimateResult が start 秒から1文字ずつ足す） ---
    m_typed.clear();
    auto typed = [&](const char *name, const std::string &full, float x, float y, float px, DirectX::XMFLOAT4 color,
                     float start) {
        m_typed.push_back({SpawnText(name, "", x, y, px, color, canvas), full, start});
    };

    // 行き先の行: 勝ちなら撃破、負けならボスの残り HP
    char sub[96];
    if (win)
        std::snprintf(sub, sizeof(sub), "TARGET : MURIEL  //  NEUTRALIZED");
    else
        std::snprintf(sub, sizeof(sub), "TARGET : MURIEL  //  HP %d%% REMAINING",
                      static_cast<int>(m_bossHpRatio * 100.0f + 0.5f));
    typed("ResultSub", sub, kLeft, kSubY, 20.0f, {kValue.x, kValue.y, kValue.z, 0.8f}, kSubStart);

    // 記録の行: 項目名（沈めた色）と値（明るい色）。値は項目名を打ち終えてから打ち始める
    const int  minutes = static_cast<int>(m_playTime) / 60;
    const float seconds = m_playTime - static_cast<float>(minutes * 60);
    char       buf[6][48];
    std::snprintf(buf[0], sizeof(buf[0]), "%02d:%05.2f", minutes, seconds);
    std::snprintf(buf[1], sizeof(buf[1]), "%d", m_hits.load());
    std::snprintf(buf[2], sizeof(buf[2]), "%d", (m_damageX10.load() + 5) / 10);
    std::snprintf(buf[3], sizeof(buf[3]), "%d", m_justEvades.load());
    std::snprintf(buf[4], sizeof(buf[4]), "%d   MAX CHAIN %d", m_cleaves, m_maxChain);
    std::snprintf(buf[5], sizeof(buf[5]), "%d UNITS", m_swarmMax);

    const char *labels[6] = {"TIME", "HITS", "DAMAGE TAKEN", "JUST EVADE", "CLEAVE", "SWARM ENGAGED"};
    const char *names[6]  = {"ResultTime", "ResultHits", "ResultDamage", "ResultEvade", "ResultCleave", "ResultSwarm"};
    for (int i = 0; i < 6; ++i)
    {
        const float y     = kStatY + kStatH * static_cast<float>(i);
        const float start = kStatStart + kStatStep * static_cast<float>(i);
        typed((std::string(names[i]) + "Label").c_str(), labels[i], kLeft, y, kStatPx, kLabel, start);
        typed(names[i], buf[i], kValueX, y, kStatPx, kValue,
              start + static_cast<float>(std::char_traits<char>::length(labels[i])) / kTypeSpeed);
    }

    // --- ボタンと操作のヒント ---
    // 左上を基準にした入れ物を置き、ボタンはその中央からの位置で置く（SpawnButton は中央基準のため）
    const float menuTop   = kStatY + kStatH * 5.0f + kStatPx + kMenuGap;
    const float menuDelay = kStatStart + kStatStep * 5.0f + kMenuDelay;
    EntityID    menu      = InvalidEntityID;
    {
        auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent>();
        auto e    = EntityFactory::SpawnNamed(w, "ResultMenu", arch);

        UITransformComponent t;
        t.anchor = {0.0f, 0.0f};
        t.pivot  = {0.0f, 0.0f};
        t.offset = {kLeft, menuTop};
        t.size   = {320.0f, 140.0f};

        e.Set(MakeUINode(canvas)).Set(t);
        menu = e.ID();
    }

    // 最初は RETRY を選んでおく
    SpawnButton(w, "BtnRetry", "LblRetry", "RETRY", {0.0f, -42.0f}, "retry", true, 230, menuDelay, menu);
    SpawnButton(w, "BtnResultTitle", "LblResultTitle", "TITLE", {0.0f, 43.0f}, "result_title", false, 230,
                menuDelay + 0.08f, menu);

    // 操作のヒント（決定・選択）。シーンを切り替えるときに消えるので、個別の ID は持たない
    std::vector<EntityID> hints;
    SpawnMenuHints(w, menu, 125.0f, hints, 240, menuDelay + 0.16f);

    m_resultTime = 0.0f;
}

void GameResultSystem::AnimateResult(float dt)
{
    m_resultTime += dt;
    const float t = m_resultTime;

    // 見出し: 左から減速しながら滑り込み、同時に濃くなる
    if (auto *tr = m_world->GetComponent<UITransformComponent>(m_headingID))
    {
        const float a = std::clamp(t / 0.35f, 0.0f, 1.0f);
        tr->offset.x  = kLeft - (1.0f - a) * (1.0f - a) * 40.0f;
        tr->alpha     = a;
    }

    // 線: 見出しを追って左から伸び、右端の◆が先端についていく
    {
        const float d    = std::clamp((t - 0.15f) / 0.35f, 0.0f, 1.0f);
        const float draw = 1.0f - (1.0f - d) * (1.0f - d);
        const float w    = kRuleW * draw;
        if (auto *tr = m_world->GetComponent<UITransformComponent>(m_ruleID))
        {
            tr->size.x   = w;
            tr->offset.x = kLeft + w * 0.5f; // 中心で置くので、左端を kLeft に止める
        }
        if (auto *tr = m_world->GetComponent<UITransformComponent>(m_ruleTipID))
        {
            tr->offset.x = kLeft + w;
            tr->alpha    = (d > 0.0f) ? 1.0f : 0.0f;
        }
    }

    // 打ち込む行: 打ち始めてからの秒数 × 速さの分だけ、先頭から文字を出す
    for (const TypedText &line : m_typed)
    {
        auto *tx = m_world->GetComponent<UITextComponent>(line.id);
        if (!tx) continue;
        const float n     = std::clamp((t - line.start) * kTypeSpeed, 0.0f, static_cast<float>(line.full.size()));
        const auto  count = static_cast<size_t>(n);
        if (tx->text.size() != count) tx->text = line.full.substr(0, count);
    }
}

void GameResultSystem::OnGui()
{
    ImGui::SeparatorText("リザルト検知 (デバッグ)");

    ImGui::Checkbox("システム有効", &m_debugEnabled);
    if (!m_debugEnabled)
        ImGui::TextDisabled("→ 勝敗判定・リザルト表示を停止中");

    ImGui::Separator();
    ImGui::Text("プレイヤー存在 : %s", m_dbgPlayerPresent ? "yes" : "NO (テストScene扱い→抑止)");
    ImGui::Text("敵を観測(sawEnemy) : %s", m_sawEnemy ? "yes" : "no");
    ImGui::Text("生存敵数 : %d", m_dbgAliveEnemies);
    ImGui::Text("リザルト表示済み : %s", m_resultShown ? "yes" : "no");
    ImGui::Text("勝利待機中 : %s (残り %.2fs)", m_pendingWin ? "yes" : "no", m_winDelay);

    ImGui::SeparatorText("戦いの記録");
    ImGui::Text("時間 %.2fs / 命中 %d / 被ダメージ %.1f / ジャスト回避 %d / 切り払い %d (最大チェイン %d)",
                m_playTime, m_hits.load(), m_damageX10.load() / 10.0f, m_justEvades.load(), m_cleaves, m_maxChain);

    ImGui::Separator();
    if (ImGui::Button("検知状態をリセット"))
    {
        m_resultShown   = false;
        m_sawEnemy      = false;
        m_pendingWin    = false;
        m_winDelay      = 0.0f;
        m_transitioning = false;
    }
    ImGui::SameLine();
    // 戦わずにリザルトの見た目を確かめる（今の記録のまま出す）
    if (!m_resultShown && ImGui::Button("勝ちのリザルトを出す"))
    {
        m_resultShown = true;
        ShowResultUI(true);
    }
    ImGui::SameLine();
    if (!m_resultShown && ImGui::Button("負けのリザルトを出す"))
    {
        m_resultShown = true;
        ShowResultUI(false);
    }
    ImGui::TextDisabled("※プレイヤー不在のSceneでは自動でリザルトを抑止します");
}
