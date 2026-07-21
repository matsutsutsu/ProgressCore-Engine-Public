#include "Editor/Windows/InspectorWindow.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>         
#include <unordered_map>  
#include <windows.h>

#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Serialization/Serializer/PrefabSerializer.h"
#include "Engine/Serialization/Serializer/SceneSerializer.h"


namespace {
    // カテゴリ見出し(英語の内部キー)を日本語の表示名に変換する。
    //   ※ 内部キー(ComponentMeta の category)は英語のまま。ここは「見た目だけ」を訳す。
    //     未知のカテゴリは nullptr を返し、呼び出し側で元の文字列を使う。
    const char* CategoryDisplayName(const std::string& cat)
    {
        if (cat == "Transform")  return "トランスフォーム";
        if (cat == "Hierarchy")  return "階層";
        if (cat == "Rendering")  return "描画";
        if (cat == "Animation")  return "アニメーション";
        if (cat == "Camera")     return "カメラ";
        if (cat == "Light")      return "ライト";
        if (cat == "Effect")     return "エフェクト";
        if (cat == "Gameplay")   return "ゲームプレイ";
        if (cat == "Combat")     return "戦闘";
        if (cat == "Movement")   return "移動";
        if (cat == "AI")         return "AI";
        if (cat == "Physics")    return "物理";
        if (cat == "Audio")      return "オーディオ";
        if (cat == "UI")         return "UI";
        if (cat == "Meta")       return "メタ情報";
        if (cat == "Internal")   return "内部";
        if (cat == "Debug")      return "デバッグ";
        if (cat == "Misc")       return "その他";
        return nullptr; // 未知カテゴリは元の名前を使う
    }
}

InspectorWindow::InspectorWindow() : EditorWindow("Inspector") {}

void InspectorWindow::DrawContents(EditorContext &context)
{
    // ★ 修正：ポインタではなく実体を直接取得
    CCL::ECS::EntityID     selected = context.selectedEntity;
    CCL::ECS::World *world    = context.world;

    // 1. エンティティ未選択時のガード
    // InvalidEntityID または 0 なら描画しない
    if (selected == CCL::ECS::InvalidEntityID || selected == 0) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "エンティティを選択してください");
        return;
    }

    // =========================================================
    // Entity メタデータ編集エリア (名前・ID)
    // =========================================================
    auto *nameComp = world->GetComponent<NameComponent>(selected);

    char filenameBuf[256] = "NewPrefab";

    if (nameComp) {
        if (ImGui::InputText("エンティティ名", nameComp->name, sizeof(nameComp->name))) {
            context.entityNames[selected] = nameComp->name;
        }
        if (strlen(nameComp->name) > 0) {
            strcpy_s(filenameBuf, sizeof(filenameBuf), nameComp->name);
        }
    }
    else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "（名前コンポーネントなし）");

        ImGui::SameLine();
        if (ImGui::Button("＋ 名前を追加")) {
            std::string defaultName = "Entity_" + std::to_string(selected);
            world->AddComponent<NameComponent>(selected, NameComponent(defaultName.c_str()));
            context.entityNames[selected] = defaultName;
        }

        auto it = context.entityNames.find(selected);
        if (it != context.entityNames.end()) {
            strcpy_s(filenameBuf, sizeof(filenameBuf), it->second.c_str());
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(ID: %llu)", selected);
    ImGui::Separator();

    // 複数選択中はその旨を表示し、複製/削除は選択全体に適用されることを明示する。
    //   ※ コンポーネント編集はアクティブ(主選択)の1体に対してのみ行う。
    const size_t selCount = context.selectedEntities.size();
    if (selCount > 1) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
            "%zu 個を選択中 (編集はアクティブの1体のみ / 複製・削除は全体)", selCount);
    }

    // 複製ボタン (選択エンティティを階層ごと複製し、複製先を選択状態にする)
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);
    if (ImGui::Button("複製", ImVec2(75, 0))) {
        std::vector<CCL::ECS::EntityID> roots = context.GetTopLevelSelection();
        if (roots.empty()) roots.push_back(selected); // 保険
        std::vector<CCL::ECS::EntityID> newRoots;
        for (CCL::ECS::EntityID src : roots) {
            auto created = SceneSerializer::DuplicateHierarchy(world, src);
            if (!created.empty()) newRoots.push_back(created[0]);
        }
        if (!newRoots.empty()) {
            context.ClearSelection();
            for (CCL::ECS::EntityID r : newRoots) context.AddSelection(r);
        }
        return; // 選択が切り替わったのでこのフレームの描画は中止
    }

    // 削除ボタン (選択中すべてを削除)
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    if (ImGui::Button("エンティティを削除", ImVec2(110, 0))) {
        std::vector<CCL::ECS::EntityID> toDelete = context.selectedEntities;
        if (toDelete.empty()) toDelete.push_back(selected); // 保険
        for (CCL::ECS::EntityID id : toDelete) {
            world->Destroy(id);
        }
        context.ClearSelection();
        return; // 破棄されたのでこれ以上の描画は中止
    }

    // =========================================================
    // Prefab保存ボタン
    // =========================================================
    ImGui::Spacing();
    if (ImGui::Button("プレハブJSONとして保存", ImVec2(-1, 0))) {
        char filename[256] = {0};
        if (strlen(filenameBuf) > 0) {
            sprintf_s(filename, "%s.json", filenameBuf);
        }
        else {
            strcpy_s(filename, "NewPrefab.json");
        }

        HWND               hWnd   = Graphics::Instance().GetWindowHandle();
        static const char *filter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";

        if (auto path = FileDialog::SavePreset("Prefab", "プレハブを保存",
            {}, Graphics::Instance().GetWindowHandle()))
        {
            PrefabSerializer::Save(path->string().c_str(), world, selected, std::string(filenameBuf));
        }
    }

    ImGui::Separator();

    // =========================================================
    // コンポーネント一覧表示と編集
    //   カテゴリ見出し(折りたたみ) → その中に各コンポーネント、の二段構造。
    //   ・カテゴリは ComponentMeta<T>::category (既定 "Misc") を GUI レジストリ
    //     経由で引く。std::map なので見出しはアルファベット順に並ぶ。
    //   ・フィルタ / Jump / 一括開閉(Expand,Collapse) は drawOne 内でそのまま機能する。
    // =========================================================
    const auto &archetype = world->GetEntityArchetype(selected);
    auto       &registry  = ComponentGuiRegistry::Instance();

    // === ① フィルタ ＋ ② 一括開閉 ＋ ⑤ Jump to ツールバー ===========
    {
        ImGui::SetNextItemWidth(-160.0f);
        ImGui::InputTextWithHint("##InspectorFilter", "コンポーネントを検索...",
            _inspectorFilterBuffer, sizeof(_inspectorFilterBuffer));

        ImGui::SameLine();
        if (ImGui::SmallButton("全展開"))
            _forceOpenState = 1;
        ImGui::SameLine();
        if (ImGui::SmallButton("全折りたたみ"))
            _forceOpenState = 0;

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##JumpTo", "コンポーネントへ移動...", ImGuiComboFlags_HeightLarge))
        {
            for (const auto &td : archetype)
            {
                std::string label = registry.GetName(td.id);
                if (label.find("Unknown") != std::string::npos && td.name != nullptr)
                {
                    label = td.name;
                }
                if (ImGui::Selectable(label.c_str()))
                {
                    _hasJumpTarget = true;
                    _jumpToTypeID  = td.id;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();
    }

    // フィルタ文字列を小文字化（大文字小文字を区別しない）
    std::string inspectorFilter = _inspectorFilterBuffer;
    std::transform(inspectorFilter.begin(), inspectorFilter.end(),
        inspectorFilter.begin(), ::tolower);
    const bool filtering = !inspectorFilter.empty();

    // -------------------------------------------------------------
    // コンポーネントの「表示名」を組み立てる（Registry の登録名 or 型名）。
    //   drawOne（描画）と componentMatches（検索一致判定）で同じ名前を使うため関数化。
    // -------------------------------------------------------------
    auto computeCompName = [&](const auto &typeData) -> std::string {
        std::string compName = registry.GetName(typeData.id);

        // 未登録（Unknown）の場合、C++が自動取得した型名を使う
        if (compName.find("Unknown") != std::string::npos &&
            typeData.name != nullptr) {
            compName = typeData.name; // 例: "struct JoltRigidbodyComponent"

            // MSVCの typeid().name() は先頭に "struct " や "class " を付けるので削る
            const std::string prefixes[] = {"struct ", "class "};
            for (const auto &prefix : prefixes) {
                if (compName.find(prefix) == 0) {
                    compName.erase(0, prefix.length());
                    break;
                }
            }
            // GUI未登録（パラメータ編集できない状態）だと分かるようマークを付ける
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

    // -------------------------------------------------------------
    // 1 コンポーネント分の描画。フィルタで弾いた場合は何も描画しない。
    // -------------------------------------------------------------
    auto drawOne = [&](const auto &typeData) -> void {
        CCL::ECS::TypeID typeID = typeData.id;

        std::string compName = computeCompName(typeData);

        // ① フィルタに一致しないコンポーネントは描画ごとスキップ
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

        // -------------------------------------------------------
        // ① コンポーネント間の明確な「黒いスリット(隙間)」
        // -------------------------------------------------------
        // コンポーネント間はうっすらした余白のみで区切る（黒い線は使わず、境界は余白で見せる）
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        // コンポーネントヘッダー: カテゴリ帯より明るいニュートラルグレーで一段下の階層を表す
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.29f, 0.31f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.34f, 0.35f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.38f, 0.39f, 0.42f, 1.0f));
        // No GUI コンポーネントは名前をくすませ、編集可能なものと区別する
        bool isNoGui = (compName.find("（GUIなし）") != std::string::npos);
        ImGui::PushStyleColor(ImGuiCol_Text,
            isNoGui ? ImVec4(0.55f, 0.55f, 0.58f, 1.0f)   // ディム
                    : ImVec4(0.92f, 0.92f, 0.94f, 1.0f)); // 通常（ほぼ白）

        // ② 一括開閉の指令があればこのフレームだけ強制（storage に永続化される）
        if (_forceOpenState != -1)
        {
            ImGui::SetNextItemOpen(_forceOpenState == 1, ImGuiCond_Always);
        }
        // 検索中は一致コンポーネントを自動で開き、すぐ中身を編集できるようにする
        if (filtering)
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        // ⑤ Jump 先は強制的に開いて中身を見せる
        bool isJumpTarget = (_hasJumpTarget && typeID == _jumpToTypeID);
        if (isJumpTarget)
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }

        bool headerOpen = ImGui::CollapsingHeader(
            compName.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);

        ImGui::PopStyleColor(4);

        // ⑤ Jump 先ヘッダーを画面上部付近までスクロール（このフレーム限り）
        if (isJumpTarget)
        {
            ImGui::SetScrollHereY(0.1f);
            _hasJumpTarget = false;
        }

        // -------------------------------------------------------
        // ③ 削除ボタンのステルス化 (ヘッダーに溶け込ませる)
        // -------------------------------------------------------
        float buttonSize = 20.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - buttonSize - 16.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));

        if (ImGui::Button("x", ImVec2(buttonSize, 18))) {
            world->RequestRemoveComponent(selected, typeID);
        }
        ImGui::PopStyleColor(4);

        // -------------------------------------------------------
        // ④ 中身のプロパティのインデント処理
        // -------------------------------------------------------
        if (headerOpen)
        {
            ImGui::Spacing();
            ImGui::Indent(16.0f); // 中身をヘッダーより深くインデント（従属を明確化）

            registry.DrawInspector(typeID, world, selected);

            ImGui::Unindent(16.0f);
            ImGui::Dummy(ImVec2(0.0f, 4.0f)); // 次コンポーネントとの間に息継ぎの余白
        }

        ImGui::PopID();
    };

    // -------------------------------------------------------------
    // カテゴリごとに typeData を振り分ける。
    //   ・hiddenInInspector のコンポーネントは一覧に出さない。
    //   ・各カテゴリ内は sortPriority 昇順。
    //   ・カテゴリ見出しの順は「属するコンポーネントの最小 priority」で決める。
    //     → priority 0 の Transform を持つ "Transform" カテゴリが必ず先頭に来る。
    // -------------------------------------------------------------
    using TypeDataT = std::decay_t<decltype(*archetype.begin())>;
    struct CatEntry {
        std::vector<const TypeDataT*> comps;
        int minPriority = 100000;
    };
    std::unordered_map<std::string, CatEntry> catMap;

    for (const auto &typeData : archetype) {
        if (registry.IsHidden(typeData.id)) continue;   // ④/③ 非表示はスキップ

        std::string cat = registry.GetCategory(typeData.id);
        if (cat.empty()) cat = "Misc";
        int prio = registry.GetSortPriority(typeData.id);

        auto &ce = catMap[cat];
        ce.comps.push_back(&typeData);
        ce.minPriority = (std::min)(ce.minPriority, prio);
    }

    // カテゴリ内を priority 昇順（同点は表示名）でソート
    for (auto &[cat, ce] : catMap) {
        std::sort(ce.comps.begin(), ce.comps.end(),
            [&](const TypeDataT* a, const TypeDataT* b) {
                int pa = registry.GetSortPriority(a->id);
                int pb = registry.GetSortPriority(b->id);
                if (pa != pb) return pa < pb;
                return registry.GetName(a->id) < registry.GetName(b->id);
            });
    }

    // カテゴリ見出しを「最小priority → 名前」で並べた順序リストにする
    std::vector<const std::string*> orderedCats;
    orderedCats.reserve(catMap.size());
    for (auto &[cat, ce] : catMap) orderedCats.push_back(&cat);
    std::sort(orderedCats.begin(), orderedCats.end(),
        [&](const std::string* a, const std::string* b) {
            int pa = catMap[*a].minPriority;
            int pb = catMap[*b].minPriority;
            if (pa != pb) return pa < pb;
            return *a < *b;
        });

    // ⑤ Jump 対象が属するカテゴリを求めておく（畳まれていても開くため）
    std::string jumpCategory;
    if (_hasJumpTarget) {
        jumpCategory = registry.GetCategory(_jumpToTypeID);
        if (jumpCategory.empty()) jumpCategory = "Misc";
    }

    // -------------------------------------------------------------
    // カテゴリ見出し（折りたたみ可能）→ 中に各コンポーネント
    // -------------------------------------------------------------
    int drawnCategoryCount = 0; // 検索時に「該当なし」を出すための実描画カウント
    for (const std::string* catPtr : orderedCats) {
        const std::string& category = *catPtr;
        auto& comps = catMap[category].comps;

        // 検索中：一致コンポーネントが1つも無いカテゴリは見出しごと隠す
        //   （空のカテゴリ帯が並んで「絞り込めていない」ように見えるのを防ぐ）
        if (filtering)
        {
            bool anyMatch = false;
            for (const TypeDataT* td : comps) {
                if (componentMatches(*td)) { anyMatch = true; break; }
            }
            if (!anyMatch) continue;
        }
        ++drawnCategoryCount;

        ImGui::PushID(category.c_str());

        if (_forceOpenState != -1)
            ImGui::SetNextItemOpen(_forceOpenState == 1, ImGuiCond_Always);
        if (_hasJumpTarget && category == jumpCategory)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        // 検索中は該当カテゴリを自動で開き、一致コンポーネントをすぐ見せる
        if (filtering)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);

        // === カテゴリ見出し（Unreal Details のカテゴリバー風）=====================
        //   ・コンポーネントヘッダーより明るい/彩度の高い色にして「親」だと分からせる
        //   ・日本語表示名（未知カテゴリは英語大文字）＋左のアクセントバーで存在感を出す
        std::string categoryLabel;
        if (const char* jp = CategoryDisplayName(category)) {
            categoryLabel = jp; // 既知カテゴリは日本語
        }
        else {
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
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f)); // y を 7.0 → 3.0 に縮小

        bool catOpen = ImGui::CollapsingHeader(categoryLabel.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap);

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        // 左端に太めのアクセントバーを重ね描き（スタイル設定に依存せず必ず見える）
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
            "「%s」に一致するコンポーネントはありません", _inspectorFilterBuffer);
    }

    // ② このフレームで消費した一括開閉指令をクリア
    _forceOpenState = -1;

    ImGui::Separator();
    ImGui::Spacing();

    // --- 新規コンポーネント追加メニュー ---
    if (ImGui::Button("＋ コンポーネントを追加", ImVec2(-1, 30))) {
        ImGui::OpenPopup("AddComponentMenu");
        // メニューを開いた瞬間に検索ワードをリセットする
        _compSearchBuffer[0] = '\0';
    }

    // ポップアップの初期サイズを固定し、見栄えを整える
    ImGui::SetNextWindowSize(ImVec2(280, 400), ImGuiCond_Appearing);
    
    if (ImGui::BeginPopup("AddComponentMenu")) {
        // 1. 自動フォーカス付きの検索バー
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(); // ポップアップが開いた瞬間、自動で文字入力状態にする
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##CompSearch", "コンポーネントを検索...", _compSearchBuffer, sizeof(_compSearchBuffer));
        
        ImGui::Separator();
        ImGui::Spacing();

        // 検索文字列の小文字化（大文字小文字を区別せず検索するため）
        std::string searchStr = _compSearchBuffer;
        std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

        // 2. スクロール可能なリスト領域の開始
        ImGui::BeginChild("ComponentList", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        auto allAvailableTypes = registry.GetAllTypeIDs();

        for (auto typeID : allAvailableTypes) {
            std::string compName = registry.GetName(typeID);

            // 検索フィルタリング処理
            if (!searchStr.empty()) {
                std::string compNameLower = compName;
                std::transform(compNameLower.begin(), compNameLower.end(), compNameLower.begin(), ::tolower);
                if (compNameLower.find(searchStr) == std::string::npos) {
                    continue; // 検索に引っかからなかったコンポーネントは非表示
                }
            }

            // 既に持っているコンポーネントはグレーアウトして押せなくする
            bool alreadyHas = world->HasComponent(selected, typeID);
            if (alreadyHas) ImGui::BeginDisabled();

            // コンポーネントの選択ボタン
            if (ImGui::Selectable(compName.c_str())) {
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
                
                // 追加したら自動でポップアップを閉じる（UX向上）
                ImGui::CloseCurrentPopup();
            }

            if (alreadyHas) ImGui::EndDisabled();
        }
        ImGui::EndChild(); // スクロール領域終了
        ImGui::EndPopup();
    }

}