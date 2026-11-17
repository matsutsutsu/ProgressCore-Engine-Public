#include "Game/UI/Input/InputIconSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Input/InputKeyTable.h"

#include <memory>

using namespace CCL::ECS;

namespace
{
    constexpr const char *kDirKM  = "Assets/Textures/UI/InputPrompts/KM/";
    constexpr const char *kDirPad = "Assets/Textures/UI/InputPrompts/Pad/";

    std::string KM(const char *file) { return std::string(kDirKM) + file; }



    std::string Pad(const char *file) { return std::string(kDirPad) + file; }
}



void InputIconSystem::Update(float dt)
{
    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return;
    const auto inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();
    if (!inputAPI) return;

    const ActiveDeviceType device = inputAPI->GetActiveDeviceType();

    // デバイスを持ち替えたときだけでなく、毎フレーム決め直す
    // 後から作られたアイコン（ポーズメニューのヒントなど）が、次に持ち替えるまで空のままにならないように
    // 対象は画面に出ているアイコン（十数個）だけなので軽い
    ForEachWithID([&](EntityID /*id*/, UIImageComponent &img, const InputIconComponent &icon) {
        std::string path;
        if (icon.kind == InputIconKind::Action)
        {
            InputBinding binding{};
            if (inputAPI->GetPrimaryBinding(icon.actionHash, device, binding))
            {
                path = ResolveBindingIcon(binding, device);
            }
        }
        else
        {
            path = ResolveFixedIcon(icon.kind, device);
        }

        // 同じパスなら書かない
        // 文字列の代入と、テクスチャのキャッシュの検索を毎フレーム走らせないため
        if (img.texturePath != path) img.texturePath = path;

        // 割り当てがない操作・アイコンを用意していない操作は隠す
        // パスが空のままだと UIRenderer が白いダミーを貼り、白い四角が並んでしまう
        img.visible = !path.empty();
    });
}



std::string InputIconSystem::ResolveBindingIcon(const InputBinding &binding,
                                                ActiveDeviceType    device) const
{
    // --- パッド ---
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

    // --- マウス ---
    if (binding.deviceType == InputDeviceType::Mouse_Button)
    {
        if (binding.keyCode == Mouse::BTN_LEFT) return KM("mouse_left.png");
        if (binding.keyCode == Mouse::BTN_RIGHT) return KM("mouse_right.png");
        return {};
    }

    // --- キーボード ---
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
    case InputIconKind::Pause:
        return pad ? std::string{} : KM("key_escape.png"); // START の画像はまだないので、パッドでは隠す
    default:
        break;
    }
    return {};
}



// ポーズ中は描画の System しか回らないので、描画ステージに置く
// ロジックステージだと、ポーズ中に作ったヒントのアイコンが決まらない
REGISTER_RENDER_SYSTEM(InputIconSystem, Priority::RenderStage::R10_UI);
