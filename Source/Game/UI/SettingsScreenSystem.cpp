#include "SettingsScreenSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"
#include "Engine/Platform/Input/Input.h"
#include "Engine/Platform/Input/GamePad.h"
#include "Engine/UI/ECS/GameViewportInfo.h"
#include "Engine/UI/ECS/UIDrawList.h"

#include "Game/UI/GameSettings.h"
#include "Game/UI/ScreenHeadingUi.h" // 左上の見出しと線（リザルト・ポーズと共通の組み）

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace CCL::ECS;
using namespace CCL::GameUI;

namespace
{
    // windows.h を持ち込まないための仮想キーコード
    constexpr int VKEY_ESCAPE = 0x1B;
    constexpr int VKEY_LEFT   = 0x25;
    constexpr int VKEY_RIGHT  = 0x27;

    // --- 配置（画面の左上からの px。左端と見出しはリザルト・ポーズとそろえる） ---
    constexpr float kRowsGap   = 130.0f;                           // 見出しの下の線から行の入れ物の上端まで
    constexpr float kRowH      = 80.0f;                            // 行の間隔
    constexpr float kDecX      = ScreenLayout::kLeft + 370.0f;     // < ボタンの中心
    constexpr float kGaugeX    = ScreenLayout::kLeft + 400.0f;     // ゲージの左端
    constexpr float kGaugeW    = 260.0f;                           // ゲージの長さ
    constexpr float kIncX      = kGaugeX + kGaugeW + 30.0f;        // > ボタンの中心
    constexpr float kValueX    = kIncX + 34.0f;                    // 値の文字の左端
    constexpr float kArrowSize = 36.0f;                            // < > ボタンの大きさ

    // --- 描画順（ポーズ・リザルトの UI より手前） ---
    constexpr int kSortDim    = 300;
    constexpr int kSortHead   = 320;
    constexpr int kSortRow    = 330;
    constexpr int kSortValue  = 335;

    constexpr float kFadeTime = 0.2f; // 下の画面と設定画面を入れ替える秒数

    // 刻みに丸める（足し引きを繰り返して 0.30000001 のような半端な値にならないように）
    float Snap(float v, float step) { return std::round(v / step) * step; }
} // namespace

// ---------------------------------------------------------------------------
// 初期化と設定の反映
// ---------------------------------------------------------------------------

void SettingsScreenSystem::Initialize()
{
    if (!m_world->HasResource<SettingsScreenState>())
        m_world->AddResource<SettingsScreenState>();
}

void SettingsScreenSystem::PlaySound(const char *eventName) const
{
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;
    if (auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>())
        audio->PlayOneShot(CCL::Utils::HashString(eventName));
}

void SettingsScreenSystem::ApplySettings()
{
    const GameSettings &s = GameSettings::Instance();

    // カメラ: 感度の倍率と上下反転。カメラは毎回作り直されることがあるので、毎フレーム書く
    m_world->ViewEach<CameraBodyTPSComponent>([&](EntityID id) {
        if (auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(id))
        {
            tps->sensitivityScale = s.lookSensitivity;
            tps->mouseInvertY     = s.invertY;
        }
    });

    // 音声: 変わったときだけ反映する（再生中の全部の音へ掛け直すので、毎フレームはしない）
    if (s.bgmVolume == m_appliedBgm && s.sfxVolume == m_appliedSfx) return;
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;
    if (auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>())
    {
        audio->SetCategoryVolume(AudioCategory::BGM, s.bgmVolume);
        audio->SetCategoryVolume(AudioCategory::SFX, s.sfxVolume);
        m_appliedBgm = s.bgmVolume;
        m_appliedSfx = s.sfxVolume;
    }
}

// ---------------------------------------------------------------------------
// 画面の開閉
// ---------------------------------------------------------------------------

EntityID SettingsScreenSystem::SpawnArrow(EntityID canvas, const char *name, const char *glyph, float x, float y)
{
    auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent,
                                          UIInteractableComponent, UIButtonComponent>();
    auto e    = EntityFactory::SpawnNamed(*m_world, name, arch);

    UITransformComponent t;
    t.anchor = {0.0f, 0.0f};
    t.pivot  = {0.5f, 0.5f};
    t.offset = {x, y};
    t.size   = {kArrowSize, kArrowSize};

    UIImageComponent img;
    img.sortOrder = kSortRow;

    UIInteractableComponent inter;
    inter.focusable = false; // 上下の行の移動に混ざらないよう、マウスで押すだけにする

    UIButtonComponent btn;
    btn.normalColor   = {0.0f, 0.0f, 0.02f, 0.4f};
    btn.hoverColor    = {0.96f, 0.88f, 0.62f, 0.35f}; // 乗せると白金がにじむ
    btn.pressedColor  = {0.96f, 0.88f, 0.62f, 0.6f};
    btn.disabledColor = btn.normalColor;

    e.Set(MakeUINode(canvas)).Set(t).Set(img).Set(inter).Set(btn);
    m_overlay.push_back(e.ID());

    // 記号（等幅のフォントなので、文字の幅の半分だけ左へずらして中央に置く）
    constexpr float kGlyphPx = 24.0f;
    m_overlay.push_back(SpawnTextTopLeft(*m_world, (std::string(name) + "Glyph").c_str(), glyph, x - kGlyphPx * 0.25f,
                                         y - kGlyphPx * 0.5f, kGlyphPx, ScreenColor::kValue, canvas, kSortValue));
    return e.ID();
}

void SettingsScreenSystem::Open(EntityID opener)
{
    World &w = *m_world;
    m_open   = true;
    m_opener = opener;
    w.GetResource<SettingsScreenState>().open = true;

    // 下の画面のボタンを操作できなくする（開いている間に、裏のボタンへ選択が移ったり押せたりしないように）
    m_disabled.clear();
    w.ViewEach<UIInteractableComponent>([&](EntityID id) {
        auto *inter = w.GetComponent<UIInteractableComponent>(id);
        if (inter && inter->interactable)
        {
            inter->interactable = false;
            m_disabled.push_back(id);
        }
    });

    // --- 画面を作る（すべて Canvas の子） ---
    m_overlay.clear();
    const EntityID canvas = SpawnCanvas(w, "SettingsCanvas");
    m_overlay.push_back(canvas);

    // 下の画面（ポーズ・タイトル・HUD）の UI を薄くしながら設定画面を出す（設定画面が出そろう次のフレームから）
    m_fadeStarted         = false;
    m_closing             = false;
    m_restoreBelowPending = false;
    m_fade                = 0.0f;

    // 暗転。下の画面の暗転も隠れるので、ポーズ画面と同じ薄さにする
    static const DirectX::XMFLOAT4 kDimBottom = {0.02f, 0.02f, 0.05f, 0.82f};
    m_overlay.push_back(SpawnPanel(w, "SettingsDim", {0.0f, 0.0f}, {4000.0f, 4000.0f}, {0.0f, 0.0f, 0.0f, 0.5f},
                                   kSortDim, -1.0f, 0.0f, canvas, &kDimBottom));

    SpawnScreenHeading(w, canvas, "SETTINGS", ScreenColor::kGold, m_overlay, kSortHead);
    {
        // 英語の見出しに、日本語を小さく添える（打ち込みの演出は他の画面と同じ）
        UIRevealComponent rv;
        rv.mode  = UIRevealMode::Type;
        rv.delay = 0.2f;
        rv.speed = ScreenLayout::kTypeSpeed;
        rv.text  = "設定";
        m_overlay.push_back(SpawnTextTopLeft(w, "SettingsSub", "", ScreenLayout::kLeft, ScreenLayout::kSubY, 20.0f,
                                             ScreenColor::kSub, canvas, kSortHead, 1.0f, &rv));
    }

    // 行の入れ物（ボタンは入れ物の中央からの位置で置く）
    const float    rowsTop = ScreenLayout::kRuleY + kRowsGap;
    const float    boxH    = kRowH * static_cast<float>(RowCount);
    const EntityID box     = SpawnBox(w, "SettingsRows", {0.0f, 0.0f}, {ScreenLayout::kLeft, rowsTop}, {320.0f, boxH}, canvas);
    m_overlay.push_back(box);

    const char *labels[RowCount] = {"視点の感度", "上下反転", "BGMの音量", "効果音の音量", "戻る"};
    for (int i = 0; i < RowCount; ++i)
    {
        char btnName[32], lblName[32];
        std::snprintf(btnName, sizeof(btnName), "SettingsRow%d", i);
        std::snprintf(lblName, sizeof(lblName), "SettingsRowLabel%d", i);

        const float offsetY = -boxH * 0.5f + kRowH * (static_cast<float>(i) + 0.5f);
        const auto  ids     = SpawnButton(w, btnName, lblName, labels[i], {0.0f, offsetY},
                                          i == RowBack ? "settings_back" : "settings_row", i == 0, kSortRow, -1.0f, box);
        m_rows[i] = ids.button;
        m_overlay.push_back(ids.button);
        m_overlay.push_back(ids.label);
        if (i == RowBack) break;

        // 値の部品（< ゲージ > 値）。画面の左上を基準に、行の中心の高さへ置く
        const float y = rowsTop + kRowH * (static_cast<float>(i) + 0.5f);
        char        name[32];
        std::snprintf(name, sizeof(name), "SettingsDec%d", i);
        m_dec[i] = SpawnArrow(canvas, name, "<", kDecX, y);
        std::snprintf(name, sizeof(name), "SettingsInc%d", i);
        m_inc[i] = SpawnArrow(canvas, name, ">", kIncX, y);

        m_gauge[i] = InvalidEntityID;
        if (i != RowInvertY)
        {
            auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIProgressBarComponent>();
            std::snprintf(name, sizeof(name), "SettingsGauge%d", i);
            auto e = EntityFactory::SpawnNamed(w, name, arch);

            UITransformComponent t;
            t.anchor = {0.0f, 0.0f};
            t.pivot  = {0.0f, 0.5f};
            t.offset = {kGaugeX, y};
            t.size   = {kGaugeW, 12.0f};

            UIProgressBarComponent bar; // HP ゲージと同じ斜めの細いゲージ
            bar.sortOrder = kSortValue;
            bar.bgColor   = {0.0f, 0.0f, 0.02f, 0.6f};
            bar.fillColor = ScreenColor::kGold;
            bar.gradient  = 0.25f;
            bar.slant     = 8.0f;
            bar.frame     = 1.5f;
            bar.frameColor = {0.96f, 0.88f, 0.62f, 0.35f};
            bar.value      = ValueRatio(i); // 生成直後は GetComponent で引けないので、初めの値はここで入れる

            e.Set(MakeUINode(canvas)).Set(t).Set(bar);
            m_gauge[i] = e.ID();
            m_overlay.push_back(e.ID());
        }

        // 値の文字。上下反転の行はゲージの代わりに、< > の間に ON / OFF を出す
        std::snprintf(name, sizeof(name), "SettingsValue%d", i);
        const float textX = (i == RowInvertY) ? kGaugeX + kGaugeW * 0.5f - 24.0f : kValueX;
        m_valueText[i]    = SpawnTextTopLeft(w, name, ValueText(i).c_str(), textX, y - 12.0f, 24.0f, ScreenColor::kValue, canvas, kSortValue);
        m_overlay.push_back(m_valueText[i]);
    }

    // 操作のヒント（決定・選択）
    SpawnMenuHints(w, box, boxH * 0.5f + 40.0f, m_overlay, 340, -1.0f);
}

void SettingsScreenSystem::Close()
{
    World &w = *m_world;

    // Destroy は印を付けるだけで、実際に消すのはロジックの最後の HierarchyCleanupSystem。
    //   ポーズ中はロジックが止まっていて消えないので、ここでは破棄を直接予約する。
    //   作った部品は m_overlay に全部入っているので、親子をたどらなくても取りこぼさない
    for (EntityID id : m_overlay)
        if (w.IsEntityValid(id)) w.RequestDestroyEntity(id);
    m_overlay.clear();

    // 境目を外す（設定画面が消える次のフレームで。それまでは設定画面を描かないまま）
    m_restoreBelowPending = true;
    m_fadeStarted         = false;
    m_closing             = false;

    // 下の画面のボタンを元に戻し、開いたボタンへ選択を戻す
    for (EntityID id : m_disabled)
        if (auto *inter = w.IsEntityValid(id) ? w.GetComponent<UIInteractableComponent>(id) : nullptr)
            inter->interactable = true;
    m_disabled.clear();
    if (auto *inter = w.IsEntityValid(m_opener) ? w.GetComponent<UIInteractableComponent>(m_opener) : nullptr)
        inter->isFocused = true;

    GameSettings::Instance().Save();

    m_open          = false;
    m_closePending  = false;
    m_closeByCancel = false;
    w.GetResource<SettingsScreenState>().open = false;
}

// ---------------------------------------------------------------------------
// 値の調整と表示
// ---------------------------------------------------------------------------

void SettingsScreenSystem::Adjust(int row, int dir)
{
    GameSettings &s = GameSettings::Instance();
    switch (row)
    {
    case RowSensitivity:
        s.lookSensitivity = std::clamp(Snap(s.lookSensitivity + GameSettings::kSensitivityStep * dir, GameSettings::kSensitivityStep),
                                       GameSettings::kSensitivityMin, GameSettings::kSensitivityMax);
        break;
    case RowInvertY:
        s.invertY = !s.invertY; // 2択なので、左右どちらでも切り替える
        break;
    case RowBgm:
        s.bgmVolume = std::clamp(Snap(s.bgmVolume + GameSettings::kVolumeStep * dir, GameSettings::kVolumeStep), 0.0f, 1.0f);
        break;
    case RowSfx:
        s.sfxVolume = std::clamp(Snap(s.sfxVolume + GameSettings::kVolumeStep * dir, GameSettings::kVolumeStep), 0.0f, 1.0f);
        break;
    default:
        return;
    }
    ApplySettings(); // 効果音の音量を変えたときに、次の音が新しい音量で鳴るよう先に反映する
    PlaySound("sfx:/ui_move");
    RefreshValues();
}

void SettingsScreenSystem::RefreshValues()
{
    for (int i = 0; i < kValueRows; ++i)
    {
        if (auto *bar = m_world->GetComponent<UIProgressBarComponent>(m_gauge[i])) bar->value = ValueRatio(i);
        if (auto *tx = m_world->GetComponent<UITextComponent>(m_valueText[i])) tx->text = ValueText(i);
    }
}

float SettingsScreenSystem::ValueRatio(int row) const
{
    const GameSettings &s = GameSettings::Instance();
    switch (row)
    {
    case RowSensitivity:
        return (s.lookSensitivity - GameSettings::kSensitivityMin) / (GameSettings::kSensitivityMax - GameSettings::kSensitivityMin);
    case RowBgm: return s.bgmVolume;
    case RowSfx: return s.sfxVolume;
    default: return 0.0f; // 上下反転はゲージを持たない
    }
}

std::string SettingsScreenSystem::ValueText(int row) const
{
    const GameSettings &s = GameSettings::Instance();
    char                buf[16] = {};
    switch (row)
    {
    case RowSensitivity: std::snprintf(buf, sizeof(buf), "%.2fx", s.lookSensitivity); break;
    case RowInvertY: return s.invertY ? "オン" : "オフ";
    case RowBgm: std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(s.bgmVolume * 100.0f))); break;
    case RowSfx: std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(s.sfxVolume * 100.0f))); break;
    default: break;
    }
    return buf;
}

// ---------------------------------------------------------------------------
// 毎フレーム
// ---------------------------------------------------------------------------

void SettingsScreenSystem::UpdateFade(float dt)
{
    if (!m_world->HasResource<UIDrawList>()) return;
    UIDrawList &list = m_world->GetResource<UIDrawList>();

    // 閉じた次のフレーム: 設定画面は消えたので、下の画面を元どおり描く
    if (m_restoreBelowPending)
    {
        list.splitSortOrder   = INT_MIN;
        list.belowAlpha       = 1.0f;
        list.aboveAlpha       = 1.0f;
        m_restoreBelowPending = false;
    }
    if (!m_open) return;

    // 開いた次のフレーム: 設定画面が出そろったので、入れ替えを始める
    if (!m_fadeStarted)
    {
        m_fadeStarted = true;
        m_fade        = 0.0f;
    }
    else
    {
        const float step = dt / kFadeTime;
        m_fade           = m_closing ? std::max(m_fade - step, 0.0f) : std::min(m_fade + step, 1.0f);
    }

    const float s       = m_fade * m_fade * (3.0f - 2.0f * m_fade); // 動き始めと終わりをゆるやかに
    list.splitSortOrder = kSortDim;
    list.aboveAlpha     = s;
    list.belowAlpha     = 1.0f - s;
}

void SettingsScreenSystem::Update(float dt)
{
    ApplySettings();

    World &w = *m_world;

    UpdateFade(dt);

    // --- 閉じている: SETTINGS のボタンが押されたら開く ---
    if (!m_open)
    {
        EntityID opener = InvalidEntityID;
        ForEachWithID([&](EntityID id, const UIInteractableComponent &inter, const UIButtonComponent &btn) {
            if (inter.clicked && btn.onClickAction == "open_settings") opener = id;
        });
        if (opener != InvalidEntityID) Open(opener);
        return;
    }

    // 閉じる要求を受けたら、下の画面へ入れ替えてから閉じる。
    //   次のフレームから始めるのは、同じフレームの Esc でポーズまで閉じないように
    if (m_closePending && !m_closing)
    {
        m_closing = true;
        // 押して閉じたときは UISoundSystem が「決定」を鳴らすので、Esc・B で閉じたときだけ「閉じる」を鳴らす
        if (m_closeByCancel) PlaySound("sfx:/ui_close");
    }
    if (m_closing)
    {
        if (m_fade <= 0.0f) Close();
        return; // 入れ替えの途中は操作を受けない
    }

    // --- 入力 ---
    Input *input = w.HasResource<Input *>() ? w.GetResource<Input *>() : nullptr;
    if (!input) return;
    const bool keyUsable = !w.HasResource<GameViewportInfo>() || w.GetResource<GameViewportInfo>().KeyboardToGame();
    auto       key       = [&](int vk) { return keyUsable && input->GetKeyboard().IsTriggered(vk); };
    GamePad   &gp        = input->GetGamePad();

    const int dir = (key(VKEY_RIGHT) || gp.IsTriggered(GamePad::BTN_RIGHT)) ? 1
                  : (key(VKEY_LEFT) || gp.IsTriggered(GamePad::BTN_LEFT)) ? -1
                                                                           : 0;
    if (key(VKEY_ESCAPE) || gp.IsTriggered(GamePad::BTN_B))
    {
        m_closePending  = true;
        m_closeByCancel = true;
        return;
    }

    // 選んでいる行（マウスが乗っている行を優先）と、押された行
    for (int i = 0; i < RowCount; ++i)
    {
        const auto *inter = w.GetComponent<UIInteractableComponent>(m_rows[i]);
        if (!inter) continue;

        if (inter->clicked)
        {
            if (i == RowBack)
                m_closePending = true;
            else if (i == RowInvertY)
                Adjust(i, 1); // 決定で ON / OFF を切り替える
        }
        if (dir != 0 && inter->isFocused && i != RowBack) Adjust(i, dir);
    }

    // < > ボタン（マウス）
    for (int i = 0; i < kValueRows; ++i)
    {
        if (const auto *dec = w.GetComponent<UIInteractableComponent>(m_dec[i]); dec && dec->clicked) Adjust(i, -1);
        if (const auto *inc = w.GetComponent<UIInteractableComponent>(m_inc[i]); inc && inc->clicked) Adjust(i, 1);
    }
}

REGISTER_RENDER_SYSTEM(SettingsScreenSystem, Priority::RenderStage::R10_UI);
