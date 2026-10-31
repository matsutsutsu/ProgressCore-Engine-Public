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

namespace
{
    // --- 帯の配置（最初の大きさの 1.4 倍） ---
    constexpr float kBottom    = 200.0f; // 画面の下端から帯の中心まで（px）
    constexpr float kBandH     = 90.0f;  // 帯の高さ（px）
    constexpr float kPadX      = 39.0f;  // 帯の左右の余白（px）
    constexpr float kIconSize  = 64.0f;  // アイコンの一辺（px）。画像（64px）と同じ大きさなので荒れない
    constexpr float kIconGap   = 22.0f;  // アイコン・「＋」・文の間（px）
    constexpr float kTextSize  = 36.0f;  // 文字の高さ（px）。文字は 48px で焼いてあり、それより小さいので荒れない
    constexpr float kEmphSize  = 48.0f;  // 強調のときの文字の高さ（px）。焼いた大きさと同じ
    constexpr float kFadeSpeed = 10.0f;  // フェードの速さ（Damp の speed）

    // --- 帯の中身（進み具合） ---
    constexpr float             kFillRate   = 12.0f;                       // 進み具合へ寄る速さ（Damp の speed）
    constexpr DirectX::XMFLOAT4 kFillColor  = {0.20f, 0.36f, 0.48f, 0.75f}; // 進んでいる途中。地の黒より少しだけ明るい青
    constexpr DirectX::XMFLOAT4 kClearColor = {0.20f, 0.46f, 0.30f, 0.80f}; // 達成の余韻。同じ明るさの緑

    // --- 画面の中央の「今だ」のボタン ---
    constexpr float kNowIconSize  = 128.0f; // 一辺（px）。画像（64px）のちょうど2倍にして、荒れを抑える
    constexpr float kNowOffsetY   = 140.0f; // 画面の中央から下へ（px）。プレイヤーの足元あたりで、ボスと突撃を隠さない
    constexpr float kNowFadeSpeed = 20.0f;  // 出す・消す速さ（Damp の speed）。押す瞬間なので速く
} // namespace

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void TutorialHintHudSystem::Update(float dt)
{
    // 処理フロー:
    // 1. UI を作る（プレイヤーが現れた最初のフレームだけ）
    // 2. 出す文言を決め、フェードを進める
    // 3. 「今だ」の段階なら文字を明滅させる
    // 4. 帯の中身を進み具合まで伸ばす
    // 5. 不透明度を UI へ書く
    // 6. 画面の中央の「今だ」のボタン

    if (!m_world->HasResource<TutorialState>()) return;
    const TutorialState &state = m_world->GetResource<TutorialState>();

    // ---------------------------------------------------------
    // 1. UI を作る
    // ---------------------------------------------------------

    // Initialize で作らないのは、その後のレベルの読み込みで消されるため
    if (!m_built)
    {
        if (!m_world->HasResource<BattleContext>()) return;
        if (!m_world->GetResource<BattleContext>().HasPlayer()) return;

        Build();
        m_built = true;
    }

    // ---------------------------------------------------------
    // 2. 文言とフェード
    // ---------------------------------------------------------

    // 文言が変わったら、いったん消し切ってから差し替える。出したまま書き換えると、
    // 新しい指示に変わったことに気付かず、前の説明を読み終えたつもりで見落とす
    const Hint hint    = SelectHint(state);
    const bool want    = state.showHint;
    const bool changed = (m_currentText != hint.text);
    if (changed && m_alpha < 0.05f) ApplyHint(hint);

    const float target = (want && m_currentText == hint.text) ? 1.0f : 0.0f;
    m_alpha += (target - m_alpha) * CCL::Math::Damp(kFadeSpeed, dt);

    // ---------------------------------------------------------
    // 3. 強調
    // ---------------------------------------------------------

    // 押す瞬間だと分かるよう、文字を明滅させる
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

    // ---------------------------------------------------------
    // 4. 帯の中身
    // ---------------------------------------------------------
    UpdateGauge(state, dt);

    // ---------------------------------------------------------
    // 5. 不透明度
    // ---------------------------------------------------------
    for (EntityID id : {m_bandID, m_fillID, m_iconID, m_textID})
    {
        if (auto *t = m_world->GetComponent<UITransformComponent>(id)) t->alpha = m_alpha;
    }

    // 同時押しでないヒントでは、「＋」と2つ目のアイコンを消しておく
    const float secondAlpha = m_hasSecond ? m_alpha : 0.0f;
    for (EntityID id : {m_plusID, m_icon2ID})
    {
        if (auto *t = m_world->GetComponent<UITransformComponent>(id)) t->alpha = secondAlpha;
    }

    // ---------------------------------------------------------
    // 6. 「今だ」のボタン
    // ---------------------------------------------------------
    UpdateNowButton(state, dt);
}

// ---------------------------------------------------------------------------
// 中身と配置
// ---------------------------------------------------------------------------

TutorialHintHudSystem::Hint TutorialHintHudSystem::SelectHint(const TutorialState &state) const
{
    // 文言とアイコンは TutorialSteps.h の表から引く。ヒントの無い項目と教え終えた後は空にする
    const TutorialStepDef *def = state.Current();
    if (!def || def->text[0] == '\0') return {};

    // 補助スローの「今だ」の段階では、押す瞬間だと分かる文言に差し替えて強調する
    if (state.assistActive && def->assistText) return {def->icon, def->action, nullptr, def->assistText, true};
    return {def->icon, def->action, def->action2, def->text};
}

void TutorialHintHudSystem::ApplyHint(const Hint &hint)
{
    m_currentText = hint.text;
    m_hasSecond   = hint.action2 != nullptr;
    m_gaugeShown  = 0.0f; // 新しい項目は空から伸ばす

    // --- 並べる ---
    // [アイコン] (+ [アイコン2]) [文] の組を、帯の中央に置く
    const float textSize = hint.emphasize ? kEmphSize : kTextSize;
    const float textW    = EstimateWidth(hint.text, textSize);
    const float plusW    = EstimateWidth("+", kTextSize);

    float rowW = kIconSize + kIconGap + textW;
    if (m_hasSecond)
    {
        rowW += plusW + kIconGap + kIconSize + kIconGap;
    }

    float x     = -rowW * 0.5f;
    auto  place = [&](EntityID id, float width, float height) {
        if (auto *t = m_world->GetComponent<UITransformComponent>(id))
        {
            t->offset = {x, -kBottom};
            t->size   = {width, height};
        }
        x += width + kIconGap;
    };

    place(m_iconID, kIconSize, kIconSize);
    if (m_hasSecond)
    {
        place(m_plusID, plusW, kTextSize);
        place(m_icon2ID, kIconSize, kIconSize);
    }
    place(m_textID, textW, textSize);

    // --- 帯 ---
    m_bandWidth = rowW + kPadX * 2.0f;
    if (auto *t = m_world->GetComponent<UITransformComponent>(m_bandID))
    {
        t->size = {m_bandWidth, kBandH};
    }

    // --- 中身 ---

    // 画像は InputIconSystem が種類とアクション名から決める。
    // 空のヒント（文言が空）は消し切った後に書くだけなので、アイコンは前のままでよい
    if (hint.text[0] != '\0')
    {
        if (auto *ic = m_world->GetComponent<InputIconComponent>(m_iconID))
        {
            ic->kind = hint.icon;
            if (hint.action) ic->actionHash = CCL::Utils::HashString(hint.action);
        }
        if (auto *ic = m_world->GetComponent<InputIconComponent>(m_icon2ID); ic && hint.action2)
        {
            ic->actionHash = CCL::Utils::HashString(hint.action2);
        }
    }
    if (auto *tx = m_world->GetComponent<UITextComponent>(m_textID))
    {
        tx->text      = hint.text;
        tx->pixelSize = textSize;
    }
}

void TutorialHintHudSystem::UpdateGauge(const TutorialState &state, float dt)
{
    // 進み具合へ滑らかに寄せる。一気に変えると、増えたことが目に入らない
    m_gaugeShown += (state.progress - m_gaugeShown) * CCL::Math::Damp(kFillRate, dt);

    // 帯の左端から伸ばす。帯は中央揃えなので、左端は -幅/2
    if (auto *t = m_world->GetComponent<UITransformComponent>(m_fillID))
    {
        t->offset = {-m_bandWidth * 0.5f, -kBottom};
        t->size   = {m_bandWidth * m_gaugeShown, kBandH};
    }

    // 達成の余韻の間は緑にして、できたことを伝える
    if (auto *img = m_world->GetComponent<UIImageComponent>(m_fillID))
    {
        img->color = state.cleared ? kClearColor : kFillColor;
    }
}

void TutorialHintHudSystem::UpdateNowButton(const TutorialState &state, float dt)
{
    // 「今だ」の間だけ出す。下の帯の文字まで目を動かさなくても、押すボタンが分かるように
    const TutorialStepDef *def    = state.Current();
    const bool             show   = state.assistActive && def && def->action;
    const float            target = show ? 1.0f : 0.0f;
    m_nowAlpha += (target - m_nowAlpha) * CCL::Math::Damp(kNowFadeSpeed, dt);

    if (show)
    {
        if (auto *ic = m_world->GetComponent<InputIconComponent>(m_nowIconID))
        {
            ic->actionHash = CCL::Utils::HashString(def->action);
        }
    }

    if (auto *t = m_world->GetComponent<UITransformComponent>(m_nowIconID))
    {
        t->alpha = m_nowAlpha;
    }
}

float TutorialHintHudSystem::EstimateWidth(const std::string &text, float pixelSize)
{
    float w = 0.0f;
    for (const unsigned char c : text)
    {
        if ((c & 0xC0) == 0x80) continue;               // UTF-8 の2バイト目以降は数えない
        w += (c < 0x80) ? pixelSize * 0.5f : pixelSize; // 半角は高さの半分、それ以外は全角とみなす
    }
    return w;
}

// ---------------------------------------------------------------------------
// 生成
// ---------------------------------------------------------------------------

void TutorialHintHudSystem::Build()
{
    // 処理フロー:
    // 1. 下地の帯と、その中身（進み具合）を作る
    // 2. アイコン・「＋」・2つ目のアイコンを作る
    // 3. 説明の1行を作る
    // 4. 画面の中央の「今だ」のボタンを作る

    // 【備考】
    //   帯の中身は画面の下辺の中央を基準に置く。位置と幅は ApplyHint が文言に合わせて決める。
    //   左下のゲージと字幕の位置を避けるため、下端から kBottom の高さに置く。

    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "TutorialHintCanvas");

    auto imageArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent>();
    auto iconArch  = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent,
                                               InputIconComponent>();
    auto textArch  = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UITextComponent>();

    // 帯の中身はどれも、下辺の中央を基準に、左端・縦の中央を合わせて置く
    auto rowTransform = [](DirectX::XMFLOAT2 size) {
        UITransformComponent t;
        t.anchor = {0.5f, 1.0f};
        t.pivot  = {0.0f, 0.5f};
        t.offset = {0.0f, -kBottom};
        t.size   = size;
        t.alpha  = 0.0f;
        return t;
    };

    auto spawnIcon = [&](const char *name) {
        auto e = EntityFactory::SpawnNamed(*m_world, name, iconArch);

        UIImageComponent img;
        img.sortOrder = 112;
        img.visible   = false; // InputIconSystem が画像を決めるまで出さない

        InputIconComponent ic;
        ic.kind = InputIconKind::Action;

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(rowTransform({kIconSize, kIconSize})).Set(img).Set(ic);
        return e.ID();
    };

    auto spawnText = [&](const char *name, const char *text) {
        auto e = EntityFactory::SpawnNamed(*m_world, name, textArch);

        UITextComponent tx;
        tx.text      = text;
        tx.pixelSize = kTextSize;
        tx.sortOrder = 113;
        tx.shadow    = true;

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(rowTransform({400.0f, kTextSize})).Set(tx);
        return e.ID();
    };

    // ---------------------------------------------------------
    // 1. 下地の帯と中身
    // ---------------------------------------------------------

    // 明るい床や空の上でも白い文字が読めるよう、暗く敷く
    {
        auto e = EntityFactory::SpawnNamed(*m_world, "TutorialHintBand", imageArch);

        UITransformComponent t = rowTransform({400.0f, kBandH});
        t.pivot                = {0.5f, 0.5f};

        UIImageComponent img;
        img.color       = {0.02f, 0.03f, 0.05f, 0.70f};
        img.useGradient = true;
        img.colorBottom = {0.02f, 0.03f, 0.05f, 0.45f};
        img.sortOrder   = 110; // ゲージ（100〜102）より手前、右上の操作説明（120〜）より奥

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(img);
        m_bandID = e.ID();
    }

    // 帯の上に重ね、進み具合だけ左から伸ばす
    {
        auto e = EntityFactory::SpawnNamed(*m_world, "TutorialHintFill", imageArch);

        UIImageComponent img;
        img.color     = kFillColor;
        img.sortOrder = 111;

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(rowTransform({0.0f, kBandH})).Set(img);
        m_fillID = e.ID();
    }

    // ---------------------------------------------------------
    // 2. アイコン
    // ---------------------------------------------------------
    m_iconID  = spawnIcon("TutorialHintIcon");
    m_plusID  = spawnText("TutorialHintPlus", "+");
    m_icon2ID = spawnIcon("TutorialHintIcon2");

    // ---------------------------------------------------------
    // 3. 説明の1行
    // ---------------------------------------------------------
    m_textID = spawnText("TutorialHintText", "");

    // ---------------------------------------------------------
    // 4. 「今だ」のボタン
    // ---------------------------------------------------------

    // 帯とは別に、画面の中央を基準に置く
    {
        auto e = EntityFactory::SpawnNamed(*m_world, "TutorialHintNowIcon", iconArch);

        UITransformComponent t;
        t.anchor = {0.5f, 0.5f};
        t.pivot  = {0.5f, 0.5f};
        t.offset = {0.0f, kNowOffsetY};
        t.size   = {kNowIconSize, kNowIconSize};
        t.alpha  = 0.0f;

        UIImageComponent img;
        img.sortOrder = 130; // 帯と右上の操作説明より手前
        img.visible   = false;

        InputIconComponent ic;
        ic.kind = InputIconKind::Action;

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(img).Set(ic);
        m_nowIconID = e.ID();
    }
}

// 全体登録（REGISTER_*）はせず、TutorialScene が登録する。ゲーム本編ではヒントを出さないため
