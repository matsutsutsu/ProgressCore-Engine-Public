#pragma once
#include "ComponentMeta.h"
#include <DirectXMath.h>
#include <cstring>
#include <imgui.h>

namespace ComponentMetaImGui {
    inline bool DrawField(const FieldDescriptor &fd, void *obj)
    {
        using namespace DirectX;
        bool changed = false;
        switch (fd.kind) {
        case FieldKind::Float: {
            float &v = RawField<float>(obj, fd.offset);
            if (fd.rangeMax != 0.0f || fd.rangeMin != 0.0f)
                changed = ImGui::SliderFloat(fd.name, &v, fd.rangeMin, fd.rangeMax);
            else
                changed = ImGui::DragFloat(fd.name, &v, fd.dragSpeed);
            break;
        }
        case FieldKind::Int: {
            int &v = RawField<int>(obj, fd.offset);
            if (fd.rangeMax != 0.0f || fd.rangeMin != 0.0f) {
                int lo = (int)fd.rangeMin, hi = (int)fd.rangeMax;
                changed = ImGui::SliderInt(fd.name, &v, lo, hi);
            }
            else
                changed = ImGui::DragInt(fd.name, &v, (int)fd.dragSpeed);
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

    template <typename T> bool DrawInspector(T &component, const char *overrideName = nullptr)
    {
        // 1. 險ｭ險亥峙繧偵ｂ繧峨≧
        const auto &fields = ComponentMeta<T>::Fields();
        if (fields.empty()) return false;

        bool  anyChanged = false;
        void *obj        = &component;

        ImGui::PushID(ComponentMeta<T>::displayName);

        // 蟆代＠繧､繝ｳ繝・Φ繝医ｒ荳九￡縺ｦ隕九◆逶ｮ繧堤ｾ弱＠縺上☆繧・
        ImGui::Indent(4.0f);

        const char *curCategory = nullptr;
        bool        catOpen     = true;

        // 2. 險ｭ險亥峙縺ｮ繝ｪ繧ｹ繝医ｒ荳翫°繧蛾・↓繝ｫ繝ｼ繝励☆繧・
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
            // 2. 險ｭ險亥峙縺ｮ繝ｪ繧ｹ繝医ｒ荳翫°繧蛾・↓繝ｫ繝ｼ繝励☆繧・
            if (DrawField(fd, obj)) anyChanged = true;
            ImGui::PopID();
        }

        if (curCategory != nullptr && catOpen) {
            ImGui::Unindent(12.0f);
            ImGui::TreePop();
        }

        ImGui::Unindent(4.0f);
        ImGui::PopID();
        return anyChanged;
    }

    template <typename T, typename RegistryType> void RegisterGuiMeta(RegistryType &registry)
    {
        registry.template Register<T>(ComponentMeta<T>::displayName,
            [](void *ptr, auto entity, auto *world) { DrawInspector(*static_cast<T *>(ptr)); });
    }
} // namespace ComponentMetaImGui