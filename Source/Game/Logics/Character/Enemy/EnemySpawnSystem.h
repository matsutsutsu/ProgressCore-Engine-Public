#pragma once
#include "ECS/System/CCL_System.h"
#include <SimpleMath.h>

// ===================================================================================
// ファイル名: EnemySpawnSystem.h
// クラス名: EnemySpawnSystem
//
// ※ 今は使っていないです
//
// 【役割・責任】
//
//   ・敵の自動スポーンを管理するグローバルシステム
//   ・Vampire Survivors や Noita のような「プレイヤー周辺の未踏地から湧く」ロジックを担当
//
// 【仕様】
//   ・Spawn Logic:
//     1. プレイヤー位置を取得し、周囲の StageMap::visitedMask を「既踏(1)」に更新する。
//     2. 定期的にスポーン試行を行う (spawnInterval)。
//     3. スポーン位置決定アルゴリズム:
//        - プレイヤーを中心に「ドーナツ状の距離 (minDist ~ maxDist)」からランダム座標を選ぶ。
//        - その座標が「床 (TileType::Floor)」かつ「未踏 (visited == 0)」なら採用。
//        - これにより、画面内（既踏）や壁の中には湧かず、常に新しい場所から敵が現れる感覚を作る。
//
//   ・ECS特性:
//     - 特定のコンポーネントをループするのではなく、システム自身がタイマーを持ち、
//       World全体の状態を見て能動的に Entity を生成する。
// ===================================================================================

struct StageMap;
struct EnemySpawnInfo;

class EnemySpawnSystem : public CCL::ECS::IfSystem<EnemySpawnSystem> {
    float spawnTimer = 0.0f;

    // 公開設定（Guiでいじる用）
    bool  isActive      = true;  // スポーンシステムが有効か
    float spawnInterval = 1.0f;  // スポーン間隔
    float minSpawnDist  = 15.0f; // 最小距離
    float maxSpawnDist  = 30.0f; // 最大距離
    int   spawnCount    = 1;     // 1回に湧く数

    // ★追加: デバッグ用の固定スポーン設定
    bool useFixedType   = false; // 固定タイプで湧かせるか？
    int  fixedTypeIndex = 0;     // 選択中の敵タイプ (0:Pawn, 1:Tank...)

     std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

  public:
    EnemySpawnSystem() : IfSystem("EnemySpawnSystem") {}

    void Update(float dt) override;

    void OnGui() override;

  private:
    void SpawnEnemy(const struct EnemySpawnInfo &info); // 前方宣言が必要かも
};