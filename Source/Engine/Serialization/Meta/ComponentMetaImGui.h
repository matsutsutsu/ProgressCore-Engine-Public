#pragma once
// ===========================================================================
// 概要: ComponentMeta を使ってコンポーネントをインスペクタに描く関数群（エディタ構成のみ）。
//
// 責務:
//   - Fields() からの自動描画（カテゴリごとの 2 列の表）と、CustomGui への切り替え
//   - 編集の開始・終了を検出して Undo コマンドを積む
//   - CustomGui を書くための 1 行ヘルパー（Prop / DragFloat / Checkbox など）
//   - ComponentGuiRegistry への登録（RegisterGuiMeta）
// ===========================================================================
#ifdef ENABLE_EDITOR

#include "ComponentMeta.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cstring>
#include <cfloat>
#include <string>
#include <type_traits>
#include "Editor/Core/EditorCommandHistory.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/Dialog.h"              // パスのフィールドのファイルダイアログ
#include "Engine/Graphics/RHI/Graphics.h"             // ダイアログの親ウィンドウ
#include "Engine/Graphics/Resource/ResourceManager.h" // パスのフィールドのサムネイル
#include "Engine/Graphics/Resource/ResourceTypes.h"   // TextureHandle
#include <imgui.h>

namespace ComponentMetaImGui
{

    // ===========================================================================
    // SFINAE（ComponentMeta<T> / T が持つメンバーを調べる）
    // ===========================================================================

    // hasCustomGui が定義されていて true か
    template <typename T, typename = void>
    struct CheckCustomGui : std::false_type
    {
    };

    template <typename T>
    struct CheckCustomGui<T, std::void_t<decltype(ComponentMeta<T>::hasCustomGui)>>
        : std::bool_constant<ComponentMeta<T>::hasCustomGui>
    {
    };

    // CustomGui が 3 引数（component, entityID, world）で呼べるか
    template <typename T, typename = void>
    struct Has3ArgsCustomGui : std::false_type
    {
    };

    template <typename T>
    struct Has3ArgsCustomGui<T,
                             std::void_t<decltype(ComponentMeta<T>::CustomGui(std::declval<T &>(), 0ULL, nullptr))>>
        : std::true_type
    {
    };

    // コンポーネントが isDirty を持つか（編集したら自動で立てる）
    template <typename T, typename = void>
    struct HasIsDirty : std::false_type
    {
    };

    template <typename T>
    struct HasIsDirty<T, std::void_t<decltype(std::declval<T>().isDirty)>> : std::true_type
    {
    };

    // ===========================================================================
    // CustomGui 用のヘルパー
    // ===========================================================================

    // 直前のウィジェットの右に "(?)" を出し、ホバーで説明を表示する（各メタで再定義しないこと）
    inline void Help(const char *text)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (text && text[0] && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text);
    }

    /**
     * @brief 「ラベル左・ウィジェット右」の 1 行を始める。自動描画の表と同じ見た目を、表を組まずに出せる。
     * @note 左にラベルを描き、直後のウィジェットの開始位置を固定の列にそろえて右端まで広げる。
     *       ウィジェットのラベルは "##..." で隠すこと。
     * @code
     *   ComponentMetaImGui::Prop("速度");       ImGui::DragFloat("##spd", &v);
     *   ComponentMetaImGui::Prop("色", "RGBA"); ImGui::ColorEdit4("##col", c);
     * @endcode
     */
    inline void Prop(const char *label, const char *tooltip = nullptr)
    {
        // ラベル列の幅（自動描画の表に近い値。インデントされていても列がそろう）
        constexpr float kLabelWidth = 150.0f;

        const float startX = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label ? label : "");
        if (tooltip && tooltip[0] && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);

        ImGui::SameLine();

        // 列の位置にそろえるが、左へは戻さない。
        //   SetCursorPosX は絶対位置なので、kLabelWidth より長いラベル（約 300px のものがある）では
        //   カーソルが左へ戻り、ウィジェットがラベルに重なる。長いラベルの行はその分右へずらす
        const float alignedX = startX + kLabelWidth;
        ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), alignedX));

        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // 1 行 1 フィールドのヘルパー。Prop → ラベルを隠したウィジェット → 変更したかを返す、を 1 関数で行う。
    //   ID は値のポインタで作るので、同じラベルが並んでも衝突しない。
    //     namespace mi = ComponentMetaImGui;
    //     changed |= mi::DragFloat("強度", &v, 0.01f, 0.0f, 1.0f);
    //     changed |= mi::Checkbox("有効", &b);
    inline bool DragFloat(const char *label, float *v, float speed = 0.1f,
                          float lo = 0.0f, float hi = 0.0f, const char *tip = nullptr)
    {
        ImGui::PushID(v);
        Prop(label, tip);
        bool ch = ImGui::DragFloat("##v", v, speed, lo, hi);
        ImGui::PopID();
        return ch;
    }
    inline bool DragFloat2(const char *label, float *v2, float speed = 0.1f,
                           float lo = 0.0f, float hi = 0.0f, const char *tip = nullptr)
    {
        ImGui::PushID(v2);
        Prop(label, tip);
        bool ch = ImGui::DragFloat2("##v", v2, speed, lo, hi);
        ImGui::PopID();
        return ch;
    }
    inline bool DragFloat3(const char *label, float *v3, float speed = 0.1f,
                           float lo = 0.0f, float hi = 0.0f, const char *tip = nullptr)
    {
        ImGui::PushID(v3);
        Prop(label, tip);
        bool ch = ImGui::DragFloat3("##v", v3, speed, lo, hi);
        ImGui::PopID();
        return ch;
    }
    inline bool DragFloat4(const char *label, float *v4, float speed = 0.1f,
                           float lo = 0.0f, float hi = 0.0f, const char *tip = nullptr)
    {
        ImGui::PushID(v4);
        Prop(label, tip);
        bool ch = ImGui::DragFloat4("##v", v4, speed, lo, hi);
        ImGui::PopID();
        return ch;
    }
    inline bool DragInt(const char *label, int *v, float speed = 1.0f,
                        int lo = 0, int hi = 0, const char *tip = nullptr)
    {
        ImGui::PushID(v);
        Prop(label, tip);
        bool ch = ImGui::DragInt("##v", v, speed, lo, hi);
        ImGui::PopID();
        return ch;
    }
    inline bool SliderFloat(const char *label, float *v, float lo, float hi, const char *tip = nullptr)
    {
        ImGui::PushID(v);
        Prop(label, tip);
        bool ch = ImGui::SliderFloat("##v", v, lo, hi);
        ImGui::PopID();
        return ch;
    }
    inline bool Checkbox(const char *label, bool *v, const char *tip = nullptr)
    {
        ImGui::PushID(v);
        Prop(label, tip);
        bool ch = ImGui::Checkbox("##v", v);
        ImGui::PopID();
        return ch;
    }
    inline bool Color3(const char *label, float *rgb, const char *tip = nullptr)
    {
        ImGui::PushID(rgb);
        Prop(label, tip);
        bool ch = ImGui::ColorEdit3("##v", rgb);
        ImGui::PopID();
        return ch;
    }
    inline bool Color4(const char *label, float *rgba, const char *tip = nullptr)
    {
        ImGui::PushID(rgba);
        Prop(label, tip);
        bool ch = ImGui::ColorEdit4("##v", rgba);
        ImGui::PopID();
        return ch;
    }
    inline bool Combo(const char *label, int *cur, const char *const items[], int count, const char *tip = nullptr)
    {
        ImGui::PushID(cur);
        Prop(label, tip);
        bool ch = ImGui::Combo("##v", cur, items, count);
        ImGui::PopID();
        return ch;
    }
    // std::string を直接編集する InputText（呼び出し側で char[] を用意しなくてよい）
    inline bool InputText(const char *label, std::string &s, const char *tip = nullptr, size_t maxLen = 256)
    {
        ImGui::PushID(&s);
        Prop(label, tip);
        char buf[256];
        strncpy_s(buf, sizeof(buf), s.c_str(), _TRUNCATE);
        bool ch = ImGui::InputText("##v", buf, (maxLen < sizeof(buf)) ? maxLen : sizeof(buf));
        if (ch) s = buf;
        ImGui::PopID();
        return ch;
    }

    // ===========================================================================
    // 自動描画
    // ===========================================================================

    // フィールドのツールチップ。説明（CFIELD_T）の後に「変数名: <jsonName>」を必ず付け、
    // 表示名が日本語でもホバーで C++ の変数名が分かるようにする
    inline std::string BuildFieldTooltip(const FieldDescriptor &fd)
    {
        std::string t;
        if (fd.tooltip && fd.tooltip[0]) t = fd.tooltip;
        if (fd.jsonName && fd.jsonName[0])
        {
            if (!t.empty()) t += "\n";
            t += "変数名: ";
            t += fd.jsonName;
        }
        if (fd.kind == FieldKind::EntityLink)
        {
            if (!t.empty()) t += "\n";
            t += "D&D でエンティティをアタッチ可能";
        }
        return t;
    }

    // FieldDescriptor 1 つ分のウィジェットだけを描く。変更したら true。
    //   ラベルと幅は呼び出し側が決める（PushID(offset) 済みなので "##v" でも ID は一意）
    inline bool DrawField(const FieldDescriptor &fd, void *obj)
    {
        using namespace DirectX;
        bool        changed = false;
        const char *W       = "##v"; // 隠しラベル

        // 読み取り専用なら表示だけにする
        if (fd.readonly) ImGui::BeginDisabled();

        switch (fd.kind)
        {
        case FieldKind::Float: {
            float &v = RawField<float>(obj, fd.offset);
            changed  = ImGui::DragFloat(W, &v, fd.dragSpeed, fd.rangeMin, fd.rangeMax);
            break;
        }
        case FieldKind::Int: {
            int &v  = RawField<int>(obj, fd.offset);
            changed = ImGui::DragInt(W, &v, fd.dragSpeed, (int)fd.rangeMin, (int)fd.rangeMax);
            break;
        }
        case FieldKind::Bool: {
            changed = ImGui::Checkbox(W, &RawField<bool>(obj, fd.offset));
            break;
        }
        case FieldKind::UInt8: {
            uint8_t &v   = RawField<uint8_t>(obj, fd.offset);
            int      tmp = (int)v;
            if (ImGui::DragInt(W, &tmp, 1.0f, 0, 255))
            {
                v       = (uint8_t)tmp;
                changed = true;
            }
            break;
        }
        case FieldKind::UInt16: {
            uint16_t &v   = RawField<uint16_t>(obj, fd.offset);
            int       tmp = (int)v;
            if (ImGui::DragInt(W, &tmp, 1.0f, 0, 65535))
            {
                v       = (uint16_t)tmp;
                changed = true;
            }
            break;
        }
        case FieldKind::UInt32: {
            uint32_t &v = RawField<uint32_t>(obj, fd.offset);
            // DragInt は min/max が int なので uint32 の上限を渡せない（-1 になり min>max でクランプが効かず、
            // INT_MAX を超える値（ハッシュ等）が負数に見える）。uint32 のまま扱える DragScalar を使う
            constexpr uint32_t kMin = 0u;
            constexpr uint32_t kMax = 0xFFFFFFFFu;
            changed                 = ImGui::DragScalar(W, ImGuiDataType_U32, &v, 1.0f, &kMin, &kMax);
            break;
        }
        case FieldKind::EntityLink: {
            auto &link = RawField<CCL::Serialization::EntityLink>(obj, fd.offset);
            ImGui::InputScalar(W, ImGuiDataType_U64, &link.id);
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID"))
                {
                    if (payload->DataSize == sizeof(uint32_t))
                    {
                        link.id = *static_cast<const uint32_t *>(payload->Data);
                        changed = true;
                    }
                    else if (payload->DataSize == sizeof(uint64_t))
                    {
                        link.id = *static_cast<const uint64_t *>(payload->Data);
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            break;
        }
        case FieldKind::EnumU8: {
            uint8_t &v   = RawField<uint8_t>(obj, fd.offset);
            int      cur = (int)v;
            if (fd.enumNames && fd.enumCount > 0)
            {
                if (ImGui::Combo(W, &cur, fd.enumNames, fd.enumCount))
                {
                    v       = (uint8_t)cur;
                    changed = true;
                }
            }
            else
            {
                int tmp = cur;
                if (ImGui::DragInt(W, &tmp, 1, 0, 255))
                {
                    v       = (uint8_t)tmp;
                    changed = true;
                }
            }
            break;
        }
        case FieldKind::EnumInt: {
            int &v = RawField<int>(obj, fd.offset);
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
            // XMFLOAT4 を RGBA のカラーピッカーで編集する
            changed = ImGui::ColorEdit4(W, &RawField<XMFLOAT4>(obj, fd.offset).x);
            break;
        }
        case FieldKind::String: {
            std::string &v = RawField<std::string>(obj, fd.offset);
            char         buf[256];
            strncpy_s(buf, sizeof(buf), v.c_str(), _TRUNCATE);

            // filePreset 指定時は「サムネイル + パス欄 + [選択][クリア]」。直接入力もできる
            if (fd.filePreset)
            {
                // 今のパスのテクスチャを取得する（ResourceManager がパスでキャッシュする）
                ID3D11ShaderResourceView *srv = nullptr;
                if (!v.empty())
                {
                    TextureHandle h = ResourceManager::Instance().LoadTexture(v.c_str());
                    if (h.IsValid()) srv = ResourceManager::Instance().GetTexture(h);
                }

                // サムネイル（2 行分の高さ）。ホバーで拡大表示とパスを出す
                const float thumb = ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight();
                if (srv)
                {
                    ImTextureID tid = (ImTextureID)(uintptr_t)srv;
                    ImGui::Image(tid, ImVec2(thumb, thumb));
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Image(tid, ImVec2(192.0f, 192.0f));
                        ImGui::TextUnformatted(v.c_str());
                        ImGui::EndTooltip();
                    }
                }
                else
                {
                    // 未設定・読込失敗は空の枠を出す
                    ImGui::Button(v.empty() ? "無し" : "?", ImVec2(thumb, thumb));
                }

                ImGui::SameLine();
                ImGui::BeginGroup();
                // パスの入力欄（幅いっぱい）
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText("##v", buf, sizeof(buf)))
                {
                    v       = buf;
                    changed = true;
                }
                // ファイルダイアログを開くボタンと、クリアボタン
                if (ImGui::Button("選択"))
                {
                    if (auto path = FileDialog::OpenPreset(fd.filePreset, "ファイルを選択",
                                                           Graphics::Instance().GetWindowHandle()))
                    {
                        v       = FileDialog::ConvertToRelativePath(*path);
                        changed = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("クリア"))
                {
                    v.clear();
                    changed = true;
                }
                ImGui::EndGroup();
            }
            else
            {
                if (ImGui::InputText(W, buf, sizeof(buf)))
                {
                    v       = buf;
                    changed = true;
                }
            }
            break;
        }
        default:
            break;
        }

        if (fd.readonly) ImGui::EndDisabled();

        // ウィジェットのホバーでも「説明 + 変数名」を出す（無効化されていてもホバーは判定できる）
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            std::string tt = BuildFieldTooltip(fd);
            if (!tt.empty()) ImGui::SetTooltip("%s", tt.c_str());
        }

        // 読み取り専用は編集扱いにしない（Undo コマンドを積まないため）
        if (fd.readonly) changed = false;

        return changed;
    }

    // コンポーネント 1 つをインスペクタに描く。hasCustomGui なら CustomGui、そうでなければ Fields() から自動で描く
    template <typename T>
    bool DrawInspector(T &component, unsigned long long entity = 0, void *world = nullptr)
    {
        bool anyChanged = false;

        // UI の操作は同時に 1 つなので、編集前の状態は static で持てる。
        //   ただし選択の切り替わりは防げない。CustomGui 側は IsAnyItemActive() が false になるまで
        //   Undo の記録を遅らせるので、その間に別の Entity を選ぶと「A のバックアップを B に」という
        //   コマンドが積まれ、Undo で B に A の値が書き込まれる。持ち主の EntityID も控え、食い違ったら捨てる
        static T                  s_backupState;
        static unsigned long long s_backupOwner = 0;

        ImGui::PushID(ComponentMeta<T>::displayName);
        ImGui::Indent(4.0f);

        if constexpr (CheckCustomGui<T>::value)
        {
            static bool s_isEditing = false;

            T    preState   = component; // 描画前の状態
            bool guiChanged = false;

            if constexpr (Has3ArgsCustomGui<T>::value)
            {
                guiChanged = ComponentMeta<T>::CustomGui(component, entity, world);
            }
            else
            {
                guiChanged = ComponentMeta<T>::CustomGui(component);
            }

            // 編集の途中で選択が別の Entity へ移ったら、その編集は捨てる（別の Entity のバックアップを使わないため）
            if (s_isEditing && s_backupOwner != entity)
            {
                s_isEditing = false;
            }

            if (guiChanged)
            {
                anyChanged = true;
                if (!s_isEditing)
                {
                    s_backupState = preState; // 編集を始めた時点の値を控える
                    s_backupOwner = entity;
                    s_isEditing   = true;
                }
            }

            // 操作を終えた瞬間（クリックを離す・入力を確定する）に Undo コマンドを積む
            if (s_isEditing && !ImGui::IsAnyItemActive())
            {
                if (world && entity != 0 && s_backupOwner == entity)
                {
                    auto *ecsWorld  = static_cast<CCL::ECS::World *>(world);
                    auto  ecsEntity = static_cast<CCL::ECS::EntityID>(entity);
                    EditorCommandHistory::Instance().ExecuteCommand(
                        std::make_unique<ChangeComponentCommand<T>>(
                            ecsWorld, ecsEntity, s_backupState, component));
                }
                s_isEditing = false;
            }
        }
        else
        {
            // Fields() から 2 列の表で描く（左: 表示名、右: ウィジェットを幅いっぱい）。
            //   同じカテゴリが離れて出てきても 1 つにまとめてから描く。まとめないと同じ名前の
            //   TreeNode / Table が 2 つでき、ホバー時に ImGui の ID 重複の警告が出る
            const auto &fields = ComponentMeta<T>::Fields();
            if (!fields.empty())
            {
                void *obj = &component;

                // 1. カテゴリごとにフィールドをまとめる（カテゴリが最初に出た順を保つ）
                struct FieldGroup
                {
                    const char                          *category = nullptr;
                    std::vector<const FieldDescriptor *> items;
                };
                std::vector<FieldGroup> groups;
                groups.reserve(8);
                for (const auto &fd : fields)
                {
                    const char *cat = fd.category;
                    FieldGroup *g   = nullptr;
                    for (auto &cand : groups)
                    {
                        if ((cand.category == nullptr && cat == nullptr) ||
                            (cand.category && cat && strcmp(cand.category, cat) == 0))
                        {
                            g = &cand;
                            break;
                        }
                    }
                    if (!g)
                    {
                        groups.push_back({cat, {}});
                        g = &groups.back();
                    }
                    g->items.push_back(&fd);
                }

                // 表示名の列幅はパネル幅の約 4 割（90〜240px）
                auto beginPropTable = [&](const char *idstr) -> bool {
                    float avail  = ImGui::GetContentRegionAvail().x;
                    float labelW = avail * 0.42f;
                    if (labelW < 90.0f) labelW = 90.0f;
                    if (labelW > 240.0f) labelW = 240.0f;
                    if (ImGui::BeginTable(idstr, 2, ImGuiTableFlags_NoBordersInBody))
                    {
                        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, labelW);
                        ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch);
                        return true;
                    }
                    return false;
                };

                // フィールド 1 つ分の行を描く
                auto drawRow = [&](const FieldDescriptor &fd) {
                    ImGui::PushID((int)fd.offset);
                    ImGui::TableNextRow();

                    // 左の列: 表示名（ホバーで「説明 + 変数名」）
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(fd.name ? fd.name : "");
                    if (ImGui::IsItemHovered())
                    {
                        std::string tt = BuildFieldTooltip(fd);
                        if (!tt.empty()) ImGui::SetTooltip("%s", tt.c_str());
                    }

                    // 右の列: ウィジェット（幅いっぱい）
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (DrawField(fd, obj)) anyChanged = true;

                    if (ImGui::IsItemActivated())
                    {
                        s_backupState = component; // 編集を始めた時点の値を控える
                        s_backupOwner = entity;
                    }
                    // 編集を終えたら Undo コマンドを積む
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        if (world && entity != 0 && s_backupOwner == entity)
                        {
                            auto *ecsWorld  = static_cast<CCL::ECS::World *>(world);
                            auto  ecsEntity = static_cast<CCL::ECS::EntityID>(entity);
                            EditorCommandHistory::Instance().ExecuteCommand(
                                std::make_unique<ChangeComponentCommand<T>>(
                                    ecsWorld, ecsEntity, s_backupState, component));
                        }
                        else
                        {
                            CCL_LOG_WARN(LogCategory::Editor,
                                         "[UndoSystem] Failed to issue command! World or Entity is null.");
                        }
                    }
                    ImGui::PopID();
                };

                // 2. カテゴリごとに描く
                for (const auto &g : groups)
                {
                    if (g.category != nullptr)
                    {
                        bool catOpen = ImGui::TreeNodeEx(g.category,
                                                         ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                        if (catOpen)
                        {
                            ImGui::Indent(12.0f);
                            if (beginPropTable(g.category))
                            {
                                for (const auto *fd : g.items)
                                    drawRow(*fd);
                                ImGui::EndTable();
                            }
                            ImGui::Unindent(12.0f);
                            ImGui::TreePop();
                        }
                    }
                    else
                    {
                        // カテゴリ無しは見出しなしで並べる
                        if (beginPropTable("##rootprops"))
                        {
                            for (const auto *fd : g.items)
                                drawRow(*fd);
                            ImGui::EndTable();
                        }
                    }
                }
            }
        }

        // 変更があり、isDirty を持つコンポーネントなら立てる
        if (anyChanged)
        {
            if constexpr (HasIsDirty<T>::value)
            {
                component.isDirty = true;
            }
        }

        ImGui::Unindent(4.0f);
        ImGui::PopID();
        return anyChanged;
    }

    // ===========================================================================
    // 登録
    // ===========================================================================

    // ComponentGuiRegistry に T の描画関数を登録する。
    //   category / sortPriority / hiddenInInspector が無ければ "Misc" / 1000 / false を使う
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