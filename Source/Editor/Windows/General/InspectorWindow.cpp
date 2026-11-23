#include "Editor/Windows/General/InspectorWindow.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <windows.h>

#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Serialization/Serializer/PrefabSerializer.h"

namespace
{
    /**
     * @brief カテゴリ見出し（英語の内部キー）を日本語の表示名に変換する
     *
     * 内部キー（ComponentMeta の category）は英語のまま。ここは「見た目だけ」を訳す
     *
     * @param cat 内部キー
     * @return 表示名（文字列リテラル）。未知のカテゴリは nullptr を返し、呼び出し側で元の文字列を使う
     */
    const char *CategoryDisplayName(const std::string &cat)
    {
        if (cat == "Transform") return "トランスフォーム";
        if (cat == "Hierarchy") return "階層";
        if (cat == "Rendering") return "描画";
        if (cat == "Animation") return "アニメーション";
        if (cat == "Camera") return "カメラ";
        if (cat == "Light") return "ライト";
        if (cat == "Effect") return "エフェクト";
        if (cat == "Gameplay") return "ゲームプレイ";
        if (cat == "Combat") return "戦闘";
        if (cat == "Movement") return "移動";
        if (cat == "AI") return "AI";
        if (cat == "Physics") return "物理";
        if (cat == "Audio") return "オーディオ";
        if (cat == "UI") return "UI";
        if (cat == "Meta") return "メタ情報";
        if (cat == "Internal") return "内部";
        if (cat == "Debug") return "デバッグ";
        if (cat == "Misc") return "その他";
        return nullptr; // 未知カテゴリは元の名前を使う
    }
}

InspectorWindow::InspectorWindow() : EditorWindow("Inspector") {}



void InspectorWindow::DrawContents(EditorContext &context)
{
    CCL::ECS::EntityID selected = context.selectedEntity;
    CCL::ECS::World   *world    = context.world;

    // 未選択なら描画しない（InvalidEntityID と 0 のどちらも未選択として扱う）
    if (selected == CCL::ECS::InvalidEntityID || selected == 0)
    {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "エンティティを選択してください");
        return;
    }

    // world は「アクティブなシーンがない」ときに nullptr になる（Framework が毎フレーム設定）
    //   以降は無条件に world-> を辿るので、ここで必ず止める
    if (!world)
    {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "シーンが読み込まれていません");
        return;
    }

    // ============================================================
    // 名前と ID（名前欄を横幅いっぱいに、ID は右端に寄せる）
    // ============================================================
    auto *nameComp = world->GetComponent<NameComponent>(selected);

    char filenameBuf[256] = "NewPrefab"; // プレハブ保存時のファイル名の初期値

    char        idText[32];
    std::snprintf(idText, sizeof(idText), "ID: %llu", selected);
    const float idWidth = ImGui::CalcTextSize(idText).x + ImGui::GetStyle().ItemSpacing.x;

    if (nameComp)
    {
        ImGui::SetNextItemWidth(-idWidth);
        ImGui::InputTextWithHint("##EntityName", "エンティティ名", nameComp->name, sizeof(nameComp->name));
        if (strlen(nameComp->name) > 0)
        {
            strcpy_s(filenameBuf, sizeof(filenameBuf), nameComp->name);
        }
    }
    else
    {
        // 名前がないエンティティは、名前欄の代わりに追加ボタンを同じ幅で置く
        if (ImGui::Button("＋ 名前を追加", ImVec2(-idWidth, 0)))
        {
            std::string defaultName = "Entity_" + std::to_string(selected);
            world->AddComponent<NameComponent>(selected, NameComponent(defaultName.c_str()));
        }
    }

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", idText);

    // 複数選択中はその旨を表示し、削除は選択全体に適用されることを示す
    //   コンポーネントの編集は主選択の1体に対してだけ行う
    const size_t selCount = context.selectedEntities.size();
    if (selCount > 1)
    {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
                           "%zu 個を選択中 (編集はアクティブの1体のみ / 削除は全体)", selCount);
    }

    // ============================================================
    // 操作ボタン（プレハブ保存 / 削除 を1行に等幅で並べる）
    // ============================================================
    ImGui::Spacing();

    const float spacing     = ImGui::GetStyle().ItemSpacing.x;
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - spacing) / 2.0f;

    // ---- プレハブとして保存 ----
    if (ImGui::Button("プレハブ保存", ImVec2(buttonWidth, 0)))
    {
        // 保存ダイアログのファイル名の初期値（エンティティ名.json）
        char filename[256] = {0};
        if (strlen(filenameBuf) > 0)
        {
            sprintf_s(filename, "%s.json", filenameBuf);
        }
        else
        {
            strcpy_s(filename, "NewPrefab.json");
        }

        if (auto path = FileDialog::SavePreset("Prefab", "プレハブを保存",
                                               filename, Graphics::Instance().GetWindowHandle()))
        {
            PrefabSerializer::Save(path->string().c_str(), world, selected, std::string(filenameBuf));
        }
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("主選択のエンティティをプレハブJSONとして保存");
    }

    // ---- 削除（選択中すべてを削除）----
    ImGui::SameLine();
    if (ImGui::Button("削除", ImVec2(buttonWidth, 0)))
    {
        std::vector<CCL::ECS::EntityID> toDelete = context.selectedEntities;
        if (toDelete.empty()) toDelete.push_back(selected); // selectedEntities が空でも主選択は削除する
        for (CCL::ECS::EntityID id : toDelete)
        {
            world->Destroy(id);
        }
        context.ClearSelection();
        return; // 破棄したので、これ以上は描画しない
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ============================================================
    // コンポーネントの一覧と編集
    //   カテゴリの見出し（折りたたみ）の中に各コンポーネントを並べる二段構造
    //   ・カテゴリは ComponentMeta<T>::category（既定 "Misc"）を GUI レジストリ経由で引く
    //   ・見出しの並びは、属するコンポーネントの最小の sortPriority 順（同じなら名前順）
    // ============================================================
    const auto &archetype = world->GetEntityArchetype(selected);
    auto       &registry  = ComponentGuiRegistry::Instance();

    // ---- 検索と一括開閉 ----
    {
        ImGui::SetNextItemWidth(-160.0f);
        ImGui::InputTextWithHint("##InspectorFilter", "コンポーネントを検索...",
                                 m_inspectorFilterBuffer, sizeof(m_inspectorFilterBuffer));

        ImGui::SameLine();
        if (ImGui::SmallButton("全展開"))
            m_forceOpenState = 1;
        ImGui::SameLine();
        if (ImGui::SmallButton("全折りたたみ"))
            m_forceOpenState = 0;

        ImGui::Separator();
    }

    // フィルタ文字列を小文字化（大文字小文字を区別しない）
    std::string inspectorFilter = m_inspectorFilterBuffer;
    std::transform(inspectorFilter.begin(), inspectorFilter.end(),
                   inspectorFilter.begin(), ::tolower);
    const bool filtering = !inspectorFilter.empty();

    // コンポーネントの表示名を作る（レジストリの登録名、未登録なら型名）
    //   drawOne（描画）と componentMatches（検索の一致判定）で同じ名前を使うためにまとめる
    auto computeCompName = [&](const auto &typeData) -> std::string {
        std::string compName = registry.GetName(typeData.id);

        // 未登録（Unknown）なら typeid の型名を使う
        if (compName.find("Unknown") != std::string::npos &&
            typeData.name != nullptr)
        {
            compName = typeData.name; // 例: "struct JoltRigidbodyComponent"

            // MSVCの typeid().name() は先頭に "struct " や "class " を付けるので削る
            const std::string prefixes[] = {"struct ", "class "};
            for (const auto &prefix : prefixes)
            {
                if (compName.find(prefix) == 0)
                {
                    compName.erase(0, prefix.length());
                    break;
                }
            }
            // GUI が未登録（値を編集できない）と分かるよう印を付ける
            compName += " （GUIなし）";
        }
        return compName;
    };

    // 検索フィルタにこのコンポーネントが一致するか（空カテゴリを隠す判定に使う）
    auto componentMatches = [&](const auto &typeData) -> bool {
        if (!filtering) return true;
        std::string n = computeCompName(typeData);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return n.find(inspectorFilter) != std::string::npos;
    };

    // コンポーネント1つぶんの描画。検索に一致しなければ何も描かない
    auto drawOne = [&](const auto &typeData) -> void {
        CCL::ECS::TypeID typeID = typeData.id;

        std::string compName = computeCompName(typeData);

        if (filtering)
        {
            std::string compNameLower = compName;
            std::transform(compNameLower.begin(), compNameLower.end(),
                           compNameLower.begin(), ::tolower);
            if (compNameLower.find(inspectorFilter) == std::string::npos)
            {
                return;
            }
        }

        ImGui::PushID((int)typeID);

        // コンポーネントの間は線を引かず、少しの余白だけで区切る
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        // コンポーネントヘッダー: カテゴリ帯より明るいニュートラルグレーで一段下の階層を表す
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.29f, 0.31f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.34f, 0.35f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.38f, 0.39f, 0.42f, 1.0f));
        // GUI のないコンポーネントは名前を暗くし、編集できるものと区別する
        bool isNoGui = (compName.find("（GUIなし）") != std::string::npos);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              isNoGui ? ImVec4(0.55f, 0.55f, 0.58f, 1.0f)   // 暗い灰
                                      : ImVec4(0.92f, 0.92f, 0.94f, 1.0f)); // 通常（ほぼ白）

        // 一括開閉の指示があれば、このフレームだけ開閉を指定する（ImGui が状態を覚えるので、以降も続く）
        if (m_forceOpenState != -1)
        {
            ImGui::SetNextItemOpen(m_forceOpenState == 1, ImGuiCond_Always);
        }
        // 検索中は一致コンポーネントを自動で開き、すぐ中身を編集できるようにする
        if (filtering)
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }

        bool headerOpen = ImGui::CollapsingHeader(
            compName.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);

        ImGui::PopStyleColor(4);

        // 削除ボタン。背景を透明にしてヘッダーの右端に重ね、ホバーしたときだけ赤くする
        float buttonSize = 20.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - buttonSize - 16.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));

        if (ImGui::Button("x", ImVec2(buttonSize, 18)))
        {
            world->RequestRemoveComponent(selected, typeID);
        }
        ImGui::PopStyleColor(4);

        // 中身はヘッダーより深く字下げし、どのコンポーネントの値かを分かりやすくする
        if (headerOpen)
        {
            ImGui::Spacing();
            ImGui::Indent(16.0f);

            registry.DrawInspector(typeID, world, selected);

            ImGui::Unindent(16.0f);
            ImGui::Dummy(ImVec2(0.0f, 4.0f)); // 次のコンポーネントとの間の余白
        }

        ImGui::PopID();
    };

    // カテゴリごとに typeData を振り分ける
    //   ・hidden のコンポーネントは一覧に出さない
    //   ・各カテゴリ内は sortPriority 昇順
    //   ・カテゴリ見出しの順は「属するコンポーネントの最小 priority」で決める
    //     priority 0 の Transform を持つ "Transform" カテゴリが必ず先頭に来る
    using TypeDataT = std::decay_t<decltype(*archetype.begin())>;
    struct CatEntry
    {
        std::vector<const TypeDataT *> comps;
        int                            minPriority = 100000;
    };
    std::unordered_map<std::string, CatEntry> catMap;

    for (const auto &typeData : archetype)
    {
        if (registry.IsHidden(typeData.id)) continue;

        std::string cat = registry.GetCategory(typeData.id);
        if (cat.empty()) cat = "Misc";
        int prio = registry.GetSortPriority(typeData.id);

        auto &ce = catMap[cat];
        ce.comps.push_back(&typeData);
        ce.minPriority = (std::min)(ce.minPriority, prio);
    }

    // カテゴリ内を priority 昇順（同点は表示名）でソート
    for (auto &[cat, ce] : catMap)
    {
        std::sort(ce.comps.begin(), ce.comps.end(),
                  [&](const TypeDataT *a, const TypeDataT *b) {
                      int pa = registry.GetSortPriority(a->id);
                      int pb = registry.GetSortPriority(b->id);
                      if (pa != pb) return pa < pb;
                      return registry.GetName(a->id) < registry.GetName(b->id);
                  });
    }

    // カテゴリ見出しを「最小priority → 名前」で並べた順序リストにする
    std::vector<const std::string *> orderedCats;
    orderedCats.reserve(catMap.size());
    for (auto &[cat, ce] : catMap)
        orderedCats.push_back(&cat);
    std::sort(orderedCats.begin(), orderedCats.end(),
              [&](const std::string *a, const std::string *b) {
                  int pa = catMap[*a].minPriority;
                  int pb = catMap[*b].minPriority;
                  if (pa != pb) return pa < pb;
                  return *a < *b;
              });

    // カテゴリの見出し（折りたたみ）と、その中の各コンポーネントを描く
    int drawnCategoryCount = 0; // 検索時に「該当なし」を出すための実描画カウント
    for (const std::string *catPtr : orderedCats)
    {
        const std::string &category = *catPtr;
        auto              &comps    = catMap[category].comps;

        // 検索中：一致コンポーネントが1つもないカテゴリは見出しごと隠す
        //   （空のカテゴリ帯が並んで「絞り込めていない」ように見えるのを防ぐ）
        if (filtering)
        {
            bool anyMatch = false;
            for (const TypeDataT *td : comps)
            {
                if (componentMatches(*td))
                {
                    anyMatch = true;
                    break;
                }
            }
            if (!anyMatch) continue;
        }
        ++drawnCategoryCount;

        ImGui::PushID(category.c_str());

        if (m_forceOpenState != -1)
            ImGui::SetNextItemOpen(m_forceOpenState == 1, ImGuiCond_Always);
        // 検索中は該当カテゴリを自動で開き、一致コンポーネントをすぐ見せる
        if (filtering)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);

        // カテゴリの見出し
        //   ・コンポーネントのヘッダーより彩度の高い色にして、一段上の階層だと分かるようにする
        //   ・日本語の表示名（未知のカテゴリは英語の大文字）と、左端の色の帯を付ける
        std::string categoryLabel;
        if (const char *jp = CategoryDisplayName(category))
        {
            categoryLabel = jp; // 既知カテゴリは日本語
        }
        else
        {
            categoryLabel = category; // 未知カテゴリは大文字化した英語
            std::transform(categoryLabel.begin(), categoryLabel.end(),
                           categoryLabel.begin(), ::toupper);
        }

        // 帯の上下に薄い余白を作り、前後のセクションと視覚的に切り離す
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        ImVec2 barMin   = ImGui::GetCursorScreenPos();
        float  barWidth = ImGui::GetContentRegionAvail().x;

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.26f, 0.38f, 1.0f)); // 彩度を落とした落ち着いた青
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.31f, 0.45f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.34f, 0.50f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));

        bool catOpen = ImGui::CollapsingHeader(categoryLabel.c_str(),
                                               ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap);

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        // 左端に色の帯を重ねて描く（スタイルの設定に依存せず必ず見える）
        {
            ImDrawList *dl        = ImGui::GetWindowDrawList();
            float       barHeight = ImGui::GetItemRectSize().y;
            ImVec2      accentMin = barMin;
            ImVec2      accentMax = ImVec2(barMin.x + 4.0f, barMin.y + barHeight);
            dl->AddRectFilled(accentMin, accentMax, IM_COL32(90, 160, 240, 255));
        }

        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        if (catOpen)
        {
            ImGui::Indent(8.0f);
            for (const TypeDataT *td : comps)
            {
                drawOne(*td);
            }
            ImGui::Unindent(8.0f);
        }

        ImGui::PopID();
    }

    // 検索したがどのコンポーネントにも一致しなかった場合の案内
    if (filtering && drawnCategoryCount == 0)
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "「%s」に一致するコンポーネントはありません", m_inspectorFilterBuffer);
    }

    // 一括開閉の指示はこのフレームだけ有効なので戻す
    m_forceOpenState = -1;

    ImGui::Separator();
    ImGui::Spacing();

    // ---- コンポーネントの追加 ----
    if (ImGui::Button("＋ コンポーネントを追加", ImVec2(-1, 30)))
    {
        ImGui::OpenPopup("AddComponentMenu");
        // メニューを開いた瞬間に検索ワードをリセットする
        m_compSearchBuffer[0] = '\0';
    }

    // ポップアップの初期サイズ
    ImGui::SetNextWindowSize(ImVec2(280, 400), ImGuiCond_Appearing);

    if (ImGui::BeginPopup("AddComponentMenu"))
    {
        // 開いた瞬間に検索欄へフォーカスし、すぐ文字を入力できるようにする
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##CompSearch", "コンポーネントを検索...", m_compSearchBuffer, sizeof(m_compSearchBuffer));

        ImGui::Separator();
        ImGui::Spacing();

        // 検索文字列の小文字化（大文字小文字を区別せず検索するため）
        std::string searchStr = m_compSearchBuffer;
        std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

        ImGui::BeginChild("ComponentList", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        auto allAvailableTypes = registry.GetAllTypeIDs();

        for (auto typeID : allAvailableTypes)
        {
            std::string compName = registry.GetName(typeID);

            // 検索フィルタリング処理
            if (!searchStr.empty())
            {
                std::string compNameLower = compName;
                std::transform(compNameLower.begin(), compNameLower.end(), compNameLower.begin(), ::tolower);
                if (compNameLower.find(searchStr) == std::string::npos)
                {
                    continue;
                }
            }

            // 既に持っているコンポーネントはグレーアウトして押せなくする
            bool alreadyHas = world->HasComponent(selected, typeID);
            if (alreadyHas) ImGui::BeginDisabled();

            // コンポーネントの選択ボタン
            if (ImGui::Selectable(compName.c_str()))
            {
                auto &entry = registry.GetEntry(typeID);
                world->RequestAddComponent(selected,
                                           typeID,
                                           entry.size,
                                           entry.ctor,
                                           entry.dtor,
                                           entry.constructor,
                                           entry.assigner,
                                           entry.mover,
                                           entry.name.c_str());

                // 追加したらポップアップを閉じる
                ImGui::CloseCurrentPopup();
            }

            if (alreadyHas) ImGui::EndDisabled();
        }
        ImGui::EndChild(); // スクロール領域終了
        ImGui::EndPopup();
    }
}