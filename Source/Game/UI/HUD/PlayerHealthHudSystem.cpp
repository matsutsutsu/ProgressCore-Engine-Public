#include "Game/UI/HUD/PlayerHealthHudSystem.h"

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
#include "Game/UI/Common/MenuUiFactory.h" // SpawnCanvas / MakeUINode
#include "Game/UI/Debug/GaugeLabGui.h"   // OnGui の演出切り替え・HP ボタン
#include "Game/Logics/AI/Battle/BattleContext.h" // OnGui で HP を書き換えるプレイヤーを引く

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace CCL::ECS;

namespace
{
    // 自分側の UI は白金の単色にする（鎧の黄に合わせる）。残りで色を変える信号色はやめ、
    // 少ないときだけ赤へ寄せて脈打たせる。色が変わるのが「危ない」ときだけなら、変化そのものが警告になる
    const DirectX::XMFLOAT4 kFillNormal = {0.96f, 0.88f, 0.62f, 1.0f};
    const DirectX::XMFLOAT4 kFillLow    = {0.92f, 0.24f, 0.20f, 1.0f};
    const DirectX::XMFLOAT4 kTrail      = {1.00f, 1.00f, 1.00f, 0.85f}; // 失った分は白く光らせてから消す
    const DirectX::XMFLOAT4 kBg         = {0.00f, 0.00f, 0.00f, 0.55f};
    const DirectX::XMFLOAT4 kValueText  = {0.86f, 0.86f, 0.88f, 0.85f}; // 数値は読まれなくてよいので、ゲージより控えめにする

    // --- 配置（画面左下。CleaveGaugeHudSystem のストック表示がこのゲージの上に乗る） ---
    constexpr float kMarginLeft   = 48.0f;  // 画面の左端から
    constexpr float kMarginBottom = 56.0f;  // 画面の下端からゲージの下辺まで
    constexpr float kBarW         = 440.0f; // ゲージの幅
    constexpr float kBarH         = 16.0f;  // ゲージの高さ。細くした方が上品に見え、3D の絵を隠しにくい
    constexpr float kValueGap     = 14.0f;  // ゲージの右端から数値まで
    constexpr float kValuePx      = 16.0f;  // 数値の文字の高さ

    DirectX::XMFLOAT4 Lerp(const DirectX::XMFLOAT4 &a, const DirectX::XMFLOAT4 &b, float t)
    {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    }
} // namespace

void PlayerHealthHudSystem::Initialize()
{
    // ゲージと数値はこの Canvas の子にする
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "PlayerHpCanvas");

    // --- ゲージ（画面左下） ---
    auto barArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                             UIProgressBarComponent>();
    auto bar     = EntityFactory::SpawnNamed(*m_world, "PlayerHpGauge", barArch);

    UITransformComponent t;
    t.anchor = {0.0f, 1.0f}; // 画面の左下
    t.pivot  = {0.0f, 1.0f}; // 自分の左下をアンカーに合わせる（下端からの距離で置ける）
    t.offset = {kMarginLeft, -kMarginBottom};
    t.size   = {kBarW, kBarH};

    UIProgressBarComponent b;
    b.sortOrder  = 100;
    b.showTrail  = true; // 失った分を遅れて減らすバーを出す
    b.bgColor    = kBg;
    b.fillColor  = kFillNormal;
    b.trailColor = kTrail;
    b.gradient   = 0.35f; // 下端を暗くして、一色の塗りに見えないようにする
    b.frame      = 2.0f;  // 淡いフィルが明るい床に溶けないよう、暗い縁で囲む
    b.slant      = 10.0f; // 両端を斜めに切る
    b.segments   = 5.0f;  // 2割ずつ区切り、残りを目で数えられるようにする
    b.glowColor  = {1.0f, 0.95f, 0.80f, 0.8f}; // 減っている途中のフィル先端を光らせる
    b.sheen      = 0.12f; // 控えめな光沢を流し、止まった塗りに見えないようにする
    b.glitch     = 1.0f;  // 被弾した瞬間、ゲージが横に裂けて色が分かれる
    b.auraWidth  = 8.0f;  // 瀕死のとき、外側に赤い光をにじませる
    b.auraColor  = {1.0f, 0.18f, 0.12f, 0.9f};
    b.value      = 1.0f;
    b.trailValue = 1.0f;

    bar.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(b);
    m_barID = bar.ID();

    // --- 残り HP の数値（ゲージの右に小さく） ---
    //   アクションでは数値はほぼ読まれず、ゲージの長さで判断する。消しはせず、目に入っても邪魔しない大きさにする
    auto valueArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                               UITextComponent>();
    auto value     = EntityFactory::SpawnNamed(*m_world, "PlayerHpValue", valueArch);

    UITransformComponent vt;
    vt.anchor = {0.0f, 1.0f};
    vt.pivot  = {0.0f, 1.0f};
    vt.offset = {kMarginLeft + kBarW + kValueGap, -kMarginBottom}; // 文字は左上から描くので、下辺をゲージの下辺に揃える
    vt.size   = {80.0f, kValuePx};

    UITextComponent vx;
    vx.text      = "";
    vx.pixelSize = kValuePx;
    vx.color     = kValueText;
    vx.sortOrder = 110; // ゲージ（100）より手前に置く
    vx.shadow    = true; // 明るい床の上でも読めるように

    value.Set(CCL::GameUI::MakeUINode(canvas)).Set(vt).Set(vx);
    m_valueID = value.ID();
}

void PlayerHealthHudSystem::Update(float dt)
{
    // 生成した直後はコンポーネントがまだ反映されていないことがあるので、null なら何もしない
    auto *bar = (m_barID != InvalidEntityID) ? m_world->GetComponent<UIProgressBarComponent>(m_barID) : nullptr;
    auto *txt = (m_valueID != InvalidEntityID) ? m_world->GetComponent<UITextComponent>(m_valueID) : nullptr;
    if (!bar) return;

    bool found = false;
    ForEach([&](const HealthComponent &hp, const PlayerComponent & /*tag*/) {
        if (found) return; // プレイヤーは1体の前提。最初の1つだけを表示する
        found = true;

        const float ratio = (hp.maxHealth > 0.0f)
                                ? std::clamp(hp.currentHealth / hp.maxHealth, 0.0f, 1.0f)
                                : 0.0f;

        // 最初は遅れて減るバーもすぐに合わせる（起動した瞬間にバーが減っていくように見えないように）
        if (!m_init)
        {
            bar->trailValue = ratio;
            m_prevRatio     = ratio;
            m_init          = true;
        }

        bar->visible = true;
        bar->value   = ratio;

        // 残りが少ないほど赤へ寄せ、赤くなった分だけ明るさを脈打たせる（多いときは静かな単色のまま）
        const float low = std::clamp((kLowHpStart - ratio) / (kLowHpStart - kLowHpFull), 0.0f, 1.0f);
        m_pulseTime += dt;
        bar->animTime += dt; // 光沢の位相。ゲームの dt で進めるのでポーズ中は止まる
        const float pulse = 0.5f - 0.5f * std::cos(m_pulseTime * kPulseHz * 6.2831853f); // 0〜1
        DirectX::XMFLOAT4 fill = Lerp(kFillNormal, kFillLow, low);
        const float       dim  = 1.0f - low * 0.35f * pulse; // 暗くする方向に脈打たせる（1 を超える色は出せないため）
        fill.x *= dim;
        fill.y *= dim;
        fill.z *= dim;
        bar->fillColor = fill;

        // 瀕死の演出: 外側の赤い光（脈打ちと同じ周期で明滅させる）
        bar->auraLevel = low * (0.4f + 0.6f * pulse);

        // HP が減ったら、遅れて減るバーを少し止めて失った量を見せ、ゲージを揺らして当たったことを伝える。
        //   被弾してからの経過時間はシェーダーのグリッチが使う
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

        // 揺れを減らすのはこの System。ウィジェットは揺れの量を持つだけ
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

        if (txt)
        {
            txt->visible = true;
            char      buf[16];
            const int cur = static_cast<int>(std::max(0.0f, hp.currentHealth) + 0.5f);
            std::snprintf(buf, sizeof(buf), "%d", cur); // 最大値は出さない。割合はゲージの長さが伝える
            txt->text = buf;
        }

        m_prevRatio = ratio;
    });

    // プレイヤーが居ないシーン（タイトルなど）では隠す
    if (!found)
    {
        bar->visible = false;
        m_shake      = 0.0f;
        if (txt) txt->visible = false;
    }
}

void PlayerHealthHudSystem::OnGui()
{
    auto *bar = (m_barID != InvalidEntityID) ? m_world->GetComponent<UIProgressBarComponent>(m_barID) : nullptr;
    if (!bar)
    {
        ImGui::TextDisabled("ゲージがまだ作られていない");
        return;
    }

    ImGui::TextDisabled("HP ゲージの演出の見比べ用（保存はされない。既定値は Initialize）");
    CCL::GameUI::DrawGaugeEffectControls(*bar);

    ImGui::Separator();
    const auto *battle = m_world->HasResource<BattleContext>() ? &m_world->GetResource<BattleContext>() : nullptr;
    auto       *hp     = (battle && battle->HasPlayer()) ? m_world->GetComponent<HealthComponent>(battle->playerEntity) : nullptr;
    if (hp)
        CCL::GameUI::DrawHealthButtons(*hp, 0.1f);
    else
        ImGui::TextDisabled("プレイヤーが居ない");
}

REGISTER_LOGIC_SYSTEM(PlayerHealthHudSystem, Priority::LogicStage::L06_HudSync);
