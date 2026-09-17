// 役割・設計方針は InputIconSystem.h を参照。
#include "InputIconSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Input/InputKeyTable.h" // VK_* / GamePad::BTN_* / Mouse::BTN_*

#include <memory>

using namespace CCL::ECS;

namespace
{
    constexpr const char *kDirKM  = "Assets/Textures/UI/InputPrompts/KM/";
    constexpr const char *kDirPad = "Assets/Textures/UI/InputPrompts/Pad/";

    std::string KM(const char *file) { return std::string(kDirKM) + file; }
    std::string Pad(const char *file) { return std::string(kDirPad) + file; }
} // namespace

std::string InputIconSystem::ResolveBindingIcon(const InputBinding &binding,
                                                ActiveDeviceType    device) const
{
    if (device == ActiveDeviceType::GamePad)
    {
        if (binding.deviceType == InputDeviceType::GamePad_TriggerR) return Pad("rt.png");

        if (binding.deviceType == InputDeviceType::GamePad_Button)
        {
            switch (binding.keyCode)
            {
            case GamePad::BTN_A:
                return Pad("btn_a.png");
            case GamePad::BTN_B:
                return Pad("btn_b.png");
            case GamePad::BTN_X:
                return Pad("btn_x.png");
            case GamePad::BTN_LEFT_SHOULDER:
                return Pad("lb.png");
            case GamePad::BTN_RIGHT_SHOULDER:
                return Pad("rb.png");
            case GamePad::BTN_RIGHT_TRIGGER:
                return Pad("rt.png");
            case GamePad::BTN_RIGHT_THUMB:
                return Pad("stick_r_press.png");
            default:
                break;
            }
        }
        return {};
    }

    if (binding.deviceType == InputDeviceType::Mouse_Button)
    {
        if (binding.keyCode == Mouse::BTN_LEFT) return KM("mouse_left.png");
        if (binding.keyCode == Mouse::BTN_RIGHT) return KM("mouse_right.png");
        return {};
    }

    if (binding.deviceType == InputDeviceType::Keyboard)
    {
        switch (binding.keyCode)
        {
        case VK_SPACE:
            return KM("key_space.png");
        case VK_SHIFT:
            return KM("key_shift.png");
        case 'R':
            return KM("key_r.png");
        default:
            break;
        }
    }
    return {};
}

std::string InputIconSystem::ResolveFixedIcon(InputIconKind kind, ActiveDeviceType device) const
{
    const bool pad = (device == ActiveDeviceType::GamePad);
    switch (kind)
    {
    case InputIconKind::Move:
        return pad ? Pad("stick_l.png") : KM("arrows_all.png");
    case InputIconKind::Camera:
        return pad ? Pad("stick_r.png") : KM("mouse_move.png");
    case InputIconKind::MenuConfirm:
        return pad ? Pad("btn_a.png") : KM("key_enter.png");
    case InputIconKind::MenuNavigate:
        return pad ? Pad("dpad_all.png") : KM("arrows_all.png");
    default:
        break;
    }
    return {};
}

void InputIconSystem::Update(float /*dt*/)
{
    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return;
    const auto inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();
    if (!inputAPI) return;

    const ActiveDeviceType device = inputAPI->GetActiveDeviceType();

    // 毎フレーム解決し、変わったものだけ書き込む。
    //   「持ち替えたフレームだけ処理する」ほうが軽いが、それだとポーズメニューのように
    //   後から生成されたアイコンが、次に持ち替えるまで空のまま出てこない。
    //   対象は画面に出ているアイコンだけ（十数個）なので、毎フレーム解決しても
    //   switch 数回ぶんでしかない。取りこぼさない方を選ぶ。
    ForEachWithID([&](EntityID /*id*/, UIImageComponent &img, const InputIconComponent &icon) {
        std::string path;
        if (icon.kind == InputIconKind::Action)
        {
            InputBinding binding{};
            if (inputAPI->GetPrimaryBinding(icon.actionHash, device, binding))
                path = ResolveBindingIcon(binding, device);
        }
        else
        {
            path = ResolveFixedIcon(icon.kind, device);
        }

        // 同じパスなら触らない。std::string の代入とテクスチャキャッシュ検索を毎フレーム
        // 走らせないための差分書き込み。
        if (img.texturePath != path) img.texturePath = path;

        // 割り当てが無い／アイコンを用意していない操作は隠す。
        //   空パスのままだと UIRenderer が白ダミーを貼り、白い四角が並んでしまう。
        img.visible = !path.empty();
    });
}

// UI帯(R10_UI)。ロジックではなく描画ステージに置く理由はヘッダの「実行ステージ」を参照
// （ポーズ中に生成されたヒントアイコンを解決するため）。
REGISTER_RENDER_SYSTEM(InputIconSystem, Priority::RenderStage::R10_UI);
