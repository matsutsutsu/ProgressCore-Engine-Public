#pragma once
#ifdef ENABLE_EDITOR

#include "ComponentMeta.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include <DirectXMath.h>
#include <cstring>
#include <type_traits>
#include "Editor/Core/EditorCommandHistory.h"
#include "Engine/Platform/Core/Logger.h"
#include <imgui.h>

namespace ComponentMetaImGui {

    // ========================================================================
    //  SFINAE 1: カスタムGUI変数が定義されているかを検知
    // ========================================================================
    template <typename T, typename = void> struct CheckCustomGui : std::false_type {};

    template <typename T>
    struct CheckCustomGui<T, std::void_t<decltype(ComponentMeta<T>::hasCustomGui)>>
        : std::bool_constant<ComponentMeta<T>::hasCustomGui> {
    };

    // ========================================================================
    //  SFINAE 2: CustomGui が「3引数」で定義されているかを検知
    // ========================================================================
    template <typename T, typename = void> struct Has3ArgsCustomGui : std::false_type {};

    template <typename T>
    struct Has3ArgsCustomGui<T,
        std::void_t<decltype(ComponentMeta<T>::CustomGui(std::declval<T&>(), 0ULL, nullptr))>>
        : std::true_type {};

    // ========================================================================
    //  SFINAE 3: コンポーネントが「isDirty」という変数を持っているかを検知
    // ========================================================================
    template <typename T, typename = void> struct HasIsDirty : std::false_type {};

    template <typename T>
    struct HasIsDirty<T, std::void_t<decltype(std::declval<T>().isDirty)>> : std::true_type {};


    // ------------------------------------------------------------------------
    // DrawField: FieldDescriptor 1 つ分のインスペクタ描画
    // ------------------------------------------------------------------------
    inline bool DrawField(const FieldDescriptor& fd, void* obj)
    {
        using namespace DirectX;
        bool changed = false;

        // 読み取り専用なら以降のウィジェットを無効化（表示はされるが編集不可）
        if (fd.readonly) ImGui::BeginDisabled();

        switch (fd.kind) {
        case FieldKind::Float: {
            float& v = RawField<float>(obj, fd.offset);
            changed = ImGui::DragFloat(fd.name, &v, fd.dragSpeed, fd.rangeMin, fd.rangeMax);
            break;
        }
        case FieldKind::Int: {
            int& v = RawField<int>(obj, fd.offset);
            changed = ImGui::DragInt(fd.name, &v, fd.dragSpeed, (int)fd.rangeMin, (int)fd.rangeMax);
            break;
        }
        case FieldKind::Bool: {
            changed = ImGui::Checkbox(fd.name, &RawField<bool>(obj, fd.offset));
            break;
        }
        case FieldKind::UInt8: {
            uint8_t& v = RawField<uint8_t>(obj, fd.offset);
            int      tmp = (int)v;
            if (ImGui::DragInt(fd.name, &tmp, 1.0f, 0, 255)) { v = (uint8_t)tmp; changed = true; }
            break;
        }
        case FieldKind::UInt16: {
            uint16_t& v = RawField<uint16_t>(obj, fd.offset);
            int       tmp = (int)v;
            if (ImGui::DragInt(fd.name, &tmp, 1.0f, 0, 65535)) { v = (uint16_t)tmp; changed = true; }
            break;
        }
        case FieldKind::UInt32: {
            uint32_t& v = RawField<uint32_t>(obj, fd.offset);
            int       tmp = (int)v;
            if (ImGui::DragInt(fd.name, &tmp, 1.0f, 0, 4294967295)) { v = (uint32_t)tmp; changed = true; }
            break;
        }
        case FieldKind::EntityLink: {
            auto& link = RawField<CCL::Serialization::EntityLink>(obj, fd.offset);
            ImGui::InputScalar(fd.name, ImGuiDataType_U64, &link.id);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID")) {
                    if (payload->DataSize == sizeof(uint32_t)) {
                        link.id = *static_cast<const uint32_t*>(payload->Data); changed = true;
                    }
                    else if (payload->DataSize == sizeof(uint64_t)) {
                        link.id = *static_cast<const uint64_t*>(payload->Data); changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemHovered() && fd.tooltip == nullptr) {
                ImGui::SetTooltip("ドラッグ＆ドロップでエンティティをアタッチ可能");
            }
            break;
        }
        case FieldKind::EnumU8: {
            uint8_t& v = RawField<uint8_t>(obj, fd.offset);
            int      cur = (int)v;
            if (fd.enumNames && fd.enumCount > 0) {
                if (ImGui::Combo(fd.name, &cur, fd.enumNames, fd.enumCount)) { v = (uint8_t)cur; changed = true; }
            }
            else {
                int tmp = cur;
                if (ImGui::DragInt(fd.name, &tmp, 1, 0, 255)) { v = (uint8_t)tmp; changed = true; }
            }
            break;
        }
        case FieldKind::EnumInt: {
            int& v = RawField<int>(obj, fd.offset);
            if (fd.enumNames && fd.enumCount > 0)
                changed = ImGui::Combo(fd.name, &v, fd.enumNames, fd.enumCount);
            else
                changed = ImGui::DragInt(fd.name, &v);
            break;
        }
        case FieldKind::Float2: {
            changed = ImGui::DragFloat2(fd.name, &RawField<XMFLOAT2>(obj, fd.offset).x, fd.dragSpeed);
            break;
        }
        case FieldKind::Float3: {
            changed = ImGui::DragFloat3(fd.name, &RawField<XMFLOAT3>(obj, fd.offset).x, fd.dragSpeed);
            break;
        }
        case FieldKind::Float4: {
            changed = ImGui::DragFloat4(fd.name, &RawField<XMFLOAT4>(obj, fd.offset).x, fd.dragSpeed);
            break;
        }
        case FieldKind::Color: {
            // XMFLOAT4 を RGBA カラーピッカーで編集
            changed = ImGui::ColorEdit4(fd.name, &RawField<XMFLOAT4>(obj, fd.offset).x);
            break;
        }
        case FieldKind::String: {
            std::string& v = RawField<std::string>(obj, fd.offset);
            char         buf[256];
            strncpy_s(buf, sizeof(buf), v.c_str(), _TRUNCATE);
            if (ImGui::InputText(fd.name, buf, sizeof(buf))) { v = buf; changed = true; }
            break;
        }
        default: break;
        }

        if (fd.readonly) ImGui::EndDisabled();

        // ツールチップ（指定があればホバーで表示。Disabled 中でもホバー判定は効く）
        if (fd.tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", fd.tooltip);
        }

        // 読み取り専用は編集扱いにしない（Undo コマンドを誤発行しないため）
        if (fd.readonly) changed = false;

        return changed;
    }

    // ------------------------------------------------------------------------
    // DrawInspector: 自動描画と手動描画(CustomGui)を静的に分岐させる
    // ------------------------------------------------------------------------
    template <typename T>
    bool DrawInspector(T& component, unsigned long long entity = 0, void* world = nullptr)
    {
        bool anyChanged = false;

        // UI 操作は同時に 1 つしか行われないため、編集前状態を static で保持できる。
        static T s_backupState;

        ImGui::PushID(ComponentMeta<T>::displayName);
        ImGui::Indent(4.0f);

        if constexpr (CheckCustomGui<T>::value) {

            static bool s_isEditing = false;

            T preState = component; // 描画前の状態を控える
            bool guiChanged = false;

            if constexpr (Has3ArgsCustomGui<T>::value) {
                guiChanged = ComponentMeta<T>::CustomGui(component, entity, world);
            }
            else {
                guiChanged = ComponentMeta<T>::CustomGui(component);
            }

            if (guiChanged) {
                anyChanged = true;
                if (!s_isEditing) {
                    s_backupState = preState; // 編集開始時にバックアップを確定
                    s_isEditing = true;
                }
            }

            // クリックを離した/入力完了した瞬間に Undo コマンドを発行
            if (s_isEditing && !ImGui::IsAnyItemActive()) {
                if (world && entity != 0) {
                    auto* ecsWorld = static_cast<CCL::ECS::Core::World*>(world);
                    auto ecsEntity = static_cast<CCL::ECS::EntityID>(entity);
                    EditorCommandHistory::Instance().ExecuteCommand(
                        std::make_unique<ChangeComponentCommand<T>>(
                            ecsWorld, ecsEntity, s_backupState, component));
                }
                s_isEditing = false;
            }
        }
        else {
            // 自動生成描画
            const auto& fields = ComponentMeta<T>::Fields();
            if (!fields.empty()) {
                void* obj = &component;
                const char* curCategory = nullptr;
                bool        catOpen = true;

                for (const auto& fd : fields) {
                    if (fd.category != nullptr) {
                        if (curCategory == nullptr || strcmp(fd.category, curCategory) != 0) {
                            if (curCategory != nullptr && catOpen) {
                                ImGui::Unindent(12.0f);
                                ImGui::TreePop();
                            }
                            curCategory = fd.category;
                            catOpen = ImGui::TreeNodeEx(curCategory,
                                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                            if (catOpen) ImGui::Indent(12.0f);
                        }
                    }
                    if (!catOpen) continue;

                    ImGui::PushID((int)fd.offset);

                    bool fieldChanged = DrawField(fd, obj);
                    if (fieldChanged) anyChanged = true;

                    // ドラッグ開始時の値をバックアップ
                    if (ImGui::IsItemActivated()) {
                        s_backupState = component;
                    }
                    // 編集完了時に Undo コマンドを発行
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        if (world && entity != 0) {
                            auto* ecsWorld = static_cast<CCL::ECS::Core::World*>(world);
                            auto ecsEntity = static_cast<CCL::ECS::EntityID>(entity);
                            EditorCommandHistory::Instance().ExecuteCommand(
                                std::make_unique<ChangeComponentCommand<T>>(
                                    ecsWorld, ecsEntity, s_backupState, component));
                        }
                        else {
                            CCL_LOG_WARN(LogCategory::Editor,
                                "[UndoSystem] Failed to issue command! World or Entity is null.");
                        }
                    }

                    ImGui::PopID();
                }

                if (curCategory != nullptr && catOpen) {
                    ImGui::Unindent(12.0f);
                    ImGui::TreePop();
                }
            }
        }

        // 何か変更され、かつ isDirty を持つコンポーネントなら自動で立てる。
        if (anyChanged) {
            if constexpr (HasIsDirty<T>::value) {
                component.isDirty = true;
            }
        }

        ImGui::Unindent(4.0f);
        ImGui::PopID();
        return anyChanged;
    }

    // 登録時にポインタと ID をキャストしてリレーする。
    // category / sortPriority / hiddenInInspector は、手書き ComponentMeta<T> 特殊化で
    // 書き忘れていても (無ければ) 既定値 "Misc" / 1000 / false にフォールバックする。
    template <typename T, typename RegistryType>
    void RegisterGuiMeta(RegistryType &registry)
    {
        std::string category = "Misc";
        if constexpr (HasCategory<T>::value)
        {
            category = std::string(ComponentMeta<T>::category);
        }

        int sortPriority = 1000;
        if constexpr (HasSortPriority<T>::value)
        {
            sortPriority = ComponentMeta<T>::sortPriority;
        }

        bool hidden = false;
        if constexpr (HasHiddenInInspector<T>::value)
        {
            hidden = ComponentMeta<T>::hiddenInInspector;
        }

        registry.template Register<T>(
            std::string(ComponentMeta<T>::displayName),
            category,
            sortPriority,
            hidden,
            typename RegistryType::GuiFunc([](void *ptr, auto entity, auto *world) {
                DrawInspector<T>(*static_cast<T *>(ptr),
                    static_cast<unsigned long long>(entity),
                    static_cast<void *>(world));
            }));
    }

} // namespace ComponentMetaImGui

#endif