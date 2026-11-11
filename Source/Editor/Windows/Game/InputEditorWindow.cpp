#include "Editor/Windows/Game/InputEditorWindow.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Platform/Input/InputKeyTable.h"
#include "Engine/Platform/Core/FileSystem.h"
#include <imgui.h>
#include <Xinput.h>
#include <json.hpp>
#include <cstring>
#include <cctype>

#pragma comment(lib, "xinput.lib")

using json = nlohmann::json;

InputEditorWindow::InputEditorWindow() : EditorWindow("InputMappingEditor")
{
    LoadFromJson();
}



// ---------------------------------------------------------------------------
// ファイルの読み書き
// ---------------------------------------------------------------------------

void InputEditorWindow::LoadFromJson()
{
    m_actions.clear();
    m_axes.clear();
    m_captureTarget = nullptr; // データを作り直すので、キャプチャの対象を必ず外す

    std::string jsonStr = FileSystem::ReadAllText(m_configPath);
    if (jsonStr.empty()) return;

    json doc;
    try
    {
        doc = json::parse(jsonStr);

        if (doc.contains("Actions"))
        {
            for (const auto &act : doc["Actions"])
            {
                ActionData action;
                action.name    = act["Name"].get<std::string>();
                action.trigger = act.value("Trigger", std::string("Down"));
                for (const auto &b : act["Bindings"])
                {
                    action.bindings.push_back({b["Device"], b["Key"], 1.0f});
                }
                m_actions.push_back(action);
            }
        }

        if (doc.contains("Axes"))
        {
            for (const auto &ax : doc["Axes"])
            {
                AxisData axis;
                axis.name = ax["Name"].get<std::string>();
                for (const auto &b : ax["Bindings"])
                {
                    axis.bindings.push_back({b["Device"], b["Key"], b.value("Scale", 1.0f)});
                }
                m_axes.push_back(axis);
            }
        }
    }
    catch (...)
    {
        // 構文エラーなどのときは、読めたところまでで終える
    }
}



void InputEditorWindow::SaveToJson()
{
    json doc;
    doc["Actions"] = json::array();
    doc["Axes"]    = json::array();

    for (const auto &act : m_actions)
    {
        json jAct;
        jAct["Name"]     = act.name;
        jAct["Trigger"]  = act.trigger;
        jAct["Bindings"] = json::array();
        for (const auto &b : act.bindings)
        {
            jAct["Bindings"].push_back({{"Device", b.device}, {"Key", b.key}});
        }
        doc["Actions"].push_back(jAct);
    }

    for (const auto &ax : m_axes)
    {
        json jAx;
        jAx["Name"]     = ax.name;
        jAx["Bindings"] = json::array();
        for (const auto &b : ax.bindings)
        {
            jAx["Bindings"].push_back({{"Device", b.device}, {"Key", b.key}, {"Scale", b.scale}});
        }
        doc["Axes"].push_back(jAx);
    }

    FileSystem::WriteAllText(m_configPath, doc.dump(4));
}



// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

void InputEditorWindow::DrawDeviceSelector(BindingData &b, bool isAxis)
{
    // デバイスの選択（ラジオボタン）
    //   切り替えたら b.device / b.key を、そのデバイスの既定値に戻す
    //   軸のタブ（isAxis=true）ではゲームパッドの既定を左スティックにする

    using DC = InputKeyTable::DeviceClass;
    DC cur   = InputKeyTable::ClassOf(b.device);

    auto radio = [&](const char *label, DC cls,
                     const char *defDevice, const char *defKey) {
        if (ImGui::RadioButton(label, cur == cls))
        {
            if (cur != cls)
            { // 実際に切り替わったときだけ戻す
                b.device = defDevice;
                b.key    = defKey;
            }
        }
    };

    radio("キーボード", DC::Keyboard, "Keyboard", "NONE");
    ImGui::SameLine();
    radio("マウス", DC::Mouse, "Mouse_Button", "BTN_LEFT");
    ImGui::SameLine();
    // ゲームパッドの既定値はタブで変える（軸のタブなら左スティック）
    radio("ゲームパッド", DC::GamePad,
          isAxis ? "GamePad_AxisLX" : "GamePad_Button",
          "NONE");
}



void InputEditorWindow::DrawKeyCombo(BindingData &b, bool isAxis)
{
    // キーの選択（コンボボックス）
    //   選択中のデバイスに対応した候補だけを出す（絞り込みの入力欄つき）
    //   軸のタブのゲームパッドは「スティック / トリガー」と「ボタン」を見出しで分ける

    using DC = InputKeyTable::DeviceClass;
    DC cls   = InputKeyTable::ClassOf(b.device);

    std::string preview = InputKeyTable::DisplayNameOf(b.device, b.key);

    if (ImGui::BeginCombo("キー##KeySelect", preview.c_str()))
    {
        // 絞り込みの文字列。選んだら空に戻す
        static char filter[32] = "";
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "絞り込み...", filter, sizeof(filter));
        ImGui::Separator();

        std::string f = filter;
        for (auto &c : f)
            c = (char)std::tolower((unsigned char)c);

        auto drawList = [&](const char                                     *header,
                            const std::vector<InputKeyTable::UnifiedEntry> &list,
                            bool                                            showHeader) {
            bool headerShown = false;
            for (const auto &e : list)
            {
                if (!f.empty())
                {
                    std::string disp(e.display), key(e.key);
                    for (auto &c : disp)
                        c = (char)std::tolower((unsigned char)c);
                    for (auto &c : key)
                        c = (char)std::tolower((unsigned char)c);
                    if (disp.find(f) == std::string::npos &&
                        key.find(f) == std::string::npos) continue;
                }
                if (showHeader && !headerShown)
                {
                    ImGui::SeparatorText(header);
                    headerShown = true;
                }
                bool isSelected = (b.device == e.device && b.key == e.key);
                if (ImGui::Selectable(std::string(e.display).c_str(), isSelected))
                {
                    b.device  = std::string(e.device);
                    b.key     = std::string(e.key);
                    filter[0] = '\0';
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
        };

        switch (cls)
        {
        case DC::Keyboard:
            drawList("", InputKeyTable::KeyboardEntries(), false);
            break;
        case DC::Mouse:
            drawList("", InputKeyTable::MouseEntries(), false);
            break;
        case DC::GamePad:
            if (isAxis)
            {
                drawList("スティック / トリガー", InputKeyTable::GamePadAxisEntries(), true);
                drawList("ボタン", InputKeyTable::GamePadEntries(), true);
            }
            else
            {
                drawList("", InputKeyTable::GamePadEntries(), false);
            }
            break;
        }
        ImGui::EndCombo();
    }
}



void InputEditorWindow::DrawBindingParams(BindingData &b, bool isAxis)
{
    DrawDeviceSelector(b, isAxis);

    // 軸（左スティック等）には個別のキーがないので、コンボは出さず読み取り専用で表示する
    if (InputKeyTable::IsAnalogDevice(b.device))
    {
        b.key            = "NONE";
        std::string disp = InputKeyTable::DisplayNameOf(b.device, b.key);
        ImGui::BeginDisabled();
        char buf[64];
        strcpy_s(buf, disp.c_str());
        ImGui::InputText("キー##ReadOnly", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
    }
    else
    {
        DrawKeyCombo(b, isAxis);

        // キャプチャ（押して登録）。ボタンのデバイスだけ
        ImGui::SameLine();
        if (m_captureTarget == &b)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.5f, 0.1f, 1.0f));
            if (ImGui::Button("キーを押してください... (ESCで中止)"))
            {
                m_captureTarget = nullptr;
            }
            ImGui::PopStyleColor();

            if (!m_captureArmed)
            {
                if (!ImGui::IsAnyMouseDown()) m_captureArmed = true;
            }
            else if (::GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            {
                m_captureTarget = nullptr;
            }
            else
            {
                using DC      = InputKeyTable::DeviceClass;
                DC   cls      = InputKeyTable::ClassOf(b.device);
                bool captured = false;

                if (cls == DC::GamePad)
                {
                    XINPUT_STATE st{};
                    if (XInputGetState(0, &st) == ERROR_SUCCESS)
                    {
                        InputKeyTable::PadHit hit;
                        if (InputKeyTable::DetectPressedGamePadButton(st.Gamepad.wButtons, hit))
                        {
                            b.device = std::string(hit.device);
                            b.key    = std::string(hit.key);
                            captured = true;
                        }
                    }
                }
                else
                { // キーボード・マウスのどちらのタブでも、拾うのはキーボードのキー
                    std::string_view kb = InputKeyTable::DetectPressedKeyboardLabel();
                    if (!kb.empty())
                    {
                        b.device = "Keyboard";
                        b.key    = std::string(kb);
                        captured = true;
                    }
                }
                if (captured) m_captureTarget = nullptr;
            }
        }
        else
        {
            if (ImGui::Button("キャプチャ"))
            {
                m_captureTarget = &b;
                m_captureArmed  = false;
            }
        }
    }

    // 軸だけ感度（符号で向き）を持つ
    if (isAxis)
    {
        ImGui::DragFloat("感度 / 方向##Scale", &b.scale, 0.1f, -1.0f, 1.0f);
    }
}



void InputEditorWindow::DrawContents(EditorContext &context)
{
    // ---- 読み直しと保存 ----
    if (ImGui::Button("JSONから再読み込み"))
    {
        LoadFromJson();
    }
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    if (ImGui::Button("保存してエンジンに反映"))
    {
        SaveToJson();

        // 保存した JSON を InputFacade に読み直させる
        if (context.world && context.world->HasResource<std::shared_ptr<IInputAPI>>())
        {
            auto inputAPI = context.world->GetResource<std::shared_ptr<IInputAPI>>();
            inputAPI->LoadConfig(m_configPath);
        }
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "まず「キーボード / マウス / ゲームパッド」を選び、次にキーを選びます。\n"
            "「キャプチャ」を押して実際にキー/ボタンを押すと自動登録できます。\n"
            "SHIFT/CTRL/ALT/Fキー/テンキー/パッド各ボタンに対応。");
    }

    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("InputTabs"))
    {
        // ---- アクション（ボタン） ----
        if (ImGui::BeginTabItem("アクション（デジタル）"))
        {
            if (ImGui::Button("＋ アクションを追加"))
            {
                m_captureTarget = nullptr; // 追加で要素が移動しうるので、キャプチャの対象を外す
                m_actions.push_back({"NewAction", {}});
            }
            ImGui::Separator();

            for (size_t i = 0; i < m_actions.size(); ++i)
            {
                ImGui::PushID((int)i);
                auto &act = m_actions[i];

                char nameBuf[64];
                strcpy_s(nameBuf, act.name.c_str());
                if (ImGui::InputText("アクション名", nameBuf, sizeof(nameBuf)))
                {
                    act.name = nameBuf;
                }

                ImGui::SameLine(ImGui::GetWindowWidth() - 100);
                if (ImGui::Button("削除##Action"))
                {
                    m_captureTarget = nullptr; // 削除した要素を指したまま残さない
                    m_actions.erase(m_actions.begin() + i);
                    ImGui::PopID();
                    break;
                }

                // 発火のタイミング
                {
                    // 表示は日本語、保存する値は英字（InputFacade が読む JSON の値）
                    const char *trigLabels[] = {"押しっぱなし", "押した瞬間", "離した瞬間"};
                    const char *trigValues[] = {"Down", "Triggered", "Released"};
                    const char *trigDesc[]   = {
                        "押されている間ずっと反応します",
                        "押した瞬間の1フレームだけ反応します",
                        "離した瞬間の1フレームだけ反応します"};
                    int cur = 0;
                    if (act.trigger == "Triggered")
                        cur = 1;
                    else if (act.trigger == "Released")
                        cur = 2;

                    ImGui::SetNextItemWidth(160);
                    if (ImGui::Combo("発火タイミング", &cur, trigLabels, IM_ARRAYSIZE(trigLabels)))
                    {
                        act.trigger = trigValues[cur];
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", trigDesc[cur]);
                    }
                }

                ImGui::Indent();
                for (size_t j = 0; j < act.bindings.size(); ++j)
                {
                    ImGui::PushID((int)j);
                    ImGui::Text("割り当て %zu:", j);
                    ImGui::SameLine();
                    if (ImGui::Button("削除##Bind"))
                    {
                        m_captureTarget = nullptr; // 削除した要素を指したまま残さない
                        act.bindings.erase(act.bindings.begin() + j);
                        ImGui::PopID();
                        break;
                    }
                    DrawBindingParams(act.bindings[j], false);
                    ImGui::Separator();
                    ImGui::PopID();
                }

                if (ImGui::Button("＋ 割り当てを追加"))
                {
                    m_captureTarget = nullptr; // 追加で要素が移動しうるので、キャプチャの対象を外す
                    act.bindings.push_back(BindingData());
                }
                ImGui::Unindent();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }

        // ---- 軸（アナログ） ----
        if (ImGui::BeginTabItem("軸（アナログ）"))
        {
            if (ImGui::Button("＋ 軸を追加"))
            {
                m_captureTarget = nullptr; // 追加で要素が移動しうるので、キャプチャの対象を外す
                m_axes.push_back({"NewAxis", {}});
            }
            ImGui::Separator();

            for (size_t i = 0; i < m_axes.size(); ++i)
            {
                ImGui::PushID((int)i);
                auto &ax = m_axes[i];

                char nameBuf[64];
                strcpy_s(nameBuf, ax.name.c_str());
                if (ImGui::InputText("軸名", nameBuf, sizeof(nameBuf)))
                {
                    ax.name = nameBuf;
                }

                ImGui::SameLine(ImGui::GetWindowWidth() - 100);
                if (ImGui::Button("削除##Axis"))
                {
                    m_captureTarget = nullptr; // 削除した要素を指したまま残さない
                    m_axes.erase(m_axes.begin() + i);
                    ImGui::PopID();
                    break;
                }

                ImGui::Indent();
                for (size_t j = 0; j < ax.bindings.size(); ++j)
                {
                    ImGui::PushID((int)j);
                    ImGui::Text("割り当て %zu:", j);
                    ImGui::SameLine();
                    if (ImGui::Button("削除##AxisBind"))
                    {
                        m_captureTarget = nullptr; // 削除した要素を指したまま残さない
                        ax.bindings.erase(ax.bindings.begin() + j);
                        ImGui::PopID();
                        break;
                    }
                    DrawBindingParams(ax.bindings[j], true);
                    ImGui::Separator();
                    ImGui::PopID();
                }

                if (ImGui::Button("＋ 割り当てを追加"))
                {
                    m_captureTarget = nullptr; // 追加で要素が移動しうるので、キャプチャの対象を外す
                    ax.bindings.push_back(BindingData());
                }
                ImGui::Unindent();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}