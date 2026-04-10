#pragma once
#include "ComponentMeta.h"
#include <DirectXMath.h>
#include <cstring>
#include <imgui.h>
#include <type_traits> // メタプログラミング用
#include "Editor/Core/EditorCommandHistory.h"

namespace ComponentMetaImGui {

    // ========================================================================
    //  SFINAE 1: カスタムGUI変数が定義されているかを検知
    // ========================================================================
    template <typename T, typename = void> struct CheckCustomGui : std::false_type {};

    template <typename T>
    struct CheckCustomGui<T, std::void_t<decltype(ComponentMeta<T>::hasCustomGui)>>
        : std::bool_constant<ComponentMeta<T>::hasCustomGui> {};

    // ========================================================================
    //  SFINAE 2: CustomGui が「3引数」で定義されているかを検知
    // ========================================================================
    template <typename T, typename = void> struct Has3ArgsCustomGui : std::false_type {};

    template <typename T>
    struct Has3ArgsCustomGui<T,
        std::void_t<decltype(ComponentMeta<T>::CustomGui(std::declval<T &>(), 0ULL, nullptr))>>
        : std::true_type {};

    // ========================================================================
    //  SFINAE 3: コンポーネントが「isDirty」という変数を持っているかを検知
    // ========================================================================
    template <typename T, typename = void> struct HasIsDirty : std::false_type {};

    template <typename T>
    struct HasIsDirty<T, std::void_t<decltype(std::declval<T>().isDirty)>> : std::true_type {};


    // ------------------------------------------------------------------------
    // DrawField 関数は既存のまま完全に維持 (変更なし)
    // ------------------------------------------------------------------------
    inline bool DrawField(const FieldDescriptor &fd, void *obj)
    {
        using namespace DirectX;
        bool changed = false;
        switch (fd.kind) {
        case FieldKind::Float: {
            float &v = RawField<float>(obj, fd.offset);

            // 第4引数(min)と第5引数(max)を渡すことで、ドラッグ操作のまま上限・下限をロックできる。
            changed = ImGui::DragFloat(fd.name, &v, fd.dragSpeed, fd.rangeMin, fd.rangeMax);
            break;
        }
        case FieldKind::Int: {
            int &v = RawField<int>(obj, fd.offset);

            changed = ImGui::DragInt(fd.name, &v, fd.dragSpeed, (int)fd.rangeMin, (int)fd.rangeMax);
            break;
        }
        case FieldKind::Bool: {
            changed = ImGui::Checkbox(fd.name, &RawField<bool>(obj, fd.offset));
            break;
        }
        case FieldKind::UInt8: {
            uint8_t &v   = RawField<uint8_t>(obj, fd.offset);
            int      tmp = (int)v;
            if (ImGui::DragInt(fd.name, &tmp, 1.0f, 0, 255)) {
                v       = (uint8_t)tmp;
                changed = true;
            }
            break;
        }
        case FieldKind::EnumU8: {
            uint8_t &v   = RawField<uint8_t>(obj, fd.offset);
            int      cur = (int)v;
            if (fd.enumNames && fd.enumCount > 0) {
                if (ImGui::Combo(fd.name, &cur, fd.enumNames, fd.enumCount)) {
                    v       = (uint8_t)cur;
                    changed = true;
                }
            }
            else {
                int tmp = cur;
                if (ImGui::DragInt(fd.name, &tmp, 1, 0, 255)) {
                    v       = (uint8_t)tmp;
                    changed = true;
                }
            }
            break;
        }
        case FieldKind::EnumInt: {
            int &v = RawField<int>(obj, fd.offset);
            if (fd.enumNames && fd.enumCount > 0)
                changed = ImGui::Combo(fd.name, &v, fd.enumNames, fd.enumCount);
            else
                changed = ImGui::DragInt(fd.name, &v);
            break;
        }
        case FieldKind::Float2: {
            changed =
                ImGui::DragFloat2(fd.name, &RawField<XMFLOAT2>(obj, fd.offset).x, fd.dragSpeed);
            break;
        }
        case FieldKind::Float3: {
            changed =
                ImGui::DragFloat3(fd.name, &RawField<XMFLOAT3>(obj, fd.offset).x, fd.dragSpeed);
            break;
        }
        case FieldKind::Float4: {
            changed =
                ImGui::DragFloat4(fd.name, &RawField<XMFLOAT4>(obj, fd.offset).x, fd.dragSpeed);
            break;
        }
        case FieldKind::String: {
            std::string &v = RawField<std::string>(obj, fd.offset);
            char         buf[256];
            strncpy_s(buf, sizeof(buf), v.c_str(), _TRUNCATE);
            if (ImGui::InputText(fd.name, buf, sizeof(buf))) {
                v       = buf;
                changed = true;
            }
            break;
        }
        default: break;
        }
        return changed;
    }

    // ------------------------------------------------------------------------
    // DrawInspector: 自動描画と手動描画(CustomGui)を静的に分岐させる
    // ------------------------------------------------------------------------
    template <typename T>
    bool DrawInspector(T &component, unsigned long long entity = 0, void *world = nullptr)
    {
        bool anyChanged = false;

        // ラッグ開始時の状態を保持するスタティック変数
        // UI操作は同時に1つしかできないため、staticで安全に保持できます
        static T s_backupState;

        ImGui::PushID(ComponentMeta<T>::displayName);
        ImGui::Indent(4.0f);

        //  自動描画か手動描画かをコンパイル時に分岐
        if constexpr (CheckCustomGui<T>::value) {
            
            //  SFINAE分岐: 3引数版が存在すればそちらを呼び、なければ1引数版を呼ぶ
            if constexpr (Has3ArgsCustomGui<T>::value) {
                anyChanged = ComponentMeta<T>::CustomGui(component, entity, world);
            } else {
                anyChanged = ComponentMeta<T>::CustomGui(component);
            }
            
        }
        else {
            // カスタムGUIがない場合は、従来通りの自動生成描画を実行する
            const auto &fields = ComponentMeta<T>::Fields();
            if (!fields.empty()) {
                void       *obj         = &component;
                const char *curCategory = nullptr;
                bool        catOpen     = true;

                for (const auto &fd : fields) {
                    if (fd.category != nullptr) {
                        if (curCategory == nullptr || strcmp(fd.category, curCategory) != 0) {
                            if (curCategory != nullptr && catOpen) {
                                ImGui::Unindent(12.0f);
                                ImGui::TreePop();
                            }
                            curCategory = fd.category;
                            catOpen     = ImGui::TreeNodeEx(curCategory,
                                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                            if (catOpen) ImGui::Indent(12.0f);
                        }
                    }
                    if (!catOpen) continue;

                    ImGui::PushID((int)fd.offset);

                    // ==========================================================
                    // 描画とUndo/Redoフックの統合
                    // ==========================================================
                    bool fieldChanged = DrawField(fd, obj);
                    if (fieldChanged) anyChanged = true;

                    // 1. スライダーをクリックした瞬間（ドラッグ開始前）の値をバックアップ
                    if (ImGui::IsItemActivated()) {
                        s_backupState = component; 
                    }
                    
                    // 2. マウスを離した瞬間（編集完了）にコマンドを発行！
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        if (world && entity != 0) {
                            auto* ecsWorld = static_cast<CCL::ECS::Core::World*>(world);
                            auto ecsEntity = static_cast<CCL::ECS::EntityID>(entity);

                            EditorCommandHistory::Instance().ExecuteCommand(
                                std::make_unique<ChangeComponentCommand<T>>(
                                    ecsWorld, ecsEntity, s_backupState, component
                                )
                            );
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

        // ========================================================================
        // ★最強の自動化: 
        // どの値でもいいから変更され(anyChanged == true)、かつ
        // このコンポーネントが isDirty 変数を持っているなら、強制的に true を叩き込む！
        // ========================================================================
        if (anyChanged) {
            if constexpr (HasIsDirty<T>::value) {
                component.isDirty = true;
            }
        }

        ImGui::Unindent(4.0f);
        ImGui::PopID();
        return anyChanged;
    }

    // 登録時にポインタとIDをキャストしてリレーする
    template <typename T, typename RegistryType> void RegisterGuiMeta(RegistryType &registry)
    {
        registry.template Register<T>(
            std::string(
                ComponentMeta<T>::displayName), // const char* を std::string に明示的キャスト
            typename RegistryType::GuiFunc([](void *ptr, auto entity, auto *world) {
                // DrawInspector<T> と明示し、引数も全てキャストして渡す
                DrawInspector<T>(*static_cast<T *>(ptr),
                    static_cast<unsigned long long>(entity),
                    static_cast<void *>(world));
            }));
    }

} // namespace ComponentMetaImGui