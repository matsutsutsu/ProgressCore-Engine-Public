#include "Game/UI/HUD/TutorialHintHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/Core/Math/Interpolation.h" // Damp
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/Input/InputIconComponent.h"
#include "Game/UI/Common/MenuUiFactory.h"        // SpawnCanvas / MakeUINode
#include "Game/Logics/AI/Battle/BattleContext.h" // プレイヤーが現れてから作る

using namespace CCL::ECS;

namespace
{
    // --- 帯 ---
    constexpr float kBottom   = 200.0f; // 画面の下端から帯の中心まで（px）
    constexpr float kBandH    = 90.0f;  // 帯の高さ（px）
    constexpr float kPadX     = 39.0f;  // 帯の左右の余白（px）
    constexpr float kIconSize = 64.0f;  // アイコンの一辺（px）。画像と同じ大きさ
    constexpr float kGap      = 22.0f;  // アイコン・「+」・文の間（px）
    constexpr float kTextSize = 36.0f;  // 文字の高さ（px）
    constexpr float kEmphSize = 48.0f;  // 「今だ」の文字の高さ（px）

    // --- ゲージ ---
    constexpr float kBarH = 8.0f;                        // 太さ（px）
    constexpr float kBarY = kBandH * 0.5f + kBarH * 0.5f; // 帯の下端にすき間なく付ける

    // --- 中央の「今だ」のボタン ---
    constexpr float kNowIconSize = 128.0f; // 画像（64px）のちょうど2倍
    constexpr float kNowOffsetY  = 140.0f; // 画面の中央から下へ（px）

    // --- 色 ---
    constexpr DirectX::XMFLOAT4 kBandColor  = {0.02f, 0.03f, 0.05f, 0.70f};
    constexpr DirectX::XMFLOAT4 kFillColor  = {0.35f, 0.75f, 1.00f, 0.95f};
    constexpr DirectX::XMFLOAT4 kClearColor = {0.40f, 0.95f, 0.50f, 0.95f}; // 達成の余韻
    constexpr DirectX::XMFLOAT4 kTextColor  = {1.00f, 1.00f, 1.00f, 1.00f};
    constexpr DirectX::XMFLOAT4 kNowColor   = {1.00f, 0.85f, 0.30f, 1.00f}; // 「今だ」の文字

    // --- 速さ（Damp の speed） ---
    constexpr float kFadeSpeed    = 10.0f;
    constexpr float kFillSpeed    = 12.0f;
    constexpr float kNowFadeSpeed = 20.0f; // 押す瞬間なので速く
} // namespace

// ---------------------------------------------------------------------------
// 更新
// ---------------------------------------------------------------------------

void TutorialHintHudSystem::Update(float dt)
{
    if (!m_world->HasResource<TutorialState>()) return;
    const TutorialState &state = m_world->GetResource<TutorialState>();

    // Initialize で作らないのは、その後のレベルの読み込みで消されるため
    if (!m_built)
    {
        if (!m_world->HasResource<BattleContext>()) return;
        if (!m_world->GetResource<BattleContext>().HasPlayer()) return;

        Build();
        m_built = true;
        return; // 作ったフレームはまだ部品を引けないので、書くのは次のフレームから
    }

    // --- 文言 ---
    // 項目が変わったら書き換えてフェードインし直す。「今だ」の切り替えは消さずにそのまま書き換える
    const TutorialStepDef *def = state.Current();
    if (def)
    {
        if (state.step != m_lastStep)
        {
            m_alpha      = 0.0f;
            m_gaugeShown = 0.0f;
            ApplyHint(*def, state.assistActive);
        }
        else if (state.assistActive != m_lastAssist)
        {
            ApplyHint(*def, state.assistActive);
        }
        m_lastStep   = state.step;
        m_lastAssist = state.assistActive;
    }

    // --- フェード ---
    const float target = state.showHint ? 1.0f : 0.0f;
    m_alpha += (target - m_alpha) * CCL::Math::Damp(kFadeSpeed, dt);

    SetAlpha(m_bandID, m_alpha);
    SetAlpha(m_barID, m_alpha);
    SetAlpha(m_fillID, m_alpha);
    SetAlpha(m_iconID, m_alpha);
    SetAlpha(m_textID, m_alpha);
    SetAlpha(m_plusID, m_hasSecond ? m_alpha : 0.0f);
    SetAlpha(m_icon2ID, m_hasSecond ? m_alpha : 0.0f);

    UpdateGauge(state, dt);
    UpdateNowButton(state, dt);
}

void TutorialHintHudSystem::ApplyHint(const TutorialStepDef &def, bool assist)
{
    const bool        useAssist = assist && def.assistText;
    const std::string text      = useAssist ? def.assistText : def.text;
    const float       textSize  = useAssist ? kEmphSize : kTextSize;

    // 「今だ」は1つ目のボタンだけを出す
    m_hasSecond = def.action2 && !useAssist;

    // --- 幅 ---
    // [アイコン] (+ [アイコン2]) [文] の組の幅を求め、帯の中央に置く
    const float textW = EstimateWidth(text, textSize);
    const float plusW = EstimateWidth("+", kTextSize);

    float rowW = kIconSize + kGap + textW;
    if (m_hasSecond)
    {
        rowW += plusW + kGap + kIconSize + kGap;
    }
    m_bandWidth = rowW + kPadX * 2.0f;

    // --- 並べる ---
    float x = -rowW * 0.5f;

    SetRect(m_iconID, x, 0.0f, kIconSize, kIconSize);
    x += kIconSize + kGap;

    if (m_hasSecond)
    {
        SetRect(m_plusID, x, 0.0f, plusW, kTextSize);
        x += plusW + kGap;

        SetRect(m_icon2ID, x, 0.0f, kIconSize, kIconSize);
        x += kIconSize + kGap;
    }

    SetRect(m_textID, x, 0.0f, textW, textSize);
    SetRect(m_bandID, -m_bandWidth * 0.5f, 0.0f, m_bandWidth, kBandH);
    SetRect(m_barID, -m_bandWidth * 0.5f, kBarY, m_bandWidth, kBarH);

    // --- 中身 ---
    // アイコンの画像は InputIconSystem が種類とアクション名から決める
    if (auto *ic = m_world->GetComponent<InputIconComponent>(m_iconID))
    {
        ic->kind = def.icon;
        if (def.action)
        {
            ic->actionHash = CCL::Utils::HashString(def.action);
        }
    }
    if (auto *ic = m_world->GetComponent<InputIconComponent>(m_icon2ID); ic && def.action2)
    {
        ic->actionHash = CCL::Utils::HashString(def.action2);
    }
    if (auto *tx = m_world->GetComponent<UITextComponent>(m_textID))
    {
        tx->text      = text;
        tx->pixelSize = textSize;
        tx->color     = useAssist ? kNowColor : kTextColor;
    }
}

void TutorialHintHudSystem::UpdateGauge(const TutorialState &state, float dt)
{
    // 一気に伸ばすと増えたことが目に入らないので、滑らかに寄せる
    m_gaugeShown += (state.progress - m_gaugeShown) * CCL::Math::Damp(kFillSpeed, dt);

    SetRect(m_fillID, -m_bandWidth * 0.5f, kBarY, m_bandWidth * m_gaugeShown, kBarH);

    if (auto *img = m_world->GetComponent<UIImageComponent>(m_fillID))
    {
        img->color = state.cleared ? kClearColor : kFillColor;
    }
}

void TutorialHintHudSystem::UpdateNowButton(const TutorialState &state, float dt)
{
    // 下の帯まで目を動かさなくても、押すボタンが分かるように中央に出す
    const TutorialStepDef *def  = state.Current();
    const bool             show = state.assistActive && def && def->action;

    const float target = show ? 1.0f : 0.0f;
    m_nowAlpha += (target - m_nowAlpha) * CCL::Math::Damp(kNowFadeSpeed, dt);
    SetAlpha(m_nowIconID, m_nowAlpha);

    if (show)
    {
        if (auto *ic = m_world->GetComponent<InputIconComponent>(m_nowIconID))
        {
            ic->actionHash = CCL::Utils::HashString(def->action);
        }
    }
}

void TutorialHintHudSystem::SetRect(EntityID id, float x, float y, float width, float height)
{
    if (auto *t = m_world->GetComponent<UITransformComponent>(id))
    {
        t->offset = {x, -kBottom + y};
        t->size   = {width, height};
    }
}

void TutorialHintHudSystem::SetAlpha(EntityID id, float alpha)
{
    if (auto *t = m_world->GetComponent<UITransformComponent>(id))
    {
        t->alpha = alpha;
    }
}

float TutorialHintHudSystem::EstimateWidth(const std::string &text, float pixelSize)
{
    float width = 0.0f;
    for (const unsigned char c : text)
    {
        // UTF-8 の2バイト目以降は数えない
        if ((c & 0xC0) == 0x80)
        {
            continue;
        }
        width += (c < 0x80) ? pixelSize * 0.5f : pixelSize;
    }
    return width;
}

// ---------------------------------------------------------------------------
// 生成
// ---------------------------------------------------------------------------

void TutorialHintHudSystem::Build()
{
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "TutorialHintCanvas");

    // 描く順: 帯・ゲージの地(110) → ゲージの中身(111) → アイコン(112) → 文字(113) → 中央のボタン(130)
    m_bandID  = SpawnImage(canvas, "TutorialHintBand", kBandColor, 110);
    m_barID   = SpawnImage(canvas, "TutorialHintBar", kBandColor, 110);
    m_fillID  = SpawnImage(canvas, "TutorialHintFill", kFillColor, 111);
    m_iconID  = SpawnIcon(canvas, "TutorialHintIcon", RowTransform(), 112);
    m_plusID  = SpawnText(canvas, "TutorialHintPlus", "+");
    m_icon2ID = SpawnIcon(canvas, "TutorialHintIcon2", RowTransform(), 112);
    m_textID  = SpawnText(canvas, "TutorialHintText", "");

    // 中央のボタンだけは、画面の中央を基準に置く
    UITransformComponent nowT;
    nowT.anchor = {0.5f, 0.5f};
    nowT.pivot  = {0.5f, 0.5f};
    nowT.offset = {0.0f, kNowOffsetY};
    nowT.size   = {kNowIconSize, kNowIconSize};
    nowT.alpha  = 0.0f;
    m_nowIconID = SpawnIcon(canvas, "TutorialHintNowIcon", nowT, 130);
}

UITransformComponent TutorialHintHudSystem::RowTransform()
{
    UITransformComponent t;
    t.anchor = {0.5f, 1.0f};
    t.pivot  = {0.0f, 0.5f};
    t.offset = {0.0f, -kBottom};
    t.alpha  = 0.0f;
    return t;
}

EntityID TutorialHintHudSystem::SpawnImage(EntityID canvas, const char *name, DirectX::XMFLOAT4 color, int sortOrder)
{
    auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent>();
    auto e    = EntityFactory::SpawnNamed(*m_world, name, arch);

    UIImageComponent img;
    img.color     = color;
    img.sortOrder = sortOrder;

    e.Set(CCL::GameUI::MakeUINode(canvas)).Set(RowTransform()).Set(img);
    return e.ID();
}

EntityID TutorialHintHudSystem::SpawnIcon(EntityID canvas, const char *name, const UITransformComponent &t, int sortOrder)
{
    auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent,
                                          InputIconComponent>();
    auto e    = EntityFactory::SpawnNamed(*m_world, name, arch);

    UIImageComponent img;
    img.sortOrder = sortOrder;
    img.visible   = false; // InputIconSystem が画像を決めるまで出さない

    e.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(img).Set(InputIconComponent{});
    return e.ID();
}

EntityID TutorialHintHudSystem::SpawnText(EntityID canvas, const char *name, const char *text)
{
    auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UITextComponent>();
    auto e    = EntityFactory::SpawnNamed(*m_world, name, arch);

    UITextComponent tx;
    tx.text      = text;
    tx.pixelSize = kTextSize;
    tx.sortOrder = 113;
    tx.shadow    = true;

    e.Set(CCL::GameUI::MakeUINode(canvas)).Set(RowTransform()).Set(tx);
    return e.ID();
}

// 全体登録（REGISTER_*）はせず、TutorialScene が登録する
