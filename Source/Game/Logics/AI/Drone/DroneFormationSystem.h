/**
 * IFormation 方式の陣形計算システム（switch廃止版）。
 *        各ドローンの currentFormation を見て registry から陣形を引き、
 *        targetPosition を計算する。移動は既存 DroneMovementSystem が担当。
 */
#pragma once
#include <string>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"

/// データ実装（JSON）を持つ陣形1件。ファイルは Assets/Formations/<name>.json。
struct FormationSpecAsset
{
    DroneFormationType type;
    const char        *name;
};

/// ドローンの目標座標を陣形に応じて計算するシステム。
class DroneFormationSystem : public CCL::ECS::IfSystem<DroneFormationSystem,
                                 CCL::ECS::Write<DroneComponent>>
{
  public:
    DroneFormationSystem() : IfSystem("DroneFormationSystem"){}

    void Initialize() override;

    /// 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    ///        DroneCommand が drone.currentFormation を書き、こちらがそれを読んでスロットを計算する。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"DroneCommandSystem", "SquadUpdateSystem"};
    }

    void Update(float dt) override;

    // ------------------------------------------------------------------
    // エディタ（FormationEditorWindow）から使う口
    //   陣形の評価に必要な材料はこのシステムが持っているので、
    //   ウィンドウ側で世界の探索をやり直さずに借りられるようにする。
    // ------------------------------------------------------------------

    /// ボス位置・プレイヤー位置など「場の情報」を組み立てる。
    /// @param dt 陣形へ渡す経過時間。0 を渡すと進行度が進まないので、
    ///           形を止めたまま評価したいとき（実装の照合）に使える。
    CCL::AI::Formation::FormationContext MakeContext(float dt) const;

    /// Assets/Formations/*.json を読み、データ実装として registry へ登録する。
    void LoadSpecAssets();

    /// データ実装を持つ陣形の一覧。エディタが編集対象の候補に使う。
    static const FormationSpecAsset *SpecAssets();
    static int                       SpecAssetCount();
    static std::string               SpecAssetPath(const char *name);

  private:
    float m_elapsed = 0.0f; // 回転アニメ用の経過時間
};
