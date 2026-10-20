#include "Game/UI/HUD/TutorialHintHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/Core/Math/Interpolation.h" // Damp: フレームレートに依らない追従の係数
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/Transform/Components/TransformComponent.h" // UI の親子関係に使う
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/Input/InputIconComponent.h"
#include "Game/UI/Common/MenuUiFactory.h"        // SpawnCanvas / MakeUINode
#include "Game/Logics/AI/Battle/BattleContext.h" // プレイヤーの有無で生成を決める

#include <cmath>
#include <initializer_list>

using namespace CCL::ECS;

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void TutorialHintHudSystem::Update(float dt)
{
    if (!m_world->HasResource<TutorialState>()) return;
    const TutorialState &state = m_world->GetResource<TutorialState>();

    // --- 作る: プレイヤーが現れてから ---
    if (!m_built)
    {
        if (!m_world->HasResource<BattleContext>()) return;
        if (!m_world->GetResource<BattleContext>().HasPlayer()) return;

        Build();
        m_built = true;
    }

    // --- 出す文言を決める ---
    // 文言が変わったら、いったん消し切ってから差し替える。出したまま書き換えると、
    // 新しい指示に変わったことに気付かず、前の説明を読み終えたつもりで見落とす
    const Hint hint    = SelectHint(state);
    const bool want    = state.showHint;
    const bool changed = (m_currentText != hint.text);
    if (changed && m_alpha < 0.05f) ApplyHint(hint);

    const float target = (want && m_currentText == hint.text) ? 1.0f : 0.0f;
    m_alpha += (target - m_alpha) * CCL::Math::Damp(kFadeSpeed, dt);

    // --- 強調: 補助スロー中は文字を明滅させ、押す瞬間だと分かるようにする ---
    m_pulse += dt;
    if (auto *tx = m_world->GetComponent<UITextComponent>(m_textID))
    {
        if (hint.emphasize)
        {
            const float k = 0.5f + 0.5f * std::sin(m_pulse * 14.0f);
            tx->color     = {1.0f, 0.78f + 0.22f * k, 0.25f + 0.35f * k, 1.0f};
        }
        else
        {
            tx->color = {1.0f, 1.0f, 1.0f, 1.0f};
        }
    }

    for (EntityID id : {m_bandID, m_iconID, m_textID})
    {
        if (auto *t = m_world->GetComponent<UITransformComponent>(id)) t->alpha = m_alpha;
    }
}

// ---------------------------------------------------------------------------
// 中身と配置
// ---------------------------------------------------------------------------

TutorialHintHudSystem::Hint TutorialHintHudSystem::SelectHint(const TutorialState &state) const
{
    // 文言とアイコンは TutorialSteps.h の表から引く。ヒントの無い項目と教え終えた後は空にする
    const TutorialStepDef *def = state.Current();
    if (!def || !def->action) return {};

    // 補助スロー中は、押す瞬間だと分かる文言に差し替えて強調する
    if (state.assistActive && def->assistText) return {def->action, def->assistText, true};
    return {def->action, def->text};
}

void TutorialHintHudSystem::ApplyHint(const Hint &hint)
{
    m_currentText = hint.text;

    const float textSize = hint.emphasize ? kEmphSize : kTextSize;
    const float textW    = EstimateWidth(hint.text, textSize);
    const float groupW   = kIconSize + kIconGap + textW; // アイコンと文の組の幅。これを中央に置く
    const float left     = -groupW * 0.5f;

    // 3つとも画面の下辺の中央を基準にし、帯の中心の高さに縦の中央をそろえる
    if (auto *t = m_world->GetComponent<UITransformComponent>(m_bandID))
    {
        t->size = {groupW + kPadX * 2.0f, kBandH};
    }
    if (auto *t = m_world->GetComponent<UITransformComponent>(m_iconID))
    {
        t->offset = {left, -kBottom};
    }
    if (auto *t = m_world->GetComponent<UITransformComponent>(m_textID))
    {
        t->offset = {left + kIconSize + kIconGap, -kBottom};
        t->size   = {textW, textSize};
    }

    // 空のヒント（action が nullptr）は消し切った後に書くだけなので、アイコンは前のままでよい
    if (auto *ic = m_world->GetComponent<InputIconComponent>(m_iconID); ic && hint.action)
    {
        ic->actionHash = CCL::Utils::HashString(hint.action);
    }
    if (auto *tx = m_world->GetComponent<UITextComponent>(m_textID))
    {
        tx->text      = hint.text;
        tx->pixelSize = textSize;
    }
}

float TutorialHintHudSystem::EstimateWidth(const std::string &text, float pixelSize)
{
    float w = 0.0f;
    for (const unsigned char c : text)
    {
        if ((c & 0xC0) == 0x80) continue;           // UTF-8 の2バイト目以降は数えない
        w += (c < 0x80) ? pixelSize * 0.5f : pixelSize; // 半角は高さの半分、それ以外は全角とみなす
    }
    return w;
}

void TutorialHintHudSystem::Build()
{
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "TutorialHintCanvas");

    // --- 下地の帯: 明るい床や空の上でも白い文字が読めるよう、暗く敷く ---
    {
        auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent>();
        auto e    = EntityFactory::SpawnNamed(*m_world, "TutorialHintBand", arch);

        UITransformComponent t;
        t.anchor = {0.5f, 1.0f};
        t.pivot  = {0.5f, 0.5f};
        t.offset = {0.0f, -kBottom};
        t.size   = {400.0f, kBandH};
        t.alpha  = 0.0f;

        UIImageComponent img;
        img.color       = {0.02f, 0.03f, 0.05f, 0.70f};
        img.useGradient = true;
        img.colorBottom = {0.02f, 0.03f, 0.05f, 0.45f};
        img.sortOrder   = 110; // ゲージ（100〜102）より手前、右上の操作説明（120〜）より奥

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(img);
        m_bandID = e.ID();
    }

    // --- アイコン: 画像は InputIconSystem が割り当てから決める ---
    {
        auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent,
                                              InputIconComponent>();
        auto e    = EntityFactory::SpawnNamed(*m_world, "TutorialHintIcon", arch);

        UITransformComponent t;
        t.anchor = {0.5f, 1.0f};
        t.pivot  = {0.0f, 0.5f};
        t.offset = {0.0f, -kBottom};
        t.size   = {kIconSize, kIconSize};
        t.alpha  = 0.0f;

        UIImageComponent img;
        img.sortOrder = 112;
        img.visible   = false; // InputIconSystem が画像を決めるまで出さない

        InputIconComponent ic;
        ic.kind = InputIconKind::Action;

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(img).Set(ic);
        m_iconID = e.ID();
    }

    // --- 説明の1行 ---
    {
        auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UITextComponent>();
        auto e    = EntityFactory::SpawnNamed(*m_world, "TutorialHintText", arch);

        UITransformComponent t;
        t.anchor = {0.5f, 1.0f};
        t.pivot  = {0.0f, 0.5f};
        t.offset = {0.0f, -kBottom};
        t.size   = {400.0f, kTextSize};
        t.alpha  = 0.0f;

        UITextComponent tx;
        tx.text      = "";
        tx.pixelSize = kTextSize;
        tx.sortOrder = 113;
        tx.shadow    = true;

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(tx);
        m_textID = e.ID();
    }
}

// 全体登録（REGISTER_*）はせず、TutorialScene が登録する。ゲーム本編ではヒントを出さないため
