// 役割・設計方針は UIInputSystem.h を参照。
//
// このSystemは「マウス（hover/press/click）」と「パッド/キーのフォーカス移動＋決定」の
// 両方を1箇所で扱う。clicked のリセット/確定を単一Systemに集約し、二重書きの競合を防ぐ。
#include "UIInputSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/UI/ECS/UILayoutUtil.h"
#include "Engine/UI/ECS/GameViewportInfo.h"
#include "Engine/Platform/Input/Input.h"
#include "Engine/Platform/Input/GamePad.h"

#include <vector>
#include <cfloat>
#include <cmath>

using namespace CCL::ECS;

namespace
{
// windows.h を持ち込まないための仮想キーコード（VK_*）。
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

    // --- 入力（マウス／キー／パッド） ---
    Mouse      &mouse    = input->GetMouse();
    const bool  pressed  = mouse.IsTriggered(Mouse::BTN_LEFT);
    const bool  released = mouse.IsReleased(Mouse::BTN_LEFT);

    auto       &gfx     = Graphics::Instance();
    const float screenW = gfx.GetScreenWidth();
    const float screenH = gfx.GetScreenHeight();

    // マウスの「ウィンドウ・クライアント座標」を UI オーサリング空間(=RT解像度)へ写像する。
    //   全画面/Shipping: GameViewInfo が無い or invalid → 恒等写像（クライアント==RT で一致）。
    //   エディタ: GameView がRTをレターボックス縮小して貼るため、その表示矩形で正規化して戻す。
    float mx = static_cast<float>(mouse.GetPositionX());
    float my = static_cast<float>(mouse.GetPositionY());

    // ゲーム画面が実際に出ているフレームか。出ていないなら入力を一切受けない。
    //   エディタで GameView が別タブの裏に隠れていると、誰も矩形を書かないので
    //   valid=false になる。そこで恒等写像へ落とすと、画面に出ていないボタンの矩形へ
    //   生のクライアント座標が当たり、他のエディタウィンドウ上のクリックが
    //   それを踏んでしまう（BTウィンドウのクリックがポーズメニューのQUITを押した）。
    bool inputUsable = true;

    if (m_world->HasResource<GameViewportInfo>())
    {
        const GameViewportInfo &vp = m_world->GetResource<GameViewportInfo>();
        if (vp.valid && vp.w > 0.0f && vp.h > 0.0f)
        {
            mx = (mx - vp.x) / vp.w * screenW; // 画像内ローカル → [0..1] → UI空間
            my = (my - vp.y) / vp.h * screenH;
        }
        else if (vp.editorVisible)
        {
            // エディタは動いているのに GameView が描かれていない。
            //   キーの決定も止める。BTノードの名前入力中に Enter を押すと、
            //   裏のUIボタンが決定されるのが同じ理屈で起きるため。
            inputUsable = false;
        }
    }

    Keyboard &kb = input->GetKeyboard();
    GamePad  &gp = input->GetGamePad();
    const bool navUp    = kb.IsTriggered(VKEY_UP)    || gp.IsTriggered(GamePad::BTN_UP);
    const bool navDown  = kb.IsTriggered(VKEY_DOWN)  || gp.IsTriggered(GamePad::BTN_DOWN);
    const bool navLeft  = kb.IsTriggered(VKEY_LEFT)  || gp.IsTriggered(GamePad::BTN_LEFT);
    const bool navRight = kb.IsTriggered(VKEY_RIGHT) || gp.IsTriggered(GamePad::BTN_RIGHT);
    const bool confirm  = inputUsable
                       && (kb.IsTriggered(VKEY_RETURN) || kb.IsTriggered(VKEY_SPACE) || gp.IsTriggered(GamePad::BTN_A));
    const bool anyDir   = inputUsable && (navUp || navDown || navLeft || navRight);

    auto inRect = [&](const UITransformComponent &t) {
        return mx >= t.computedPos.x && mx <= t.computedPos.x + t.computedSize.x &&
               my >= t.computedPos.y && my <= t.computedPos.y + t.computedSize.y;
    };

    // --- Pass 1: 矩形計算 ＋ マウス最前面ヒット ＋ フォーカス候補(中心)の収集 ---
    EntityID topID       = InvalidEntityID; // マウスが乗っている最前面
    int      topPriority = 0;
    EntityID focusedID   = InvalidEntityID; // 現在のフォーカス
    struct Cand { EntityID id; float cx, cy; };
    std::vector<Cand> cands;

    ForEachWithID([&](EntityID id, UITransformComponent &t, UIInteractableComponent &inter) {
        CCL::UI::ComputeUIRect(*m_world, id, t, screenW, screenH); // 階層(Canvas offset/scale)込みで解決
        if (!inter.interactable) return;

        // マウスヒット（最前面優先）。ゲーム画面が出ていないフレームは誰にも当てない。
        if (inputUsable && inRect(t) && (topID == InvalidEntityID || inter.priority >= topPriority))
        {
            topID       = id;
            topPriority = inter.priority;
        }

        // フォーカス候補
        if (inter.focusable)
        {
            cands.push_back({id, t.computedPos.x + t.computedSize.x * 0.5f,
                                 t.computedPos.y + t.computedSize.y * 0.5f});
            if (inter.isFocused) focusedID = id;
        }
    });

    // --- フォーカスの移動先を決定 ---
    EntityID newFocus = focusedID;
    if (!cands.empty())
    {
        if (focusedID == InvalidEntityID)
        {
            if (anyDir) newFocus = cands.front().id; // 初回入力でどれかへ
        }
        else if (anyDir)
        {
            float fx = 0.0f, fy = 0.0f;
            for (const auto &c : cands)
                if (c.id == focusedID) { fx = c.cx; fy = c.cy; break; }

            float    best   = FLT_MAX;
            EntityID bestId = focusedID;
            for (const auto &c : cands)
            {
                if (c.id == focusedID) continue;
                const float dx = c.cx - fx, dy = c.cy - fy;
                float       score = FLT_MAX;
                const float eps   = 1.0f;
                // 押した方向にある候補のみ対象。直交ズレを重み付けして最も素直な隣を選ぶ。
                if (navDown && dy > eps)       score = dy + std::abs(dx) * 2.0f;
                else if (navUp && dy < -eps)   score = -dy + std::abs(dx) * 2.0f;
                else if (navRight && dx > eps) score = dx + std::abs(dy) * 2.0f;
                else if (navLeft && dx < -eps) score = -dx + std::abs(dy) * 2.0f;
                if (score < best) { best = score; bestId = c.id; }
            }
            newFocus = bestId;
        }
    }

    // --- Pass 2: 状態確定（hover/press/click/focus） ---
    ForEachWithID([&](EntityID id, UITransformComponent & /*t*/, UIInteractableComponent &inter) {
        inter.clicked = false;

        // マウス hover/press/click
        const bool hotMouse = (id == topID) && inter.interactable;
        inter.isHovered     = hotMouse;
        if (pressed && hotMouse)
            inter.isPressed = true;
        if (released)
        {
            if (inter.isPressed && hotMouse) inter.clicked = true;
            inter.isPressed = false;
        }

        // パッド/キー focus と決定
        inter.isFocused = inter.interactable && inter.focusable && (id == newFocus);
        if (confirm && inter.isFocused)
            inter.clicked = true;
    });
}

REGISTER_RENDER_SYSTEM(UIInputSystem, Priority::RenderStage::R10_UI);
