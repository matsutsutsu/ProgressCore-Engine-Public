#include "UIInputSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/UI/ECS/GameViewportInfo.h"
#include "Engine/Platform/Input/Input.h"
#include "Engine/Platform/Input/GamePad.h"

#include <vector>
#include <cfloat>
#include <cmath>

using namespace CCL::ECS;

namespace
{
    // windows.h を持ち込まないための仮想キーコード（VK_*）
    constexpr int VKEY_LEFT   = 0x25;
    constexpr int VKEY_UP     = 0x26;
    constexpr int VKEY_RIGHT  = 0x27;
    constexpr int VKEY_DOWN   = 0x28;
    constexpr int VKEY_RETURN = 0x0D;
    constexpr int VKEY_SPACE  = 0x20;
} // namespace

void UIInputSystem::Update(float /*dt*/)
{
    if (!m_world->HasResource<Input *>()) return;
    Input *input = m_world->GetResource<Input *>();
    if (!input) return;

    Mouse     &mouse    = input->GetMouse();
    const bool pressed  = mouse.IsTriggered(Mouse::BTN_LEFT);
    const bool released = mouse.IsReleased(Mouse::BTN_LEFT);

    // カーソルがこのフレームで動いたか。
    //   hover した要素へ無条件にフォーカスを移すと、キーで選び直した直後に、
    //   止まっているカーソルの下の要素へフォーカスが戻されてしまう。
    //   動いたフレームだけ移すことで、マウスとパッドのどちらで操作しても
    //   ハイライトされている 1 つが操作対象になる
    const bool mouseMoved = (mouse.GetPositionX() != mouse.GetOldPositionX()) ||
                            (mouse.GetPositionY() != mouse.GetOldPositionY());

    auto       &gfx     = Graphics::Instance();
    const float screenW = gfx.GetScreenWidth();
    const float screenH = gfx.GetScreenHeight();

    // マウスのクライアント座標を UI の座標系（RT の解像度）へ変換する。
    //   全画面 / Shipping: GameViewportInfo が無いか valid=false なので変換しない（クライアント == RT）。
    //   エディタ: GameView が RT を縮小表示しているので、その表示矩形で正規化して戻す
    float mx = static_cast<float>(mouse.GetPositionX());
    float my = static_cast<float>(mouse.GetPositionY());

    // ゲーム画面が実際に出ているフレームか。出ていないなら入力を一切受けない。
    //   エディタで GameView が別タブの裏に隠れていると誰も矩形を書かず valid=false になる。
    //   そこで変換なしにすると、画面に出ていないボタンの矩形に他のエディタウィンドウ上の
    //   クリックが当たる（BT ウィンドウのクリックがポーズメニューの QUIT を押した）
    bool inputUsable = true;

    if (m_world->HasResource<GameViewportInfo>())
    {
        const GameViewportInfo &vp = m_world->GetResource<GameViewportInfo>();
        if (vp.valid && vp.w > 0.0f && vp.h > 0.0f)
        {
            mx = (mx - vp.x) / vp.w * screenW; // 画像内の位置 → [0,1] → UI の座標
            my = (my - vp.y) / vp.h * screenH;
        }
        else if (vp.editorVisible)
        {
            // エディタは動いているのに GameView が表示されていない。
            //   キーの決定も止める。止めないと BT ノードの名前入力中の Enter で
            //   裏の UI ボタンが決定されてしまう
            inputUsable = false;
        }
    }

    // キーボードはエディタの別ウィンドウ（BT エディタなど）を操作中なら受けない。
    //   Space や Enter はエディタでも使うので、そのまま読むと裏のボタンまで決定してしまう。
    //   パッドはエディタで使わないので止めない。
    const bool keyUsable = !m_world->HasResource<GameViewportInfo>() ||
                           m_world->GetResource<GameViewportInfo>().KeyboardToGame();
    auto keyTriggered = [&](int vk) { return keyUsable && input->GetKeyboard().IsTriggered(vk); };

    GamePad   &gp       = input->GetGamePad();
    const bool navUp    = keyTriggered(VKEY_UP) || gp.IsTriggered(GamePad::BTN_UP);
    const bool navDown  = keyTriggered(VKEY_DOWN) || gp.IsTriggered(GamePad::BTN_DOWN);
    const bool navLeft  = keyTriggered(VKEY_LEFT) || gp.IsTriggered(GamePad::BTN_LEFT);
    const bool navRight = keyTriggered(VKEY_RIGHT) || gp.IsTriggered(GamePad::BTN_RIGHT);
    const bool confirm  = inputUsable && (keyTriggered(VKEY_RETURN) || keyTriggered(VKEY_SPACE) || gp.IsTriggered(GamePad::BTN_A));
    const bool anyDir   = inputUsable && (navUp || navDown || navLeft || navRight);

    auto inRect = [&](const UITransformComponent &t) {
        return mx >= t.computedPos.x && mx <= t.computedPos.x + t.computedSize.x &&
               my >= t.computedPos.y && my <= t.computedPos.y + t.computedSize.y;
    };

    // 1 周目: マウスが乗っている最前面の要素と、フォーカス候補（要素の中心）を集める
    EntityID topID        = InvalidEntityID; // マウスが乗っている最前面の要素
    int      topPriority  = 0;
    bool     topFocusable = false; // その要素がフォーカス対象か（hover でフォーカスを移すのに使う）
    EntityID focusedID   = InvalidEntityID; // 現在のフォーカス
    struct Cand
    {
        EntityID id;
        float    cx, cy;
    };
    std::vector<Cand> cands;

    ForEachWithID([&](EntityID id, const UITransformComponent &t, UIInteractableComponent &inter) {
        if (!inter.interactable) return;

        // マウスのヒット（最前面を優先）。ゲーム画面が出ていないフレームは誰にも当てない
        if (inputUsable && inRect(t) && (topID == InvalidEntityID || inter.priority >= topPriority))
        {
            topID        = id;
            topPriority  = inter.priority;
            topFocusable = inter.focusable;
        }

        // フォーカス候補
        if (inter.focusable)
        {
            cands.push_back({id, t.computedPos.x + t.computedSize.x * 0.5f,
                             t.computedPos.y + t.computedSize.y * 0.5f});
            if (inter.isFocused) focusedID = id;
        }
    });

    // フォーカスの移動先を決める
    EntityID newFocus = focusedID;
    if (!cands.empty())
    {
        if (focusedID == InvalidEntityID)
        {
            if (anyDir) newFocus = cands.front().id; // 最初の方向入力で先頭の候補へ
        }
        else if (anyDir)
        {
            float fx = 0.0f, fy = 0.0f;
            for (const auto &c : cands)
                if (c.id == focusedID)
                {
                    fx = c.cx;
                    fy = c.cy;
                    break;
                }

            float    best   = FLT_MAX;
            EntityID bestId = focusedID;
            for (const auto &c : cands)
            {
                if (c.id == focusedID) continue;
                const float dx = c.cx - fx, dy = c.cy - fy;
                float       score = FLT_MAX;
                const float eps   = 1.0f;
                // 押した方向にある候補だけを対象にする。直交方向のずれを 2 倍で罰し、まっすぐ隣にある候補を選ぶ
                if (navDown && dy > eps)
                    score = dy + std::abs(dx) * 2.0f;
                else if (navUp && dy < -eps)
                    score = -dy + std::abs(dx) * 2.0f;
                else if (navRight && dx > eps)
                    score = dx + std::abs(dy) * 2.0f;
                else if (navLeft && dx < -eps)
                    score = -dx + std::abs(dy) * 2.0f;
                if (score < best)
                {
                    best   = score;
                    bestId = c.id;
                }
            }
            newFocus = bestId;
        }
    }

    // カーソルを動かして要素に乗せたら、そこへフォーカスも移す。
    //   これが無いと、キーで選んでいる要素とマウスが乗っている要素が別々にハイライトされ
    //   （UIButtonSystem は hover と focus を同じ色にする）、決定キーがどちらに効くか分からなくなる
    if (mouseMoved && topID != InvalidEntityID && topFocusable)
        newFocus = topID;

    // 2 周目: hover / press / click / focus の状態を確定する
    ForEachWithID([&](EntityID id, const UITransformComponent & /*t*/, UIInteractableComponent &inter) {
        inter.clicked = false;

        // マウスの hover / press / click
        const bool hotMouse = (id == topID) && inter.interactable;
        inter.isHovered     = hotMouse;
        if (pressed && hotMouse)
            inter.isPressed = true;
        if (released)
        {
            if (inter.isPressed && hotMouse) inter.clicked = true;
            inter.isPressed = false;
        }

        // パッド/キーの focus と決定
        inter.isFocused = inter.interactable && inter.focusable && (id == newFocus);
        if (confirm && inter.isFocused)
            inter.clicked = true;
    });
}

REGISTER_RENDER_SYSTEM(UIInputSystem, Priority::RenderStage::R10_UI);
