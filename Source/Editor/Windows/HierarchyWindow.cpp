#include "Editor/Windows/HierarchyWindow.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Core/Components/NameComponent.h"
#include <algorithm>
#include <string>
#include "Engine/Transform/Components/PendingParentComponent.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include "Engine/Serialization/Serializer/SceneSerializer.h"


HierarchyWindow::HierarchyWindow() : EditorWindow("Hierarchy") {}

void HierarchyWindow::DrawContents(EditorContext& context)
{
    ZoneScopedN("HierarchyWindow::DrawContents");

    auto* world = context.world;
    auto& chunkManager = world->GetChunkManager();
    auto& chunks = chunkManager.GetChunks();

    // =========================================================================
    // ★ 究極の修正: 毎フレーム必ずキャッシュを空にする！
    // これを忘れると過去の親子関係が蓄積し、全てが循環参照と誤認されます。
    // =========================================================================
    {
        ZoneScopedN("Hierarchy [1]: Clear & Alloc");

        _rootEntities.clear();
        _allEntities.clear();
        _flatList.clear();

        // リアロケーション防止のため、あらかじめ1万体分のメモリを確保（推容疑者Cの対策）
        _flatList.reserve(10000);
        _allEntities.reserve(10000);
    }

    // ---------------------------------------------------------
    // ツールバー (生成 ＆ 検索)  ※ここが消えていた部分です！
    // ---------------------------------------------------------
    if (ImGui::Button("Spawn Entity")) {
        auto arch = ArchetypeHelper::Generate<TransformComponent, NameComponent>();
        EntityFactory::SpawnNamed(*world, "New Entity ", arch);
    }

    ImGui::SameLine();
    // 検索バーの実装
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##Search", "Search Entity...", _searchBuffer, sizeof(_searchBuffer));

    bool        isSearching = strlen(_searchBuffer) > 0;
    std::string searchStr = _searchBuffer;
    // 小文字に変換して大文字小文字を区別しない検索にする
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

    ImGui::Separator();

    {
        // =========================================================================
        // ★ 計測プローブ 2: チャンク走査とツリー構築フェーズ
        // ここが重い場合、_openNodes.count() か IsEntityDestroyed() が犯人です
        // =========================================================================
        ZoneScopedN("Hierarchy [2]: Chunk Scan & Build");

        // =========================================================
        // ★ 超高速 Pass: ルートエンティティの抽出だけを行う
        // =========================================================
        for (auto& chunkPtr : chunks) {
            if (!chunkPtr) continue;
            CCL::ECS::Chunk* chunk = chunkPtr.get();

            TransformComponent* transforms = chunk->HasComponent<TransformComponent>()
                ? chunk->GetComponentArray<TransformComponent>() : nullptr;

            for (size_t i = 0; i < chunk->GetEntityCount(); ++i) {
                if (chunk->IsEntityDestroyed(i)) continue;
                CCL::ECS::EntityID entity = chunk->GetEntityByIndex(i);
                if (entity == CCL::ECS::InvalidEntityID) continue;

                // ★ 修正1: 無駄な毎フレームのアロケーションを防ぐため、検索時のみリストに追加する
                if (isSearching) {
                    _allEntities.push_back(entity);
                }

                // 親がいない(0)場合はルート
                if (!transforms || transforms[i].parent == 0) {
                    if (isSearching) {
                        _rootEntities.push_back(entity); // 検索ロジック用に一応残す
                    }
                    else {
                        // ====================================================================
                        // ★ 究極のDOD最適化: GetComponentを回避し、キャッシュから直接読む！
                        // ====================================================================
                        bool hasChildren = (transforms != nullptr && transforms[i].firstChildID != 0);

                        // 直接UI用のフラットリストに追加
                        _flatList.push_back({ entity, 0, hasChildren });

                        // 子供がいて、かつ「このノードが開かれている」場合のみ、子供の再帰探索を走らせる
                        // （※閉じていれば、この先の重い処理は一切走らない！）
                        if (hasChildren && _openNodes.count(entity) > 0) {
                            FlattenChildren(transforms[i].firstChildID, 1, world, _flatList);
                        }
                    }
                }
            }
        }
    }


    {
        // =========================================================================
        // ★ 計測プローブ 3: 検索文字列処理フェーズ
        // =========================================================================
        ZoneScopedN("Hierarchy [3]: Search String Process");

        // ---------------------------------------------------------
        // 2. 平坦化フェーズ 
        // ---------------------------------------------------------
        if (isSearching) {
            for (auto e : _allEntities) {
                // ★検索中のみ、重い文字列処理を行う
                auto* nameComp = world->GetComponent<NameComponent>(e);
                std::string nameLower = (nameComp && strlen(nameComp->name) > 0) ? nameComp->name : "entity " + std::to_string(e);
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

                if (nameLower.find(searchStr) != std::string::npos) {
                    _flatList.push_back({ e, 0, false });
                }
            }
        }
    }

    {
        // =========================================================================
        // ★ 計測プローブ 3: 検索文字列処理フェーズ
        // =========================================================================
        ZoneScopedN("Hierarchy [3]: Search String Process");

        // ---------------------------------------------------------
        // 3. 描画フェーズ 
        // ---------------------------------------------------------
        ImGui::BeginChild("HierarchyTree", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        ImGuiListClipper clipper;
        clipper.Begin((int)_flatList.size());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                DrawFlatNode(_flatList[i], context); // 引数を減らす
            }
        }
        clipper.End();

        ImGui::EndChild();

    }
    // ---------------------------------------------------------
    // 4. ドラッグ＆ドロップ受け入れ処理と選択解除
    // ---------------------------------------------------------
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PREFAB")) {
            const char* prefabPath = (const char*)payload->Data;
            CCL::ECS::EntityID newEntity = Prefab::SpawnPrefab(*context.world, prefabPath);
            if (newEntity != CCL::ECS::InvalidEntityID) {
                context.SetSingleSelection(newEntity);
            }
        }
        ImGui::EndDragDropTarget();
    }
 
    // =========================================================================
    // ★ 追加: ヒエラルキーの背景へのドロップ判定 (親の解除 / ルート化)
    // =========================================================================
    // 残りのウィンドウ領域をダミーアイテムで埋めて、ドロップ判定を有効にする
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID")) {
            CCL::ECS::EntityID draggedEntity = *(const CCL::ECS::EntityID*)payload->Data;

            // 親を 0（なし）に設定して予約
            if (context.world->HasComponent<PendingParentComponent>(draggedEntity)) {
                context.world->GetComponent<PendingParentComponent>(draggedEntity)->parentID = 0;
            }
            else {
                context.world->AddComponent<PendingParentComponent>(draggedEntity);
            }
            CCL_LOG_INFO(LogCategory::Editor, "Unparenting: Entity %u is now root", draggedEntity);
        }
        ImGui::EndDragDropTarget();
    }
    // 背景クリック時の選択解除
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
        context.ClearSelection();
    }

    // ---------------------------------------------------------
    // 5. キーボードショートカット: Ctrl+D で選択エンティティ(複数可)を階層ごと複製
    // ---------------------------------------------------------
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_D, false) &&
        !context.selectedEntities.empty())
    {
        std::vector<CCL::ECS::EntityID> roots = context.GetTopLevelSelection();
        std::vector<CCL::ECS::EntityID> newRoots;
        for (CCL::ECS::EntityID src : roots) {
            auto created = SceneSerializer::DuplicateHierarchy(context.world, src);
            if (!created.empty()) newRoots.push_back(created[0]);
        }
        if (!newRoots.empty()) {
            // 複製で生まれたルート群を新しい選択セットにする
            context.ClearSelection();
            for (CCL::ECS::EntityID r : newRoots) context.AddSelection(r);
            CCL_LOG_INFO(LogCategory::Editor, "Duplicated %zu entities (Ctrl+D)", newRoots.size());
        }
    }
}


// =========================================================================
// 個別ノードの描画 (再帰を排除した超高速描画)
// =========================================================================
void HierarchyWindow::DrawFlatNode(
    const FlatNode& node,
    EditorContext& context)
{
    // =========================================================
    // ★遅延評価: 画面に映る「たった40個」に対してだけ名前を生成する！
    // =========================================================
    std::string name = "Entity " + std::to_string(node.entityID);
    auto* nameComp = context.world->GetComponent<NameComponent>(node.entityID);
    if (nameComp && strlen(nameComp->name) > 0) {
        name = nameComp->name;
    }

    // ★重要: 深さに応じてUIの表示位置を右にずらす（見た目だけツリーにする）
    ImGui::Indent(node.depth * ImGui::GetTreeNodeToLabelSpacing());

    // ★重要: ImGuiTreeNodeFlags_NoTreePushOnOpen を必ずつける！
    // これにより、ImGuiの内部スタックを汚さず、単なる「見た目だけ」のツリーノードになります。
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;



    if (!node.hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    if (context.IsSelected(node.entityID)) flags |= ImGuiTreeNodeFlags_Selected;

    // エンジン側で管理している開閉状態を ImGui に強制的に教える
    bool isOpen = _openNodes.count(node.entityID) > 0;
    ImGui::SetNextItemOpen(isOpen);

    // 描画
    ImGui::TreeNodeEx((void*)(uintptr_t)node.entityID, flags, "%s", name.c_str());

    // ユーザーが左の矢印(▶)をクリックして開閉状態を切り替えた時の処理
    if (ImGui::IsItemToggledOpen()) {
        if (isOpen) _openNodes.erase(node.entityID); // 閉じる
        else        _openNodes.insert(node.entityID); // 開く
    }

    // =======================================================================
    // ★ここから追加: ドラッグ＆ドロップの「送信元（Source）」としての設定
    // =======================================================================
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {

        // "DND_ENTITY_ID" という識別子で、EntityIDのメモリの塊をペイロードとして積む
        ImGui::SetDragDropPayload("DND_ENTITY_ID", &node.entityID, sizeof(CCL::ECS::EntityID));

        // ドラッグ中、マウスカーソルの横に表示されるプレビューUI
        ImGui::Text("Drop Entity: %u", node.entityID);

        ImGui::EndDragDropSource();
    }

    // =========================================================================
    // ★ 追加: ドラッグ＆ドロップ：受信先 (親子関係の設定)
    // =========================================================================
    if (ImGui::BeginDragDropTarget()) {
        // "DND_ENTITY_ID" タグの荷物を受け取る
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID")) {
            CCL::ECS::EntityID draggedEntity = *(const CCL::ECS::EntityID*)payload->Data;
            CCL::ECS::EntityID targetParent = node.entityID;

            // ① 自分自身には入れられない
            // ② ドロップ先(targetParent)が、ドラッグしている奴(draggedEntity)の子供や孫であってはならない
            if (draggedEntity != targetParent && !IsDescendantOf(targetParent, draggedEntity, context.world)) {
                // PendingParentComponent を使用して親子関係を予約
                if (context.world->HasComponent<PendingParentComponent>(draggedEntity)) {
                    context.world->GetComponent<PendingParentComponent>(draggedEntity)->parentID = targetParent;
                }
                else {
					PendingParentComponent ppc;
					ppc.parentID = targetParent;
                    context.world->AddComponent<PendingParentComponent>(draggedEntity, ppc);
                }
                CCL_LOG_INFO(LogCategory::Editor, "Parenting: Entity %u is now child of %u", draggedEntity, targetParent);
            }
        }
        else {
            // ブロックされた場合は警告を出す
            //CCL_LOG_WARN(LogCategory::Editor, "Parenting Failed: Circular reference or self-parenting detected!");
        }
        
        ImGui::EndDragDropTarget();
    }

    // =======================================================================
    // クリック（選択）判定を「スマート・クリック」に進化させる
    // =======================================================================
    // 条件1: アイテムの上でマウスがホバーされている
    // 条件2: マウスの左ボタンが「離された（Released）」瞬間である
    // 条件3: TreeNodeの開閉ボタン(矢印)が押されたわけではない
    // 条件4: ★ ドラッグ操作をしていない（閾値以上マウスが移動していない）
    
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {

        // ImGuiの標準設定（io.MouseDragThreshold）を超えてドラッグされていないかチェック
        // MouseDragThreshold はデフォルトで 6.0f 程度に設定されています。
        if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            // かつ、矢印（開閉ボタン）を押していない場合のみ選択を切り替える
            if (!ImGui::IsItemToggledOpen()) {
                ImGuiIO& io = ImGui::GetIO();
                if (io.KeyCtrl) {
                    // Ctrl+クリック: 個別トグル (追加/解除)
                    context.ToggleSelection(node.entityID);
                }
                else if (io.KeyShift && context.selectedEntity != CCL::ECS::InvalidEntityID) {
                    // Shift+クリック: 主選択(アンカー)からこのノードまでを範囲選択
                    SelectRangeTo(node.entityID, context);
                }
                else {
                    // 通常クリック: 単一選択
                    context.SetSingleSelection(node.entityID);
                }
            }
        }
    }

    // ---------------------------------------------------------
    //  右クリックメニュー (独立/Unparent 機能の追加)
    // ---------------------------------------------------------
    // 未選択のノードを右クリックした場合は、まずそれを単一選択にする
    // (一般的なエディタの挙動。選択済みノードを右クリックした時は選択を維持する)。
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (!context.IsSelected(node.entityID)) {
            context.SetSingleSelection(node.entityID);
        }
    }

    if (ImGui::BeginPopupContextItem()) {

        // 選択数に応じた見出し (複数選択中はまとめて操作されることを明示)
        const size_t selCount = context.selectedEntities.size();
        if (selCount > 1) {
            ImGui::TextDisabled("%zu 個を選択中", selCount);
            ImGui::Separator();
        }

        // 追加: 親から外れてルートに戻るボタン (選択中すべてに適用)
        if (ImGui::MenuItem("Unparent (Make Root)")) {
            for (CCL::ECS::EntityID id : context.selectedEntities) {
                if (context.world->HasComponent<PendingParentComponent>(id)) {
                    context.world->GetComponent<PendingParentComponent>(id)->parentID = 0;
                } else {
                    context.world->AddComponent<PendingParentComponent>(id); // 0は親なしを意味する
                }
            }
            CCL_LOG_INFO(LogCategory::Editor, "Unparented %zu entities.", selCount);
        }

        // 追加: 選択エンティティ(複数可)を階層ごと複製する
        if (ImGui::MenuItem("複製", "Ctrl+D")) {
            std::vector<CCL::ECS::EntityID> roots = context.GetTopLevelSelection();
            std::vector<CCL::ECS::EntityID> newRoots;
            for (CCL::ECS::EntityID src : roots) {
                auto created = SceneSerializer::DuplicateHierarchy(context.world, src);
                if (!created.empty()) newRoots.push_back(created[0]);
            }
            if (!newRoots.empty()) {
                context.ClearSelection();
                for (CCL::ECS::EntityID r : newRoots) context.AddSelection(r);
                CCL_LOG_INFO(LogCategory::Editor, "Duplicated %zu entities", newRoots.size());
            }
        }

        ImGui::Separator(); // 区切り線

        if (ImGui::MenuItem("Delete Entity")) {
            // 選択中すべてを削除 (セットをコピーしてから回す)
            std::vector<CCL::ECS::EntityID> toDelete = context.selectedEntities;
            for (CCL::ECS::EntityID id : toDelete) {
                context.world->Destroy(id);
            }
            context.ClearSelection();
        }
        ImGui::EndPopup();
    }

    // UIの位置を元に戻す
    ImGui::Unindent(node.depth * ImGui::GetTreeNodeToLabelSpacing());
}


// ★ 古い FlattenTree を丸ごと消して、これに差し替える
void HierarchyWindow::FlattenChildren(
    CCL::ECS::EntityID firstChildID,
    int depth,
    CCL::ECS::World* world,
    std::vector<FlatNode>& outFlatList)
{
    CCL::ECS::EntityID childID = firstChildID;

    // 兄弟リンク(nextSiblingID)を辿って全子供をリスト化
    while (childID != 0) {
        auto* childTrans = world->GetComponent<TransformComponent>(childID);
        if (!childTrans) break;

        bool hasChildren = (childTrans->firstChildID != 0);
        outFlatList.push_back({ childID, depth, hasChildren });

        // この子供も開かれていたら、さらに孫を再帰探索する
        if (hasChildren && _openNodes.count(childID) > 0) {
            FlattenChildren(childTrans->firstChildID, depth + 1, world, outFlatList);
        }

        childID = childTrans->nextSiblingID;
    }
}

// =========================================================================
// Shift+クリック範囲選択
//   現在の主選択(アンカー)から clicked までを、画面に見えている並び順
//   (_flatList) の上で連続選択する。
// =========================================================================
void HierarchyWindow::SelectRangeTo(CCL::ECS::EntityID clicked, EditorContext& context)
{
    CCL::ECS::EntityID anchor = context.selectedEntity;

    // _flatList 内でのアンカーとクリック先の位置を探す
    int anchorIdx = -1, clickedIdx = -1;
    for (int i = 0; i < (int)_flatList.size(); ++i) {
        if (_flatList[i].entityID == anchor)  anchorIdx = i;
        if (_flatList[i].entityID == clicked) clickedIdx = i;
    }

    // どちらかが見つからない(検索中で表示外など)場合は単純に追加選択にフォールバック
    if (anchorIdx < 0 || clickedIdx < 0) {
        context.AddSelection(clicked);
        return;
    }

    int lo = (std::min)(anchorIdx, clickedIdx);
    int hi = (std::max)(anchorIdx, clickedIdx);

    // 範囲を選択セットに置き換える (アンカーは維持、主選択は clicked にする)
    context.selectedEntities.clear();
    for (int i = lo; i <= hi; ++i) {
        context.selectedEntities.push_back(_flatList[i].entityID);
    }
    context.selectedEntity = clicked;
}

bool HierarchyWindow::IsDescendantOf(
    CCL::ECS::EntityID targetEntity,
    CCL::ECS::EntityID ancestorEntity,
    CCL::ECS::World* world)
{
    if (targetEntity == ancestorEntity) return true;

    auto* trans = world->GetComponent<TransformComponent>(ancestorEntity);
    if (!trans) return false;

    // 子供とその兄弟たちをチェック
    CCL::ECS::EntityID childID = trans->firstChildID;
    while (childID != 0) {
        if (IsDescendantOf(targetEntity, childID, world)) return true;
        auto* childTrans = world->GetComponent<TransformComponent>(childID);
        childID = childTrans ? childTrans->nextSiblingID : 0;
    }
    return false;
}
