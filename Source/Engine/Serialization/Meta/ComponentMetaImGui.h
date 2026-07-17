#pragma once
#ifdef ENABLE_EDITOR

#include "ComponentMeta.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include <DirectXMath.h>
#include <cstring>
#include <cfloat>
#include <string>
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
    // フィールドのツールチップ文字列を組み立てる。
    //   ・tooltip（CFIELD_T の説明）があれば先頭に
    //   ・続けて「変数名: <jsonName>」を必ず付ける
    //     → 表示名を日本語にしても、ホバーすれば元の C++ 変数名が分かる。
    // ------------------------------------------------------------------------
    inline std::string BuildFieldTooltip(const FieldDescriptor& fd)
    {
        std::string t;
        if (fd.tooltip && fd.tooltip[0]) t = fd.tooltip;
        if (fd.jsonName && fd.jsonName[0]) {
            if (!t.empty()) t += "\n";
            t += "変数名: ";
            t += fd.jsonName;
        }
        if (fd.kind == FieldKind::EntityLink) {
            if (!t.empty()) t += "\n";
            t += "D&D でエンティティをアタッチ可能";
        }
        return t;
    }

    // ------------------------------------------------------------------------
    // DrawField: FieldDescriptor 1 つ分の「ウィジェット部分だけ」を描画する。
    //   ラベル(表示名)は呼び出し側が左カラムに描くので、ここでは隠しラベル "##v"
    //   をウィジェットに渡す（PushID(offset) 済みなので "##v" でも ID は一意）。
    //   幅は呼び出し側が SetNextItemWidth(-FLT_MIN) で右カラムいっぱいに広げる。
    // ------------------------------------------------------------------------
    inline bool DrawField(const FieldDescriptor& fd, void* obj)
    {
        using namespace DirectX;
        bool changed = false;
        const char* W = "##v"; // 隠しラベル

        // 読み取り専用なら以降のウィジェットを無効化（表示はされるが編集不可）
        if (fd.readonly) ImGui::BeginDisabled();

        switch (fd.kind) {
        case FieldKind::Float: {
            float& v = RawField<float>(obj, fd.offset);
            changed = ImGui::DragFloat(W, &v, fd.dragSpeed, fd.rangeMin, fd.rangeMax);
            break;
        }
        case FieldKind::Int: {
            int& v = RawField<int>(obj, fd.offset);
            changed = ImGui::DragInt(W, &v, fd.dragSpeed, (int)fd.rangeMin, (int)fd.rangeMax);
            break;
        }
        case FieldKind::Bool: {
            changed = ImGui::Checkbox(W, &RawField<bool>(obj, fd.offset));
            break;
        }
        case FieldKind::UInt8: {
            uint8_t& v = RawField<uint8_t>(obj, fd.offset);
            int      tmp = (int)v;
            if (ImGui::DragInt(W, &tmp, 1.0f, 0, 255)) { v = (uint8_t)tmp; changed = true; }
            break;
        }
        case FieldKind::UInt16: {
            uint16_t& v = RawField<uint16_t>(obj, fd.offset);
            int       tmp = (int)v;
            if (ImGui::DragInt(W, &tmp, 1.0f, 0, 65535)) { v = (uint16_t)tmp; changed = true; }
            break;
        }
        case FieldKind::UInt32: {
            uint32_t& v = RawField<uint32_t>(obj, fd.offset);
            int       tmp = (int)v;
            if (ImGui::DragInt(W, &tmp, 1.0f, 0, 4294967295)) { v = (uint32_t)tmp; changed = true; }
            break;
        }
        case FieldKind::EntityLink: {
            auto& link = RawField<CCL::Serialization::EntityLink>(obj, fd.offset);
            ImGui::InputScalar(W, ImGuiDataType_U64, &link.id);
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
            break;
        }
        case FieldKind::EnumU8: {
            uint8_t& v = RawField<uint8_t>(obj, fd.offset);
            int      cur = (int)v;
            if (fd.enumNames && fd.enumCount > 0) {
                if (ImGui::Combo(W, &cur, fd.enumNames, fd.enumCount)) { v = (uint8_t)cur; changed = true; }
            }
            else {
                int tmp = cur;
                if (ImGui::DragInt(W, &tmp, 1, 0, 255)) { v = (uint8_t)tmp; changed = true; }
            }
            break;
        }
        case FieldKind::EnumInt: {
            int& v = RawField<int>(obj, fd.offset);
            if (fd.enumNames && fd.enumCount > 0)
                changed = ImGui::Combo(W, &v, fd.enumNames, fd.enumCount);
            else
                changed = ImGui::DragInt(W, &v);
            break;
        }
        case FieldKind::Float2: {
            changed = ImGui::DragFloat2(W, &RawField<XMFLOAT2>(obj, fd.offset).x, fd.dragSpeed);
            break;
        }
        case FieldKind::Float3: {
            changed = ImGui::DragFloat3(W, &RawField<XMFLOAT3>(obj, fd.offset).x, fd.dragSpeed);
            break;
        }
        case FieldKind::Float4: {
            changed = ImGui::DragFloat4(W, &RawField<XMFLOAT4>(obj, fd.offset).x, fd.dragSpeed);
            break;
        }
        case FieldKind::Color: {
            // XMFLOAT4 を RGBA カラーピッカーで編集
            changed = ImGui::ColorEdit4(W, &RawField<XMFLOAT4>(obj, fd.offset).x);
            break;
        }
        case FieldKind::String: {
            std::string& v = RawField<std::string>(obj, fd.offset);
            char         buf[256];
            strncpy_s(buf, sizeof(buf), v.c_str(), _TRUNCATE);
            if (ImGui::InputText(W, buf, sizeof(buf))) { v = buf; changed = true; }
            break;
        }
        default: break;
        }

        if (fd.readonly) ImGui::EndDisabled();

        // ウィジェットをホバーしても「説明＋変数名」を表示（Disabled でもホバー判定は効く）
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            std::string tt = BuildFieldTooltip(fd);
            if (!tt.empty()) ImGui::SetTooltip("%s", tt.c_str());
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
            // 自動生成描画（2カラムのプロパティグリッド）
            //   ・左カラム = 表示名（固定幅・自動クリップ。ホバーで変数名ツールチップ）
            //   ・右カラム = ウィジェット（常に幅いっぱいに広げるので見切れない）
            //   ★ 同名カテゴリが離れて出現しても 1 つに統合してから描く。
            //     （連続前提で描くと同名の TreeNode/Table が 2 つでき、ImGui の
            //       ID 重複警告がホバー時に出るため）
            const auto& fields = ComponentMeta<T>::Fields();
            if (!fields.empty()) {
                void* obj = &component;

                // --- 1. カテゴリ単位でフィールドをまとめる（カテゴリの初出順を保持） ---
                struct FieldGroup {
                    const char* category = nullptr;
                    std::vector<const FieldDescriptor*> items;
                };
                std::vector<FieldGroup> groups;
                groups.reserve(8);
                for (const auto& fd : fields) {
                    const char* cat = fd.category;
                    FieldGroup* g = nullptr;
                    for (auto& cand : groups) {
                        if ((cand.category == nullptr && cat == nullptr) ||
                            (cand.category && cat && strcmp(cand.category, cat) == 0)) {
                            g = &cand; break;
                        }
                    }
                    if (!g) { groups.push_back({ cat, {} }); g = &groups.back(); }
                    g->items.push_back(&fd);
                }

                // 表示名列の幅：パネル幅の約4割（極端値は制限）。
                auto beginPropTable = [&](const char* idstr) -> bool {
                    float avail  = ImGui::GetContentRegionAvail().x;
                    float labelW = avail * 0.42f;
                    if (labelW < 90.0f)  labelW = 90.0f;
                    if (labelW > 240.0f) labelW = 240.0f;
                    if (ImGui::BeginTable(idstr, 2, ImGuiTableFlags_NoBordersInBody)) {
                        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, labelW);
                        ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch);
                        return true;
                    }
                    return false;
                };

                // 1 フィールド分の行を描く。
                auto drawRow = [&](const FieldDescriptor& fd) {
                    ImGui::PushID((int)fd.offset);
                    ImGui::TableNextRow();

                    // 左カラム：表示名（ホバーで「説明＋変数名」ツールチップ）
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(fd.name ? fd.name : "");
                    if (ImGui::IsItemHovered()) {
                        std::string tt = BuildFieldTooltip(fd);
                        if (!tt.empty()) ImGui::SetTooltip("%s", tt.c_str());
                    }

                    // 右カラム：ウィジェット（幅いっぱい）
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (DrawField(fd, obj)) anyChanged = true;

                    if (ImGui::IsItemActivated()) {
                        s_backupState = component; // ドラッグ開始時の値をバックアップ
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) { // 編集完了時に Undo 発行
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
                };

                // --- 2. グループごとに描画（各カテゴリは 1 回だけ＝ID重複しない） ---
                for (const auto& g : groups) {
                    if (g.category != nullptr) {
                        bool catOpen = ImGui::TreeNodeEx(g.category,
                            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                        if (catOpen) {
                            ImGui::Indent(12.0f);
                            if (beginPropTable(g.category)) {
                                for (const auto* fd : g.items) drawRow(*fd);
                                ImGui::EndTable();
                            }
                            ImGui::Unindent(12.0f);
                            ImGui::TreePop();
                        }
                    }
                    else {
                        // カテゴリ無し：見出しなしで並べる
                        if (beginPropTable("##rootprops")) {
                            for (const auto* fd : g.items) drawRow(*fd);
                            ImGui::EndTable();
                        }
                    }
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