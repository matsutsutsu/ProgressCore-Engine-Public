#include "Editor/Windows/General/HierarchyWindow.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Core/Components/EntityLabel.h"
#include <algorithm>
#include <string>
#include "Engine/Transform/Components/PendingParentComponent.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Core/Components/PrefabInstanceComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include <filesystem>

HierarchyWindow::HierarchyWindow() : EditorWindow("Hierarchy") {}



// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

void HierarchyWindow::DrawContents(EditorContext &context)
{
    ZoneScopedN("HierarchyWindow::DrawContents");

    auto *world = context.world;
    if (!world) return; // シーンの読み込み前など

    auto &chunkManager = world->GetChunkManager();
    auto &chunks       = chunkManager.GetChunks();

    // 前のフレームの一覧を必ず捨てる
    // 残したままだと過去の親子関係が溜まり、循環参照と誤って判定される
    {
        ZoneScopedN("Hierarchy [1]: Clear & Alloc");

        m_allEntities.clear();
        m_flatList.clear();

        // 1万体ぶんを先に確保し、フレーム中の再確保を避ける
        m_flatList.reserve(10000);
        m_allEntities.reserve(10000);
    }

    // ---- ツールバー（生成と検索） ----
    if (ImGui::Button("Spawn Entity"))
    {
        auto arch = ArchetypeHelper::Generate<TransformComponent, NameComponent>();
        EntityFactory::SpawnNamed(*world, "New Entity ", arch);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##Search", "Search Entity...", m_searchBuffer, sizeof(m_searchBuffer));

    bool        isSearching = strlen(m_searchBuffer) > 0;
    std::string searchStr   = m_searchBuffer;
    // 小文字にそろえ、大文字小文字を区別しない検索にする
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

    ImGui::Separator();

    {
        // チャンクを走査して一覧を作る
        // 重い場合は m_openNodes.count() か IsEntityDestroyed() を疑う
        ZoneScopedN("Hierarchy [2]: Chunk Scan & Build");

        // 検索していなければ、ルートから開いているノードだけをたどる
        // 検索中は全エンティティを集め、後で名前で絞る（階層は無視して平らに並べる）
        for (auto &chunkPtr : chunks)
        {
            if (!chunkPtr) continue;
            CCL::ECS::Chunk *chunk = chunkPtr.get();

            TransformComponent *transforms = chunk->HasComponent<TransformComponent>()
                                                 ? chunk->GetComponentArray<TransformComponent>()
                                                 : nullptr;

            for (size_t i = 0; i < chunk->GetEntityCount(); ++i)
            {
                if (chunk->IsEntityDestroyed(i)) continue;
                CCL::ECS::EntityID entity = chunk->GetEntityByIndex(i);
                if (entity == CCL::ECS::InvalidEntityID) continue;

                if (isSearching)
                {
                    m_allEntities.push_back(entity);
                    continue;
                }

                // 親がいない（0）ならルート
                if (!transforms || transforms[i].parent == 0)
                {
                    // GetComponent を使わず、走査中のチャンクの配列から直接読む
                    bool hasChildren = (transforms != nullptr && transforms[i].firstChildID != 0);

                    m_flatList.push_back({entity, 0, hasChildren});

                    // 子がいて、このノードが開いているときだけ子をたどる（閉じていれば何もしない）
                    if (hasChildren && m_openNodes.count(entity) > 0)
                    {
                        FlattenChildren(transforms[i].firstChildID, 1, world, m_flatList);
                    }
                }
            }
        }
    }

    {
        ZoneScopedN("Hierarchy [3]: Search String Process");

        // 検索中だけ、名前の文字列処理を行う
        if (isSearching)
        {
            for (auto e : m_allEntities)
            {
                auto       *nameComp  = world->GetComponent<NameComponent>(e);
                std::string nameLower = (nameComp && strlen(nameComp->name) > 0) ? nameComp->name : "entity " + std::to_string(e);
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

                if (nameLower.find(searchStr) != std::string::npos)
                {
                    m_flatList.push_back({e, 0, false});
                }
            }
        }
    }

    {
        ZoneScopedN("Hierarchy [4]: Draw");

        // ListClipper で、見えている行だけを描く
        ImGui::BeginChild("HierarchyTree", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        ImGuiListClipper clipper;
        clipper.Begin((int)m_flatList.size());
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                DrawFlatNode(m_flatList[i], context);
            }
        }
        clipper.End();

        ImGui::EndChild();
    }

    // ---- アセットのドロップ（プレハブ・モデルの配置） ----
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PREFAB"))
        {
            const char        *prefabPath = (const char *)payload->Data;
            CCL::ECS::EntityID newEntity  = Prefab::SpawnPrefab(*context.world, prefabPath);
            if (newEntity != CCL::ECS::InvalidEntityID)
            {
                // プレハブの実体として印を付ける（レベル保存では参照として保存し、シーン保存では除外する）
                context.world->AddComponent<PrefabInstanceComponent>(
                    newEntity, PrefabInstanceComponent{prefabPath});
                context.SetSingleSelection(newEntity);
            }
        }
        // モデルファイルのドロップ
        //   プレハブを作らずにモデル単体を置けるようにする
        //   アニメーションの確認を、何もないシーンでそのまま行うための入口
        else if (const ImGuiPayload *modelPayload = ImGui::AcceptDragDropPayload("ASSET_MODEL"))
        {
            const char *modelPath = (const char *)modelPayload->Data;

            // 先に読み込んでしまい、アニメーションを持つモデルにだけ
            // AnimatorComponent を付ける。アニメーションシーケンサは
            // ModelComponent と AnimatorComponent の両方がある
            // エンティティだけを対象にするため、ここで揃えておく
            ModelComponent modelComponent(modelPath);

            bool hasAnimation = false;
            if (const Model *model = modelComponent.GetModel())
            {
                if (const ModelResource *resource = model->GetResource())
                {
                    hasAnimation = !resource->GetAnimations().empty();
                }
            }

            // "Valkyra.model.json" は stem が "Valkyra.model" になるので、もう一段落とす
            std::string entityName = std::filesystem::path(modelPath).stem().string();
            if (ModelResource::IsModelSetFile(modelPath))
            {
                entityName = std::filesystem::path(entityName).stem().string();
            }

            auto arch = hasAnimation
                            ? ArchetypeHelper::Generate<TransformComponent, NameComponent, ModelComponent, AnimatorComponent>()
                            : ArchetypeHelper::Generate<TransformComponent, NameComponent, ModelComponent>();

            auto ref = EntityFactory::SpawnNamed(*context.world, entityName.c_str(), arch);
            ref.Set(TransformComponent{}).Set(std::move(modelComponent));

            // SpawnNamed は生成予約を積むだけなので、選択する前に確定させる
            context.world->ScrutinyAndApply();
            context.SetSingleSelection(ref.id);

            CCL_LOG_INFO(LogCategory::Editor, "モデルを配置しました: %s (アニメーション %s)",
                         modelPath, hasAnimation ? "あり" : "なし");
        }
        ImGui::EndDragDropTarget();
    }

    // ---- 背景へのエンティティのドロップ（親の解除） ----
    // 残りの領域をダミーで埋め、ドロップを受けられるようにする
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID"))
        {
            CCL::ECS::EntityID draggedEntity = *(const CCL::ECS::EntityID *)payload->Data;

            // 親を 0（なし）にするよう予約する
            if (context.world->HasComponent<PendingParentComponent>(draggedEntity))
            {
                context.world->GetComponent<PendingParentComponent>(draggedEntity)->parentID = 0;
            }
            else
            {
                context.world->AddComponent<PendingParentComponent>(draggedEntity);
            }
            CCL_LOG_INFO(LogCategory::Editor, "Unparenting: %s is now root",
                         CCL::EntityLabel(*context.world, draggedEntity).c_str());
        }
        ImGui::EndDragDropTarget();
    }
    // 背景をクリックしたら選択を解除する
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
    {
        context.ClearSelection();
    }

    // ---- Ctrl+D で選択中のエンティティ（複数可）を階層ごと複製 ----
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_D, false) &&
        !context.selectedEntities.empty())
    {
        DuplicateSelection(context);
    }
}



void HierarchyWindow::DrawFlatNode(
    const FlatNode &node,
    EditorContext  &context)
{
    // 名前は画面に見えている行の分だけ作る
    std::string name     = "Entity " + std::to_string(node.entityID);
    auto       *nameComp = context.world->GetComponent<NameComponent>(node.entityID);
    if (nameComp && strlen(nameComp->name) > 0)
    {
        name = nameComp->name;
    }

    // 深さに応じて右へずらす（見た目だけツリーにする）
    ImGui::Indent(node.depth * ImGui::GetTreeNodeToLabelSpacing());

    // NoTreePushOnOpen を必ず付ける。ImGui の ID スタックに積まず、
    // 見た目だけのツリーノードにする（階層は depth で表している）
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    if (!node.hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    if (context.IsSelected(node.entityID)) flags |= ImGuiTreeNodeFlags_Selected;

    // 開閉状態は m_openNodes が持つので、毎フレーム ImGui に指定する
    bool isOpen = m_openNodes.count(node.entityID) > 0;
    ImGui::SetNextItemOpen(isOpen);

    ImGui::TreeNodeEx((void *)(uintptr_t)node.entityID, flags, "%s", name.c_str());

    // 矢印をクリックして開閉したら m_openNodes に反映する
    if (ImGui::IsItemToggledOpen())
    {
        if (isOpen)
            m_openNodes.erase(node.entityID);
        else
            m_openNodes.insert(node.entityID);
    }

    // ---- ドラッグの送り元 ----
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("DND_ENTITY_ID", &node.entityID, sizeof(CCL::ECS::EntityID));

        // ドラッグ中にカーソルの横に出す表示
        //   EntityID は 64bit。%u は 4byte しか読まないため、上位32bit の世代が
        //   落ちて「使い回されたIDが同じ番号に見える」表示バグになる（可変長引数なので
        //   コンパイラも警告を出さない）。%llu + 明示キャストで受ける
        ImGui::Text("Drop Entity: %llu", static_cast<unsigned long long>(node.entityID));

        ImGui::EndDragDropSource();
    }

    // ---- ドロップ先（親子付け） ----
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID"))
        {
            CCL::ECS::EntityID draggedEntity = *(const CCL::ECS::EntityID *)payload->Data;
            CCL::ECS::EntityID targetParent  = node.entityID;

            // 自分自身の下と、自分の子孫の下には入れられない（循環するため）
            if (draggedEntity != targetParent && !IsDescendantOf(targetParent, draggedEntity, context.world))
            {
                // PendingParentComponent で親子付けを予約する
                if (context.world->HasComponent<PendingParentComponent>(draggedEntity))
                {
                    context.world->GetComponent<PendingParentComponent>(draggedEntity)->parentID = targetParent;
                }
                else
                {
                    PendingParentComponent ppc;
                    ppc.parentID = targetParent;
                    context.world->AddComponent<PendingParentComponent>(draggedEntity, ppc);
                }
                CCL_LOG_INFO(LogCategory::Editor, "Parenting: %s is now child of %s",
                             CCL::EntityLabel(*context.world, draggedEntity).c_str(),
                             CCL::EntityLabel(*context.world, targetParent).c_str());
            }
        }

        ImGui::EndDragDropTarget();
    }

    // ---- クリックでの選択 ----
    // ボタンを離した瞬間に、ドラッグしておらず、開閉の矢印でもなければ選択する
    // （押した瞬間に選択すると、ドラッグを始めただけで選択が変わる）
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        // io.MouseDragThreshold（既定 6px 程度）を超えて動かしていたらドラッグとみなす
        if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            if (!ImGui::IsItemToggledOpen())
            {
                ImGuiIO &io = ImGui::GetIO();
                if (io.KeyCtrl)
                {
                    // Ctrl+クリック: 選択に追加 / 選択から外す
                    context.ToggleSelection(node.entityID);
                }
                else if (io.KeyShift && context.selectedEntity != CCL::ECS::InvalidEntityID)
                {
                    // Shift+クリック: 主選択からこのノードまでを範囲選択
                    SelectRangeTo(node.entityID, context);
                }
                else
                {
                    context.SetSingleSelection(node.entityID);
                }
            }
        }
    }

    // ---- 右クリックメニュー ----
    // 未選択のノードを右クリックしたら、まずそれを単一選択にする
    // （選択済みのノードを右クリックしたときは選択を保つ）
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        if (!context.IsSelected(node.entityID))
        {
            context.SetSingleSelection(node.entityID);
        }
    }

    if (ImGui::BeginPopupContextItem())
    {
        // 複数選択中は、まとめて操作されることを見出しで示す
        const size_t selCount = context.selectedEntities.size();
        if (selCount > 1)
        {
            ImGui::TextDisabled("%zu 個を選択中", selCount);
            ImGui::Separator();
        }

        // 親から外してルートにする（選択中すべて）
        if (ImGui::MenuItem("Unparent (Make Root)"))
        {
            for (CCL::ECS::EntityID id : context.selectedEntities)
            {
                if (context.world->HasComponent<PendingParentComponent>(id))
                {
                    context.world->GetComponent<PendingParentComponent>(id)->parentID = 0;
                }
                else
                {
                    context.world->AddComponent<PendingParentComponent>(id); // 既定の parentID=0 は親なし
                }
            }
            CCL_LOG_INFO(LogCategory::Editor, "Unparented %zu entities.", selCount);
        }

        if (ImGui::MenuItem("複製", "Ctrl+D"))
        {
            DuplicateSelection(context);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete Entity"))
        {
            // 削除中に選択セットが変わっても困らないよう、コピーしてから回す
            std::vector<CCL::ECS::EntityID> toDelete = context.selectedEntities;
            for (CCL::ECS::EntityID id : toDelete)
            {
                context.world->Destroy(id);
            }
            context.ClearSelection();
        }
        ImGui::EndPopup();
    }

    ImGui::Unindent(node.depth * ImGui::GetTreeNodeToLabelSpacing());
}



// ---------------------------------------------------------------------------
// 選択と複製
// ---------------------------------------------------------------------------

void HierarchyWindow::FlattenChildren(
    CCL::ECS::EntityID     firstChildID,
    int                    depth,
    CCL::ECS::World       *world,
    std::vector<FlatNode> &outFlatList)
{
    CCL::ECS::EntityID childID = firstChildID;

    // 兄弟リンク（nextSiblingID）をたどり、子を全部一覧に足す
    while (childID != 0)
    {
        auto *childTrans = world->GetComponent<TransformComponent>(childID);
        if (!childTrans) break;

        bool hasChildren = (childTrans->firstChildID != 0);
        outFlatList.push_back({childID, depth, hasChildren});

        // この子も開いていれば、孫をたどる
        if (hasChildren && m_openNodes.count(childID) > 0)
        {
            FlattenChildren(childTrans->firstChildID, depth + 1, world, outFlatList);
        }

        childID = childTrans->nextSiblingID;
    }
}



void HierarchyWindow::SelectRangeTo(CCL::ECS::EntityID clicked, EditorContext &context)
{
    CCL::ECS::EntityID anchor = context.selectedEntity;

    int anchorIdx = -1, clickedIdx = -1;
    for (int i = 0; i < (int)m_flatList.size(); ++i)
    {
        if (m_flatList[i].entityID == anchor) anchorIdx = i;
        if (m_flatList[i].entityID == clicked) clickedIdx = i;
    }

    // どちらかが一覧にない（検索で隠れているなど）なら、clicked を追加選択するだけにする
    if (anchorIdx < 0 || clickedIdx < 0)
    {
        context.AddSelection(clicked);
        return;
    }

    int lo = (std::min)(anchorIdx, clickedIdx);
    int hi = (std::max)(anchorIdx, clickedIdx);

    // 選択セットを範囲に置き換え、主選択は clicked にする
    context.selectedEntities.clear();
    for (int i = lo; i <= hi; ++i)
    {
        context.selectedEntities.push_back(m_flatList[i].entityID);
    }
    context.selectedEntity = clicked;
}



void HierarchyWindow::DuplicateSelection(EditorContext &context)
{
    // 祖先も選択されているものは、祖先の複製に含まれるので除く
    std::vector<CCL::ECS::EntityID> roots = context.GetTopLevelSelection();
    std::vector<CCL::ECS::EntityID> newRoots;
    for (CCL::ECS::EntityID src : roots)
    {
        auto created = SceneSerializer::DuplicateHierarchy(context.world, src);
        if (!created.empty()) newRoots.push_back(created[0]);
    }
    if (!newRoots.empty())
    {
        // 複製したルートを新しい選択セットにする
        context.ClearSelection();
        for (CCL::ECS::EntityID r : newRoots)
            context.AddSelection(r);
        CCL_LOG_INFO(LogCategory::Editor, "Duplicated %zu entities", newRoots.size());
    }
}



bool HierarchyWindow::IsDescendantOf(
    CCL::ECS::EntityID targetEntity,
    CCL::ECS::EntityID ancestorEntity,
    CCL::ECS::World   *world)
{
    if (targetEntity == ancestorEntity) return true;

    auto *trans = world->GetComponent<TransformComponent>(ancestorEntity);
    if (!trans) return false;

    // 子とその兄弟を順に調べ、それぞれの子孫も再帰で調べる
    CCL::ECS::EntityID childID = trans->firstChildID;
    while (childID != 0)
    {
        if (IsDescendantOf(targetEntity, childID, world)) return true;
        auto *childTrans = world->GetComponent<TransformComponent>(childID);
        childID          = childTrans ? childTrans->nextSiblingID : 0;
    }
    return false;
}
