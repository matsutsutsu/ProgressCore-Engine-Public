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
#include "Engine/Platform/Core/Dialog.h"   // FileDialog (パスフィールドのダイアログ)
#include "Engine/Graphics/RHI/Graphics.h"  // 親ウィンドウ HWND
#include "Engine/Graphics/Resource/ResourceManager.h" // パスフィールドのサムネイルプレビュー
#include "Engine/Graphics/Resource/ResourceTypes.h"   // TextureHandle
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
    // Help: 直前のウィジェットの右に "(?)" を出し、ホバーで説明を表示する共通ヘルパー。
    //   もともと GPUParticleComponentMeta 内のローカル実装だったものを、どのメタからも
    //   使える MetaImGui の標準機能へ昇格させたもの。CustomGui 内で
    //   `ComponentMetaImGui::Help("...")` と呼ぶ（各メタで再定義しないこと）。
    // ------------------------------------------------------------------------
    inline void Help(const char* text)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (text && text[0] && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text);
    }

    // ------------------------------------------------------------------------
    // Prop: CustomGui 用の「ラベル左・ウィジェット右」行ヘルパー。
    //   自動グリッド(Fields ベース)と同じ見た目を、テーブルを組まずに1行ずつ出せる。
    //    ・左にラベルを描き、直後のウィジェット開始位置を固定列頭にそろえ、
    //     SetNextItemWidth(-FLT_MIN) で右端いっぱいに広げる。
    //   使い方（ウィジェットのラベルは "##..." で隠すこと）:
    //       ComponentMetaImGui::Prop("速度");        ImGui::DragFloat("##spd", &v);
    //       ComponentMetaImGui::Prop("色", "RGBA");  ImGui::ColorEdit4("##col", c);
    //   ※ インスペクタ全体を「ラベル左」で統一するための標準ヘルパー。
    // ------------------------------------------------------------------------
    inline void Prop(const char* label, const char* tooltip = nullptr)
    {
        // ラベル列の固定幅（自動グリッドの比率に近い値。インデント下でも列頭がそろう）。
        constexpr float kLabelWidth = 150.0f;

        const float startX = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label ? label : "");
        if (tooltip && tooltip[0] && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);

        ImGui::SameLine();
        ImGui::SetCursorPosX(startX + kLabelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN); // 直後のウィジェットを右端まで広げる
    }

    // ========================================================================
    //  1行フィールドヘルパー（CustomGui を「1行1フィールド」で書くための糖衣）
    //    ・Prop(ラベル左) → 隠しラベルのウィジェット → changed を返す、を1関数に集約。
    //    ・ID は値ポインタ(v)で一意化するので、同名ラベルが並んでも衝突しない。
    //    ・使い方（CustomGui 内）:
    //        namespace mi = ComponentMetaImGui;
    //        changed |= mi::DragFloat("強度", &v, 0.01f, 0.0f, 1.0f);
    //        changed |= mi::Checkbox("有効", &b);
    //        changed |= mi::Color3("色", &c.x);
    //    これだけで自動グリッドと同じ「ラベル左・幅いっぱい」に揃う。
    // ========================================================================
    inline bool DragFloat(const char* label, float* v, float speed = 0.1f,
                          float lo = 0.0f, float hi = 0.0f, const char* tip = nullptr)
    {
        ImGui::PushID(v); Prop(label, tip);
        bool ch = ImGui::DragFloat("##v", v, speed, lo, hi); ImGui::PopID(); return ch;
    }
    inline bool DragFloat2(const char* label, float* v2, float speed = 0.1f,
                           float lo = 0.0f, float hi = 0.0f, const char* tip = nullptr)
    {
        ImGui::PushID(v2); Prop(label, tip);
        bool ch = ImGui::DragFloat2("##v", v2, speed, lo, hi); ImGui::PopID(); return ch;
    }
    inline bool DragFloat3(const char* label, float* v3, float speed = 0.1f,
                           float lo = 0.0f, float hi = 0.0f, const char* tip = nullptr)
    {
        ImGui::PushID(v3); Prop(label, tip);
        bool ch = ImGui::DragFloat3("##v", v3, speed, lo, hi); ImGui::PopID(); return ch;
    }
    inline bool DragFloat4(const char* label, float* v4, float speed = 0.1f,
                           float lo = 0.0f, float hi = 0.0f, const char* tip = nullptr)
    {
        ImGui::PushID(v4); Prop(label, tip);
        bool ch = ImGui::DragFloat4("##v", v4, speed, lo, hi); ImGui::PopID(); return ch;
    }
    inline bool DragInt(const char* label, int* v, float speed = 1.0f,
                        int lo = 0, int hi = 0, const char* tip = nullptr)
    {
        ImGui::PushID(v); Prop(label, tip);
        bool ch = ImGui::DragInt("##v", v, speed, lo, hi); ImGui::PopID(); return ch;
    }
    inline bool SliderFloat(const char* label, float* v, float lo, float hi, const char* tip = nullptr)
    {
        ImGui::PushID(v); Prop(label, tip);
        bool ch = ImGui::SliderFloat("##v", v, lo, hi); ImGui::PopID(); return ch;
    }
    inline bool Checkbox(const char* label, bool* v, const char* tip = nullptr)
    {
        ImGui::PushID(v); Prop(label, tip);
        bool ch = ImGui::Checkbox("##v", v); ImGui::PopID(); return ch;
    }
    inline bool Color3(const char* label, float* rgb, const char* tip = nullptr)
    {
        ImGui::PushID(rgb); Prop(label, tip);
        bool ch = ImGui::ColorEdit3("##v", rgb); ImGui::PopID(); return ch;
    }
    inline bool Color4(const char* label, float* rgba, const char* tip = nullptr)
    {
        ImGui::PushID(rgba); Prop(label, tip);
        bool ch = ImGui::ColorEdit4("##v", rgba); ImGui::PopID(); return ch;
    }
    inline bool Combo(const char* label, int* cur, const char* const items[], int count, const char* tip = nullptr)
    {
        ImGui::PushID(cur); Prop(label, tip);
        bool ch = ImGui::Combo("##v", cur, items, count); ImGui::PopID(); return ch;
    }
    // std::string を直接編集する InputText（内部でバッファを管理。call 側の char[] 定型を排除）。
    inline bool InputText(const char* label, std::string& s, const char* tip = nullptr, size_t maxLen = 256)
    {
        ImGui::PushID(&s); Prop(label, tip);
        char buf[256];
        strncpy_s(buf, sizeof(buf), s.c_str(), _TRUNCATE);
        bool ch = ImGui::InputText("##v", buf, (maxLen < sizeof(buf)) ? maxLen : sizeof(buf));
        if (ch) s = buf;
        ImGui::PopID();
        return ch;
    }

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
            // DragInt は min/max が int のため uint32 の上限(4294967295)を渡せない。
            //    渡すと -1 に切り捨てられて min>max となり、クランプが無効化されるうえ
            //    int 経由の表示で INT_MAX 超の値(ハッシュ等)が負数として見えてしまう。
            //    uint32 のまま扱える DragScalar を使う。
            constexpr uint32_t kMin = 0u;
            constexpr uint32_t kMax = 0xFFFFFFFFu;
            changed = ImGui::DragScalar(W, ImGuiDataType_U32, &v, 1.0f, &kMin, &kMax);
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

            // filePreset 指定時は「サムネイル＋パス欄＋[選択][クリア]」。直打ちも残す。
            if (fd.filePreset) {
                // 現在のパスからプレビュー用SRVを解決（ResourceManager はパスでキャッシュ）。
                ID3D11ShaderResourceView* srv = nullptr;
                if (!v.empty()) {
                    TextureHandle h = ResourceManager::Instance().LoadTexture(v.c_str());
                    if (h.IsValid()) srv = ResourceManager::Instance().GetTexture(h);
                }

                // サムネイル（2行分の高さ）。ホバーで拡大プレビュー＋パス表示。
                const float thumb = ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight();
                if (srv) {
                    ImTextureID tid = (ImTextureID)(uintptr_t)srv;
                    ImGui::Image(tid, ImVec2(thumb, thumb));
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Image(tid, ImVec2(192.0f, 192.0f));
                        ImGui::TextUnformatted(v.c_str());
                        ImGui::EndTooltip();
                    }
                }
                else {
                    // 未設定 / 読込失敗はプレースホルダ枠。
                    ImGui::Button(v.empty() ? "無し" : "?", ImVec2(thumb, thumb));
                }

                ImGui::SameLine();
                ImGui::BeginGroup();
                // パス（直打ち可能・幅いっぱい）
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText("##v", buf, sizeof(buf))) { v = buf; changed = true; }
                // ボタン行：ファイルダイアログを開く / クリア
                if (ImGui::Button("選択")) {
                    if (auto path = FileDialog::OpenPreset(fd.filePreset, "ファイルを選択",
                                                           Graphics::Instance().GetWindowHandle())) {
                        v = FileDialog::ConvertToRelativePath(*path);
                        changed = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("クリア")) { v.clear(); changed = true; }
                ImGui::EndGroup();
            }
            else {
                if (ImGui::InputText(W, buf, sizeof(buf))) { v = buf; changed = true; }
            }
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
                    auto* ecsWorld = static_cast<CCL::ECS::World*>(world);
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
            //   同名カテゴリが離れて出現しても 1 つに統合してから描く。
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
                            auto* ecsWorld = static_cast<CCL::ECS::World*>(world);
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