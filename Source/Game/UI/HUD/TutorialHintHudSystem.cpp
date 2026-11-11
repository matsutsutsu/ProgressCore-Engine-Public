#include "Game/UI/HUD/TutorialHintHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Math/Interpolation.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Serialization/Factory/Prefab.h"

#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/Input/InputIconComponent.h"
#include "Game/UI/Common/UiPrefabUtil.h"
#include "Game/Logics/AI/Battle/BattleContext.h"

using namespace CCL::ECS;

namespace
{
    // --- プレハブ ---
    constexpr const char *kPrefabPath = "Assets/Prefabs/UI/TutorialHint.json";
} // namespace



// ---------------------------------------------------------------------------
// 更新
// ---------------------------------------------------------------------------

void TutorialHintHudSystem::Update(float dt)
{
    // 処理フロー:
    //   1. プレイヤーが現れたフレームに、TutorialHint プレハブを生成する
    //   2. 今の項目のアイコンと文言を書き、帯を並べ直す
    //   3. 帯全体をフェードさせる
    //   4. ゲージと中央のボタンを進める

    if (!m_world->HasResource<TutorialState>()) return;

    // ---------------------------------------------------------
    // 1. 生成
    // ---------------------------------------------------------

    // Initialize で作らないのは、その後のレベルの読み込みで消されるため
    if (!m_spawned)
    {
        if (!m_world->HasResource<BattleContext>()) return;
        if (!m_world->GetResource<BattleContext>().HasPlayer()) return;

        SpawnHud();
    }
    if (m_rootID == InvalidEntityID) return;

    // プレハブのルートにスタイルがなければ、コンポーネントの既定値で動かす
    static const TutorialHintStyleComponent kDefaultStyle{};
    const auto *stylePtr = m_world->GetComponent<TutorialHintStyleComponent>(m_rootID);
    const auto &style    = stylePtr ? *stylePtr : kDefaultStyle;

    const TutorialState &state = m_world->GetResource<TutorialState>();

    // ---------------------------------------------------------
    // 2. 文言と並べ方
    // ---------------------------------------------------------
    const TutorialStepDef *def = state.Current();
    if (def)
    {
        // 項目が変わったらフェードインし直す
        if (state.step != m_lastStep)
        {
            m_alpha      = 0.0f;
            m_gaugeShown = 0.0f;
            m_lastStep   = state.step;
        }

        // 並べ直しは毎フレーム行う
        // インスペクタで Style の寸法を変えたとき、その場で帯に反映されるように（数個の値を書くだけで軽い）
        ApplyHint(*def, state.assistActive, style);
    }

    // ---------------------------------------------------------
    // 3. フェード
    // ---------------------------------------------------------
    const float target = state.showHint ? 1.0f : 0.0f;
    m_alpha += (target - m_alpha) * CCL::Math::Damp(style.fadeSpeed, dt);

    SetAlpha(m_bandID, m_alpha);
    SetAlpha(m_barID, m_alpha);
    SetAlpha(m_fillID, m_alpha);
    SetAlpha(m_iconID, m_alpha);
    SetAlpha(m_textID, m_alpha);
    SetAlpha(m_plusID, m_hasSecond ? m_alpha : 0.0f);
    SetAlpha(m_icon2ID, m_hasSecond ? m_alpha : 0.0f);

    // ---------------------------------------------------------
    // 4. ゲージと中央のボタン
    // ---------------------------------------------------------
    UpdateGauge(state, style, dt);
    UpdateNowButton(state, style, dt);
}



void TutorialHintHudSystem::ApplyHint(const TutorialStepDef &def, bool assist, const TutorialHintStyleComponent &style)
{
    const bool        useAssist = assist && def.assistText;
    const std::string text      = useAssist ? def.assistText : def.text;
    const float       textSize  = useAssist ? style.emphSize : style.textSize;

    // 「今だ」は1つ目のボタンだけを出す
    m_hasSecond = def.action2 && !useAssist;

    // --- 幅 ---
    // [アイコン] (+ [アイコン2]) [文] の組の幅を求め、帯の中央に置く
    const float textW = EstimateWidth(text, textSize);
    const float plusW = EstimateWidth("+", style.textSize);

    float rowW = style.iconSize + style.gap + textW;
    if (m_hasSecond)
    {
        rowW += plusW + style.gap + style.iconSize + style.gap;
    }
    m_bandWidth = rowW + style.padX * 2.0f;

    // --- 並べる ---
    float x = -rowW * 0.5f;

    SetRect(m_iconID, x, 0.0f, style.iconSize, style.iconSize);
    x += style.iconSize + style.gap;

    if (m_hasSecond)
    {
        SetRect(m_plusID, x, 0.0f, plusW, style.textSize);
        x += plusW + style.gap;

        SetRect(m_icon2ID, x, 0.0f, style.iconSize, style.iconSize);
        x += style.iconSize + style.gap;
    }

    // ゲージは帯の下端にすき間なく付ける
    const float barY = style.bandHeight * 0.5f + style.barHeight * 0.5f;

    SetRect(m_textID, x, 0.0f, textW, textSize);
    SetRect(m_bandID, -m_bandWidth * 0.5f, 0.0f, m_bandWidth, style.bandHeight);
    SetRect(m_barID, -m_bandWidth * 0.5f, barY, m_bandWidth, style.barHeight);

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
        tx->color     = useAssist ? style.nowColor : style.textColor;
    }
    if (auto *tx = m_world->GetComponent<UITextComponent>(m_plusID))
    {
        tx->pixelSize = style.textSize;
    }
}



void TutorialHintHudSystem::UpdateGauge(const TutorialState &state, const TutorialHintStyleComponent &style, float dt)
{
    // 一気に伸ばすと増えたことが目に入らないので、滑らかに寄せる
    m_gaugeShown += (state.progress - m_gaugeShown) * CCL::Math::Damp(style.fillSpeed, dt);

    const float barY = style.bandHeight * 0.5f + style.barHeight * 0.5f;
    SetRect(m_fillID, -m_bandWidth * 0.5f, barY, m_bandWidth * m_gaugeShown, style.barHeight);

    if (auto *img = m_world->GetComponent<UIImageComponent>(m_fillID))
    {
        img->color = state.cleared ? style.clearColor : style.fillColor;
    }
}



void TutorialHintHudSystem::UpdateNowButton(const TutorialState &state, const TutorialHintStyleComponent &style, float dt)
{
    // 下の帯まで目を動かさなくても、押すボタンが分かるように中央に出す
    const TutorialStepDef *def  = state.Current();
    const bool             show = state.assistActive && def && def->action;

    const float target = show ? 1.0f : 0.0f;
    m_nowAlpha += (target - m_nowAlpha) * CCL::Math::Damp(style.nowFadeSpeed, dt);
    SetAlpha(m_nowIconID, m_nowAlpha);

    if (show)
    {
        if (auto *ic = m_world->GetComponent<InputIconComponent>(m_nowIconID))
        {
            ic->actionHash = CCL::Utils::HashString(def->action);
        }
    }
}



// ---------------------------------------------------------------------------
// 部品の操作
// ---------------------------------------------------------------------------

void TutorialHintHudSystem::SetRect(EntityID id, float x, float y, float width, float height)
{
    auto *t = m_world->GetComponent<UITransformComponent>(id);
    if (!t) return;

    t->offset = {x, y};
    t->size   = {width, height};
}



void TutorialHintHudSystem::SetAlpha(EntityID id, float alpha)
{
    if (auto *t = m_world->GetComponent<UITransformComponent>(id)) t->alpha = alpha;
}



float TutorialHintHudSystem::EstimateWidth(const std::string &text, float pixelSize)
{
    float width = 0.0f;
    for (const unsigned char c : text)
    {
        // UTF-8 の2バイト目以降は数えない
        if ((c & 0xC0) == 0x80) continue;

        width += (c < 0x80) ? pixelSize * 0.5f : pixelSize;
    }
    return width;
}



// ---------------------------------------------------------------------------
// 生成
// ---------------------------------------------------------------------------

void TutorialHintHudSystem::SpawnHud()
{
    // 失敗しても試すのは1回だけにする
    // プレハブが壊れているとき、毎フレーム読み直してエラーログが流れ続けないように
    m_spawned = true;

    m_rootID = Prefab::SpawnPrefab(*m_world, kPrefabPath);
    if (m_rootID == InvalidEntityID) return; // 読めない理由は Prefab がログに出す

    // エディタで子の名前を変えると引けなくなる
    // 黙って部品が消えると原因が分からないので、見つからなければログに出す
    const auto find = [this](const char *name) {
        const EntityID id = CCL::GameUI::FindChildByName(*m_world, m_rootID, name);
        if (id == InvalidEntityID)
        {
            CCL_LOG_ERROR(LogCategory::Game, "[TutorialHint] %s に '%s' が見つかりません", kPrefabPath, name);
        }
        return id;
    };

    m_bandID    = find("TutorialHintBand");
    m_barID     = find("TutorialHintBar");
    m_fillID    = find("TutorialHintFill");
    m_iconID    = find("TutorialHintIcon");
    m_plusID    = find("TutorialHintPlus");
    m_icon2ID   = find("TutorialHintIcon2");
    m_textID    = find("TutorialHintText");
    m_nowIconID = find("TutorialHintNowIcon");
}



// 全体登録（REGISTER_*）はせず、TutorialScene が登録する
