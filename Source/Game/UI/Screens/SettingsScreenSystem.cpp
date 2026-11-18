#include "Game/UI/Screens/SettingsScreenSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Game/Logics/Camera/CameraBodyActionTPSComponent.h"
#include "Engine/Platform/Input/Input.h"
#include "Engine/Platform/Input/GamePad.h"
#include "Engine/UI/ECS/GameViewportInfo.h"
#include "Engine/UI/ECS/UIDrawList.h"

#include "Game/Core/GameSettings.h"
#include "Game/UI/Common/ScreenHeadingUi.h"

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

    // --- 配置（画面の左上からの px。左端と見出しは MenuStyle にそろえる） ---
    constexpr float kRowsGap     = 130.0f; // 見出しの下の線から、行の入れ物の上端までの間隔
    constexpr float kRowH        = 80.0f;  // 行の間隔
    constexpr float kDecOffset   = 370.0f; // 左端（MenuStyle の left）から < ボタンの中心まで
    constexpr float kGaugeOffset = 400.0f; // 左端（MenuStyle の left）からゲージの左端まで
    constexpr float kGaugeW      = 260.0f; // ゲージの長さ
    constexpr float kIncGap      = 30.0f;  // ゲージの右端から > ボタンの中心まで
    constexpr float kValueGap    = 34.0f;  // > ボタンの中心から値の文字の左端まで
    constexpr float kArrowSize   = 36.0f;  // < > ボタンの大きさ

    // --- 描画順（ポーズ・リザルトの UI より手前） ---
    constexpr int kSortDim   = 300; // 暗転。下の画面との境目にもなる
    constexpr int kSortHead  = 320; // 見出し
    constexpr int kSortRow   = 330; // 行のボタン・< > ボタン
    constexpr int kSortValue = 335; // ゲージ・値の文字・< > の記号

    constexpr float kFadeTime = 0.2f; // 下の画面と設定画面を入れ替える時間（秒）

    // 効果音全体の音量の基準。設定画面の値（0〜1）にこれを掛けてから音声へ渡す
    // BGM はプレハブの volume で下げているので、効果音だけここで下げる。全体が大きい・小さいときはこの値を変える
    constexpr float kSfxMixGain = 0.3f;

    // 値を刻み（step）の倍数に丸める
    // 足し引きを繰り返して、0.30000001 のような半端な値にならないようにする
    float Snap(float v, float step) { return std::round(v / step) * step; }
}



// ---------------------------------------------------------------------------
// 初期化と設定の反映
// ---------------------------------------------------------------------------

void SettingsScreenSystem::Initialize()
{
    if (!m_world->HasResource<SettingsScreenState>()) m_world->AddResource<SettingsScreenState>();
}



void SettingsScreenSystem::PlaySound(const char *eventName) const
{
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;

    if (auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>())
    {
        audio->PlayOneShot(CCL::Utils::HashString(eventName));
    }
}



void SettingsScreenSystem::ApplySettings()
{
    const GameSettings &s = GameSettings::Instance();

    // --- カメラ ---
    // カメラは作り直されることがあるので、感度の倍率は毎フレーム書く
    m_world->ViewEach<CameraBodyTPSComponent>([&](EntityID id) {
        if (auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(id)) tps->sensitivityScale = s.lookSensitivity;
    });

    // --- 音声 ---
    // 音量は変わったときだけ反映する
    // 反映すると再生中のすべての音へ掛け直すので、毎フレームはしない
    if (s.bgmVolume == m_appliedBgm && s.sfxVolume == m_appliedSfx) return;
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;

    if (auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>())
    {
        audio->SetCategoryVolume(AudioCategory::BGM, s.bgmVolume);
        audio->SetCategoryVolume(AudioCategory::SFX, s.sfxVolume * kSfxMixGain);
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

    // 上下の行の移動に混ざらないよう、フォーカスは受けず、マウスで押すだけにする
    UIInteractableComponent inter;
    inter.focusable = false;

    UIButtonComponent btn;
    btn.normalColor   = {0.0f, 0.0f, 0.02f, 0.4f};
    btn.hoverColor    = {0.96f, 0.88f, 0.62f, 0.35f}; // 乗せると白金がにじむ
    btn.pressedColor  = {0.96f, 0.88f, 0.62f, 0.6f};
    btn.disabledColor = btn.normalColor;

    e.Set(MakeUINode(canvas)).Set(t).Set(img).Set(inter).Set(btn);
    m_overlay.push_back(e.ID());

    // 記号は等幅のフォントなので、文字の幅（高さのおよそ半分）の半分だけ左へずらして中央に置く
    constexpr float kGlyphPx = 24.0f;
    m_overlay.push_back(SpawnTextTopLeft(*m_world, (std::string(name) + "Glyph").c_str(), glyph, x - kGlyphPx * 0.25f,
                                         y - kGlyphPx * 0.5f, kGlyphPx, GetMenuStyle(*m_world).value, canvas, kSortValue));
    return e.ID();
}



void SettingsScreenSystem::Open(EntityID opener)
{
    // 処理フロー:
    // 1. 開いた状態にする
    // 2. 下の画面のボタンを操作できなくする
    // 3. Canvas と暗転を作る
    // 4. 見出しを作る
    // 5. 行（ボタン・< ゲージ > ・値）を作る
    // 6. 操作のヒントを作る

    // ---------------------------------------------------------
    // 1. 開いた状態
    // ---------------------------------------------------------
    World &w = *m_world;
    m_open   = true;
    m_opener = opener;
    w.GetResource<SettingsScreenState>().open = true;

    // ---------------------------------------------------------
    // 2. 下の画面のボタン
    // ---------------------------------------------------------

    // 開いている間に、裏のボタンへ選択が移ったり、押せたりしないようにする
    // 操作できなくしたものだけを控え、閉じるときに戻す
    m_disabled.clear();
    w.ViewEach<UIInteractableComponent>([&](EntityID id) {
        auto *inter = w.GetComponent<UIInteractableComponent>(id);
        if (inter && inter->interactable)
        {
            inter->interactable = false;
            m_disabled.push_back(id);
        }
    });

    // ---------------------------------------------------------
    // 3. Canvas と暗転
    // ---------------------------------------------------------

    // 作る部品はすべて Canvas の子にする
    m_overlay.clear();
    const EntityID canvas = SpawnCanvas(w, "SettingsCanvas");
    m_overlay.push_back(canvas);

    // 下の画面（ポーズ・タイトル・HUD）の UI を薄くしながら設定画面を出す
    // 入れ替えは、設定画面が出そろう次のフレームから UpdateFade が始める
    m_fadeStarted         = false;
    m_closing             = false;
    m_restoreBelowPending = false;
    m_fade                = 0.0f;

    // 暗転は、ポーズ画面と同じ薄さにする。入れ替えると下の画面の暗転は隠れるので、こちらで同じ暗さを作る
    static const DirectX::XMFLOAT4 kDimBottom = {0.02f, 0.02f, 0.05f, 0.82f};
    m_overlay.push_back(SpawnPanel(w, "SettingsDim", {0.0f, 0.0f}, {4000.0f, 4000.0f}, {0.0f, 0.0f, 0.0f, 0.5f},
                                   kSortDim, -1.0f, 0.0f, canvas, &kDimBottom));

    // ---------------------------------------------------------
    // 4. 見出し
    // ---------------------------------------------------------
    const MenuStyleComponent style = GetMenuStyle(w);
    SpawnScreenHeading(w, canvas, "SETTINGS", style.gold, m_overlay, kSortHead);
    {
        // 英語の見出しに、日本語を小さく添える。打ち込みの演出は他の画面と同じ
        UIRevealComponent rv;
        rv.mode  = UIRevealMode::Type;
        rv.delay = 0.2f;
        rv.speed = style.typeSpeed;
        rv.text  = "設定";
        m_overlay.push_back(SpawnTextTopLeft(w, "SettingsSub", "", style.left, style.SubY(), 20.0f, style.sub, canvas,
                                             kSortHead, 1.0f, &rv));
    }

    // ---------------------------------------------------------
    // 5. 行
    // ---------------------------------------------------------

    // 行のボタンは入れ物の中央からの位置で置く（SpawnButton の決まり）
    const float    rowsTop = style.RuleY() + kRowsGap;
    const float    boxH    = kRowH * static_cast<float>(RowCount);
    const EntityID box     = SpawnBox(w, "SettingsRows", {0.0f, 0.0f}, {style.left, rowsTop}, {320.0f, boxH}, canvas);
    m_overlay.push_back(box);

    // 値の部品（< ゲージ > 値）の横位置。左端は MenuStyle に合わせる
    const float decX   = style.left + kDecOffset;
    const float gaugeX = style.left + kGaugeOffset;
    const float incX   = gaugeX + kGaugeW + kIncGap;
    const float valueX = incX + kValueGap;

    const char *labels[RowCount] = {"視点の感度", "BGMの音量", "効果音の音量", "戻る"};
    for (int i = 0; i < RowCount; ++i)
    {
        // --- 行のボタン ---
        char btnName[32], lblName[32];
        std::snprintf(btnName, sizeof(btnName), "SettingsRow%d", i);
        std::snprintf(lblName, sizeof(lblName), "SettingsRowLabel%d", i);

        const float offsetY = -boxH * 0.5f + kRowH * (static_cast<float>(i) + 0.5f);
        const auto  ids     = SpawnButton(w, btnName, lblName, labels[i], {0.0f, offsetY},
                                          i == RowBack ? "settings_back" : "settings_row", i == 0, kSortRow, -1.0f, box);
        m_rows[i] = ids.button;
        m_overlay.push_back(ids.button);
        m_overlay.push_back(ids.label);

        // 「戻る」は値を持たないので、ボタンだけで終わる
        if (i == RowBack) break;

        // --- 値の部品（< ゲージ > 値） ---

        // 画面の左上を基準に、行の中心の高さへ置く
        const float y = rowsTop + kRowH * (static_cast<float>(i) + 0.5f);
        char        name[32];
        std::snprintf(name, sizeof(name), "SettingsDec%d", i);
        m_dec[i] = SpawnArrow(canvas, name, "<", decX, y);
        std::snprintf(name, sizeof(name), "SettingsInc%d", i);
        m_inc[i] = SpawnArrow(canvas, name, ">", incX, y);

        // ゲージ
        {
            auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIProgressBarComponent>();
            std::snprintf(name, sizeof(name), "SettingsGauge%d", i);
            auto e = EntityFactory::SpawnNamed(w, name, arch);

            UITransformComponent t;
            t.anchor = {0.0f, 0.0f};
            t.pivot  = {0.0f, 0.5f};
            t.offset = {gaugeX, y};
            t.size   = {kGaugeW, 12.0f};

            // HP ゲージと同じ、斜めの細いゲージにする
            UIProgressBarComponent bar;
            bar.sortOrder  = kSortValue;
            bar.bgColor    = {0.0f, 0.0f, 0.02f, 0.6f};
            bar.fillColor  = style.gold;
            bar.gradient   = 0.25f;
            bar.slant      = 8.0f;
            bar.frame      = 1.5f;
            bar.frameColor = {0.96f, 0.88f, 0.62f, 0.35f};

            // 生成は遅れて反映され、生成直後は GetComponent で引けない。初めの値はここで入れる
            bar.value = ValueRatio(i);

            e.Set(MakeUINode(canvas)).Set(t).Set(bar);
            m_gauge[i] = e.ID();
            m_overlay.push_back(e.ID());
        }

        // 値の文字。初めの値は、ゲージと同じ理由で生成時に入れる
        std::snprintf(name, sizeof(name), "SettingsValue%d", i);
        m_valueText[i] = SpawnTextTopLeft(w, name, ValueText(i).c_str(), valueX, y - 12.0f, 24.0f, style.value, canvas, kSortValue);
        m_overlay.push_back(m_valueText[i]);
    }

    // ---------------------------------------------------------
    // 6. 操作のヒント
    // ---------------------------------------------------------
    SpawnMenuHints(w, box, boxH * 0.5f + 40.0f, m_overlay, 340, -1.0f);
}



void SettingsScreenSystem::Close()
{
    // 処理フロー:
    // 1. 作った部品を消す
    // 2. 下の画面の不透明度を、次のフレームで戻すよう予約する
    // 3. 下の画面のボタンを操作できるように戻す
    // 4. 設定を保存し、閉じた状態にする

    World &w = *m_world;

    // ---------------------------------------------------------
    // 1. 部品を消す
    // ---------------------------------------------------------

    // Destroy は印を付けるだけで、実際に消すのはロジックの最後の HierarchyCleanupSystem
    // ポーズ中はロジックが止まっていて消えないので、ここでは破棄を直接予約する
    // 作った部品は m_overlay にすべて入っているので、親子をたどらなくても取りこぼさない
    for (EntityID id : m_overlay)
    {
        if (w.IsEntityValid(id)) w.RequestDestroyEntity(id);
    }
    m_overlay.clear();

    // ---------------------------------------------------------
    // 2. 不透明度を戻す予約
    // ---------------------------------------------------------

    // 設定画面が実際に消えるのは次のフレーム。それまでは設定画面を描かない（不透明度 0）まま置いておく
    m_restoreBelowPending = true;
    m_fadeStarted         = false;
    m_closing             = false;

    // ---------------------------------------------------------
    // 3. 下の画面のボタン
    // ---------------------------------------------------------

    // 操作できるように戻し、開いたボタンへ選択を戻す
    for (EntityID id : m_disabled)
    {
        if (auto *inter = w.GetComponent<UIInteractableComponent>(id)) inter->interactable = true;
    }
    m_disabled.clear();
    if (auto *inter = w.GetComponent<UIInteractableComponent>(m_opener)) inter->isFocused = true;

    // ---------------------------------------------------------
    // 4. 保存して閉じる
    // ---------------------------------------------------------
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
    case RowBgm:
        s.bgmVolume = std::clamp(Snap(s.bgmVolume + GameSettings::kVolumeStep * dir, GameSettings::kVolumeStep), 0.0f, 1.0f);
        break;
    case RowSfx:
        s.sfxVolume = std::clamp(Snap(s.sfxVolume + GameSettings::kVolumeStep * dir, GameSettings::kVolumeStep), 0.0f, 1.0f);
        break;
    default:
        return;
    }

    // 効果音の音量を変えたときに、下の「移動」の音が新しい音量で鳴るよう、先に反映する
    ApplySettings();
    PlaySound("sfx:/SE_UIMove");
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
    default: return 0.0f;
    }
}



std::string SettingsScreenSystem::ValueText(int row) const
{
    const GameSettings &s = GameSettings::Instance();
    char                buf[16] = {};
    switch (row)
    {
    case RowSensitivity: std::snprintf(buf, sizeof(buf), "%.2fx", s.lookSensitivity); break;
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
    // 処理フロー:
    // 1. 閉じた次のフレームなら、下の画面の不透明度を元に戻す
    // 2. 入れ替えの進み具合を進める
    // 3. 描画リストへ不透明度を書く

    // 生成と破棄は次のフレームに反映される
    // そこで、開いたときは設定画面が出そろう次のフレームから入れ替えを始め、
    // 閉じたときは部品を消した次のフレームで不透明度を戻す
    // 同じフレームで切り替えると、暗転のないフレームや2枚重なるフレームが挟まり、画面がちらつく

    if (!m_world->HasResource<UIDrawList>()) return;
    UIDrawList &list = m_world->GetResource<UIDrawList>();

    // ---------------------------------------------------------
    // 1. 閉じた次のフレーム
    // ---------------------------------------------------------

    // 設定画面は消えたので、下の画面を元どおり描く
    if (m_restoreBelowPending)
    {
        list.splitSortOrder   = INT_MIN;
        list.belowAlpha       = 1.0f;
        list.aboveAlpha       = 1.0f;
        m_restoreBelowPending = false;
    }
    if (!m_open) return;

    // ---------------------------------------------------------
    // 2. 進み具合
    // ---------------------------------------------------------

    // 開いた次のフレームは、設定画面が出そろったところなので、0 から始める
    const float step = dt / kFadeTime;
    if (!m_fadeStarted)
    {
        m_fadeStarted = true;
        m_fade        = 0.0f;
    }
    else if (m_closing)
    {
        m_fade = std::max(m_fade - step, 0.0f);
    }
    else
    {
        m_fade = std::min(m_fade + step, 1.0f);
    }

    // ---------------------------------------------------------
    // 3. 不透明度
    // ---------------------------------------------------------

    // 暗転（kSortDim）を境目にして、手前の設定画面を濃く、奥の下の画面を薄くする
    // 2枚の暗転がどちらも半分の濃さで重なる途中は、背景がわずかに明るくなる
    const float s       = m_fade * m_fade * (3.0f - 2.0f * m_fade); // 動き始めと終わりをゆるやかにした進み具合
    list.splitSortOrder = kSortDim;
    list.aboveAlpha     = s;
    list.belowAlpha     = 1.0f - s;
}



void SettingsScreenSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 設定の値を反映し、入れ替えを進める
    // 2. 閉じていれば、「設定」のボタンが押されたかを見て開く
    // 3. 閉じる要求を受けていれば、入れ替えてから閉じる
    // 4. 左右の入力と Esc・B を読む
    // 5. 行のボタンと左右の入力で値を変える
    // 6. < > ボタンで値を変える

    // ---------------------------------------------------------
    // 1. 反映と入れ替え
    // ---------------------------------------------------------
    ApplySettings();

    World &w = *m_world;

    UpdateFade(dt);

    // ---------------------------------------------------------
    // 2. 開く
    // ---------------------------------------------------------
    if (!m_open)
    {
        EntityID opener = InvalidEntityID;
        ForEachWithID([&](EntityID id, const UIInteractableComponent &inter, const UIButtonComponent &btn) {
            if (inter.clicked && btn.onClickAction == "open_settings") opener = id;
        });
        if (opener != InvalidEntityID) Open(opener);
        return;
    }

    // ---------------------------------------------------------
    // 3. 閉じる
    // ---------------------------------------------------------

    // 閉じる要求は、次のフレームのここで受ける
    // 要求を受けたフレームに閉じると、同じフレームの Esc で PauseSystem がポーズまで閉じてしまう
    if (m_closePending && !m_closing)
    {
        m_closing = true;

        // 押して閉じたときは UISoundSystem が「決定」を鳴らす。Esc・B で閉じたときだけ「閉じる」を鳴らす
        if (m_closeByCancel) PlaySound("sfx:/SE_UIClose");
    }

    // 入れ替えの途中は、操作を受けない。入れ替えが終わったら閉じる
    if (m_closing)
    {
        if (m_fade <= 0.0f) Close();
        return;
    }

    // ---------------------------------------------------------
    // 4. 入力
    // ---------------------------------------------------------
    Input *input = w.HasResource<Input *>() ? w.GetResource<Input *>() : nullptr;
    if (!input) return;

    // エディタの別のウィンドウを操作している間は、キーボードを受けない（GameViewportInfo.h）
    const bool keyUsable = !w.HasResource<GameViewportInfo>() || w.GetResource<GameViewportInfo>().KeyboardToGame();
    auto       key       = [&](int vk) { return keyUsable && input->GetKeyboard().IsTriggered(vk); };
    GamePad   &gp        = input->GetGamePad();

    // 値の左右は、十字キーと左スティックのどちらでも変えられる（行の上下の選択と同じ）
    int dir = 0;
    if (key(VKEY_RIGHT) || gp.IsTriggered(GamePad::BTN_RIGHT | GamePad::BTN_LSTICK_RIGHT))
    {
        dir = 1;
    }
    else if (key(VKEY_LEFT) || gp.IsTriggered(GamePad::BTN_LEFT | GamePad::BTN_LSTICK_LEFT))
    {
        dir = -1;
    }

    if (key(VKEY_ESCAPE) || gp.IsTriggered(GamePad::BTN_B))
    {
        m_closePending  = true;
        m_closeByCancel = true;
        return;
    }

    // ---------------------------------------------------------
    // 5. 行のボタン
    // ---------------------------------------------------------

    // 左右の入力は、選んでいる行（UIInputSystem がマウスの乗っている行を優先して決める）に効かせる
    for (int i = 0; i < RowCount; ++i)
    {
        const auto *inter = w.GetComponent<UIInteractableComponent>(m_rows[i]);
        if (!inter) continue;

        if (inter->clicked && i == RowBack) m_closePending = true;
        if (dir != 0 && inter->isFocused && i != RowBack) Adjust(i, dir);
    }

    // ---------------------------------------------------------
    // 6. < > ボタン
    // ---------------------------------------------------------
    for (int i = 0; i < kValueRows; ++i)
    {
        const auto *dec = w.GetComponent<UIInteractableComponent>(m_dec[i]);
        const auto *inc = w.GetComponent<UIInteractableComponent>(m_inc[i]);
        if (dec && dec->clicked) Adjust(i, -1);
        if (inc && inc->clicked) Adjust(i, 1);
    }
}



REGISTER_RENDER_SYSTEM(SettingsScreenSystem, Priority::RenderStage::R10_UI);
