// IFormation 方式の陣形計算システム（switch廃止版）。
//        各ドローンの currentFormation を見て registry から陣形を引き、
//        targetPosition を計算する。移動は既存 DroneMovementSystem が担当。
#pragma once
#include <string>
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

// データ実装（JSON）を持つ陣形1件。ファイルは Assets/Formations/<name>.json。
struct FormationSpecAsset
{
    DroneFormationType type;
    const char        *name;
};

// ドローンの目標座標を陣形に応じて計算するシステム。
class DroneFormationSystem : public CCL::ECS::IfSystem<DroneFormationSystem,
                                 CCL::ECS::Write<DroneComponent>>
{
    using Base = CCL::ECS::IfSystem<DroneFormationSystem, CCL::ECS::Write<DroneComponent>>;

  public:
    DroneFormationSystem() : IfSystem("DroneFormationSystem"){}

    void Initialize() override;

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    //        DroneCommand が drone.currentFormation を書き、こちらがそれを読んでスロットを計算する。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BehaviorTreeSystem", "DroneCommandSystem", "SquadUpdateSystem"};
    }

    // MakeContext がボスの Transform と BossCommandComponent を View で引くので申告する。
    //   テンプレート引数には足せない（ForEach の絞り込みまで変わってしまうため）。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = Base::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<BossCommandComponent>::ID());
        types.push_back(CCL::ECS::TypeInfo<TransformComponent>::ID());
        return types;
    }

    void Update(float dt) override;

    // ------------------------------------------------------------------
    // エディタ（FormationEditorWindow）から使う口
    //   陣形の評価に必要な材料はこのシステムが持っているので、
    //   ウィンドウ側で世界の探索をやり直さずに借りられるようにする。
    // ------------------------------------------------------------------

    /**
     * ボス位置・プレイヤー位置など「場の情報」を組み立てる。
     * @param dt 陣形へ渡す経過時間。0 を渡すと進行度が進まないので、
     *           形を止めたまま評価したいとき（実装の照合）に使える。
     */
    CCL::AI::Formation::FormationContext MakeContext(float dt) const;

    // Assets/Formations/*.json を読み、データ実装として registry へ登録する。
    void LoadSpecAssets();

    // データ実装を持つ陣形の一覧。エディタが編集対象の候補に使う。
    static const FormationSpecAsset *SpecAssets();
    static int                       SpecAssetCount();
    static std::string               SpecAssetPath(const char *name);

    /**
     * 進行度をエディタ側が持つモード。負を渡すと通常動作へ戻る。
     *
     * 陣形は Calculate の中で attackProgress を dt×speed だけ進めてから
     * 形を作る。この2つが同じ関数にあるので、外から進行度を書いても
     * 次の Calculate が上書きしてしまう。
     * そこで dt=0 を渡す。陣形は進めようがなくなり、進行度の持ち主が
     * エディタへ移る。止めた形をそのまま実機で見られる。
     *
     * @param progress 0〜1 で固定したい進行度。負なら解除。
     */
    void  SetDebugProgress(float progress) { m_debugProgress = progress; }
    float DebugProgress() const { return m_debugProgress; }

  private:
    float m_elapsed       = 0.0f;  // 回転アニメ用の経過時間
    float m_debugProgress = -1.0f; // 負 = 通常動作（SetDebugProgress を参照）
};
