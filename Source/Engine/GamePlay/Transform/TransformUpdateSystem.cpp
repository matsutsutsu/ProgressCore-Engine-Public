#include "TransformUpdateSystem.h"
#include "ECS/Core/CCL_World.h"

#include "Engine/GamePlay/Transform/PendingParentComponent.h"

// 各システムの .cpp ファイルの上部に追加
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;

TransformUpdateSystem::TransformUpdateSystem() : IfSystem("TransformUpdateSystem") {}

void TransformUpdateSystem::Update(float dt)
{
    // -----------------------------------------------------------------------
    // 1. 構造変更フェーズ (メインスレッド必須)
    // -----------------------------------------------------------------------

    // 生成時に予約された親子関係の適用
    ProcessPendingParents();

    // 壊れたリンク（親がいるのに親から認識されていない子）の修復
    RepairBrokenLinks();

    // -----------------------------------------------------------------------
    // 2. 行列更新フェーズ (並列処理可能)
    // -----------------------------------------------------------------------

    // 共通ロジック: ラムダ式で定義して使い回す
    auto updateLogic = [&](EntityID id, TransformComponent &trans) {
        // 親がいるEntityは、Rootからの再帰呼び出しの中で処理されるのでスキップ
        // (起点は常に親を持たないRootエンティティ)
        if (trans.parentID != 0) return;

        // 自分がStaticでないなら行列更新
        if (!trans.isStatic) {
            trans.UpdateMatrix(XMMatrixIdentity());
        }

        // 子供がいる場合、再帰処理を開始
        // (自分がStaticでも、子供が動く可能性があるので伝播させる)
        if (trans.firstChildID != 0) {
            XMMATRIX myWorldMatrix = XMLoadFloat4x4(&trans.worldMatrix);
            UpdateTransformRecursive(trans.firstChildID, myWorldMatrix);
        }
    };


    // ★マルチスレッド実行
    ForEachWithIDParallel(updateLogic);
    
}

// ★追加: 実装
void TransformUpdateSystem::ProcessPendingParents()
{
    // PendingParentComponent を持っているエンティティを探す
    // (IfSystemのフィルタとは別に、Worldから直接検索するか、専用のViewを使う)
    // ここでは簡易的にWorld経由で全走査またはViewを使用
    // ※Systemのテンプレート引数にPendingParentComponentを追加していないので、
    //   _world->View<PendingParentComponent>() を使います。

    // 注意: ループ中にコンポーネント削除(RemoveComponent)を行うため、
    // イテレータ無効化を避けるために一度リストに集めるのが安全です。
    std::vector<EntityID> pendingEntities;
    for (auto id : _world->View<PendingParentComponent>()) {
        pendingEntities.push_back(id);
    }

    for (auto id : pendingEntities) {
        auto *pending = _world->GetComponent<PendingParentComponent>(id);
        if (pending && pending->parentID != InvalidEntityID) {

            // 親と子が存在するか確認 (1フレーム経過していれば存在するはず)
            if (_world->IsEntityValid(pending->parentID) && _world->IsEntityValid(id)) {
                // 親子付け実行！
                SetParent(*_world, id, pending->parentID);
            }
        }

        // 用が済んだら予約票を捨てる
        _world->RequestRemoveComponent<PendingParentComponent>(id);
    }
}

// リンク切れ修復の実装
void TransformUpdateSystem::RepairBrokenLinks()
{
    // 構造を変更する可能性があるのでシングルスレッド(ForEach)で実行
    ForEachWithID([&](EntityID id, TransformComponent &trans) {
        // 親が設定されている場合のみチェック
        if (trans.parentID != 0) {
            auto *parentTrans = _world->GetComponent<TransformComponent>(trans.parentID);

            // 親が存在するのに、親から認識されていない（リンク切れ）場合を判定
            bool isOrphan = false;
            if (parentTrans) {
                // 親にとっての最初の子が自分ならOK
                bool isFirstChild = (parentTrans->firstChildID == id);
                // 兄弟がいるならリストにいるのでOK
                bool hasSibling = (trans.nextSiblingID != 0 || trans.prevSiblingID != 0);

                if (!isFirstChild && !hasSibling) {
                    isOrphan = true;
                }
            }

            if (isOrphan) {
                // 自動修復: 正しい親子関係を結び直す (書き込み発生)
                SetParent(*_world, id, trans.parentID);
            }
        }
    });
}

void TransformUpdateSystem::UpdateTransformRecursive(
    EntityID currentID, const XMMATRIX &parentMatrix)
{

   // 子要素を「兄弟(Sibling)リンク」を使って横に走査するループ
    EntityID entityItr = currentID;

    // 安全対策: 無限ループ防止 (循環参照対策)
    int       safetyCount  = 0;
    const int MAX_SIBLINGS = 10000;

    while (entityItr != 0) {
        if (++safetyCount > MAX_SIBLINGS) break;

        auto *trans = _world->GetComponent<TransformComponent>(entityItr);
        if (!trans) break; // エラーガード

        // 更新処理をする「前」に、次の人を確保しておく
        // もし再帰処理中に構造が変わっても、ループだけは安全に回せるようにするため
        EntityID nextEntity = trans->nextSiblingID;

        // 1. 自分（子）の行列更新
        // ★重要: 子は親の影響を受けるため、自身のisStaticに関わらず行列計算を行うのが一般的
        // もし「子は絶対動かない」という仕様なら弾くが、通常は親につくなら動くべき
        trans->UpdateMatrix(parentMatrix);

        // 2. さらにその子（孫）がいれば深さ優先で再帰
        if (trans->firstChildID != 0) {
            XMMATRIX myWorld = XMLoadFloat4x4(&trans->worldMatrix);
            UpdateTransformRecursive(trans->firstChildID, myWorld);
        }

        // 3. 次の兄弟へ移動
        entityItr = nextEntity;
    }
}

// ---------------------------------------------------------
// ヘルパー関数: 親子関係の構築
// ---------------------------------------------------------

void TransformUpdateSystem::SetParent(Core::World &world, EntityID childID, EntityID newParentID)
{
    // 自己参照チェック
    if (childID == newParentID) return;
    if (!world.IsEntityValid(childID)) return;

    // 1. まず現在の親から切り離す
    DetachChild(world, childID);

    // 親を「無し(0)」にするだけならここで終了
    if (newParentID == 0) return;
    if (!world.IsEntityValid(newParentID)) return;

    auto *childTrans  = world.GetComponent<TransformComponent>(childID);
    auto *parentTrans = world.GetComponent<TransformComponent>(newParentID);

    if (!childTrans || !parentTrans) return;

    // 2. 新しい親を設定
    childTrans->parentID = newParentID;

    // 3. 親の子リストの「先頭」に自分を挿入する (Insert Head)
    //    双方向リストへの挿入処理: O(1)

    EntityID oldFirstChildID = parentTrans->firstChildID;

    // 自分の次 = 親の元々の先頭
    childTrans->nextSiblingID = oldFirstChildID;
    // 自分の前 = 無し (先頭になるため)
    childTrans->prevSiblingID = 0;

    // もし親に元々子供がいたら、その子供の「前」を自分にする
    if (oldFirstChildID != 0) {
        auto *oldFirstTrans = world.GetComponent<TransformComponent>(oldFirstChildID);
        if (oldFirstTrans) {
            oldFirstTrans->prevSiblingID = childID;
        }
    }

    // 親の先頭ポインタを自分に更新
    parentTrans->firstChildID = childID;
}

void TransformUpdateSystem::DetachChild(Core::World &world, EntityID childID)
{
    auto *childTrans = world.GetComponent<TransformComponent>(childID);
    if (!childTrans || childTrans->parentID == 0) return;

    EntityID parentID    = childTrans->parentID;
    auto    *parentTrans = world.GetComponent<TransformComponent>(parentID);

    // 1. 親から見たリンクの修正
    if (parentTrans) {
        // 親の「最初の子」が自分なら、自分の「次」を新しい「最初の子」にする
        if (parentTrans->firstChildID == childID) {
            parentTrans->firstChildID = childTrans->nextSiblingID;
        }
    }

    // 2. 兄弟間のリンク修正
    // 「前の兄弟」がいる場合、その「次」を自分の「次」につなぐ
    if (childTrans->prevSiblingID != 0) {
        auto *prevTrans = world.GetComponent<TransformComponent>(childTrans->prevSiblingID);
        if (prevTrans) {
            prevTrans->nextSiblingID = childTrans->nextSiblingID;
        }
    }

    // 「次の兄弟」がいる場合、その「前」を自分の「前」につなぐ
    if (childTrans->nextSiblingID != 0) {
        auto *nextTrans = world.GetComponent<TransformComponent>(childTrans->nextSiblingID);
        if (nextTrans) {
            nextTrans->prevSiblingID = childTrans->prevSiblingID;
        }
    }

    // 3. 自分のリンク情報をクリア
    childTrans->parentID      = 0;
    childTrans->prevSiblingID = 0;
    childTrans->nextSiblingID = 0;
}

// 階層構造を考慮した削除の実装
void TransformUpdateSystem::DestroyEntityWithHierarchy(
    CCL::ECS::Core::World &world, CCL::ECS::EntityID entity, bool destroyChildren)
{
    if (!world.IsEntityValid(entity)) return;

    auto *trans = world.GetComponent<TransformComponent>(entity);
    if (!trans) {
        // Transformがないなら普通に消すだけ
        world.RequestDestroyEntity(entity);
        return;
    }

    // 1. 子供の処理（再帰的削除）
    if (destroyChildren) {
        CCL::ECS::EntityID childID = trans->firstChildID;
        while (childID != 0) {
            auto *childTrans = world.GetComponent<TransformComponent>(childID);
            if (!childTrans) break;

            CCL::ECS::EntityID nextChild = childTrans->nextSiblingID;

            // 子供自身もこの関数を呼んで、そのまた子供も消す (再帰)
            DestroyEntityWithHierarchy(world, childID, true);

            childID = nextChild;
        }
    }
    else {
        // 子供を道連れにしない場合、子供を「親なし」にして逃がす
        CCL::ECS::EntityID childID = trans->firstChildID;
        while (childID != 0) {
            auto *childTrans = world.GetComponent<TransformComponent>(childID);
            if (!childTrans) break;

            CCL::ECS::EntityID nextChild = childTrans->nextSiblingID;

            // 親から切り離す（これでリスト構造が安全に更新される）
            DetachChild(world, childID);

            childID = nextChild;
        }
    }

    // 2. 親・兄弟からの切り離し (自分をリストから安全に抜く)
    // これを忘れると、前の兄弟が「死んだ自分」を nextSibling として参照し続けてしまう
    DetachChild(world, entity);

    // 3. 最後に本体の削除予約
    world.RequestDestroyEntity(entity);
}

// ★修正: 削除予約ではなく、タグの付与を行う
void TransformUpdateSystem::MarkForDestruction(
    CCL::ECS::Core::World &world, CCL::ECS::EntityID entity, bool destroyChildren)
{
    if (!world.IsEntityValid(entity)) return;

    // 既にタグが付いているなら何もしない（無限ループ防止）
    if (world.HasComponent<Tag::DestroyTag>(entity)) return;

    // 1. 自分に死の宣告 (タグ付与)
    world.AddComponent<Tag::DestroyTag>(entity);

    auto *trans = world.GetComponent<TransformComponent>(entity);
    if (!trans) return;

    // 2. 子供への伝播
    if (destroyChildren) {
        CCL::ECS::EntityID childID = trans->firstChildID;
        while (childID != 0) {
            auto *childTrans = world.GetComponent<TransformComponent>(childID);
            if (!childTrans) break;

            CCL::ECS::EntityID nextChild = childTrans->nextSiblingID;

            // 再帰的にタグをつける
            MarkForDestruction(world, childID, true);

            childID = nextChild;
        }
    }
    else {
        // 子供を道連れにしない場合、子供を「親なし」にして逃がす
        // 注意: HierarchyCleanupSystemでDetachされるので、ここでは何もしなくて良いが、
        // 明示的に親子関係を切りたい場合は DetachChild を呼ぶ。
        // タグシステムの場合は「CleanupSystemが実行されるまで構造は維持される」のが正しい姿なので、
        // ここでは親子のリンクはいじらないのが正解。
        // しかし、親だけ死んで子が残る場合、CleanupSystemの実行時に「親が消える」ので
        // 自動的に子は孤立する。

        // ただし、親がタグ付きで、子がタグ無しの場合の挙動として
        // 「親が消える前に子をDetach」しておく必要がある。
        // それは HierarchyCleanupSystem::Update 内の DetachChild で行われるので、
        // ここでは何もしなくてOK！
    }
}


// 自動登録。SystemGroups.h に書く必要はなくなります
REGISTER_LOGIC_SYSTEM(TransformUpdateSystem, Priority::LogicStage::L04_Physics);