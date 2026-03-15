#include "EnemySpawnSystem.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Game/Logics/Character/Enemy/EnemyComponent.h" // 敵の数カウント用
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Game/Logics/World/StageMap.h"
#include <imgui.h>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// ===================================================================================
// ファイル名: EnemySpawnSystem.cpp
// クラス名: EnemySpawnSystem
//
// ※ 今は使っていないです
//
// 【役割・責任】
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

using namespace DirectX::SimpleMath;

 std::vector<CCL::ECS::TypeID> EnemySpawnSystem::GetWriteTypes() const
{
     return {CCL::ECS::TypeInfo<TransformComponent>::ID(),
         CCL::ECS::TypeInfo<EnemyComponent>::ID()};

 }

void EnemySpawnSystem::Update(float dt)
{
    if (!_world->HasResource<StageMap>()) return;
    auto &map = _world->GetResource<StageMap>();
    if (!isActive) return; // Guiでオフにされていたらリターン

    // プレイヤー位置取得
    Vector3 playerPos;
    auto    view = _world->View<PlayerComponent, TransformComponent>();
    if (view.empty()) return;

    playerPos = _world->GetComponent<TransformComponent>(view[0])->position;

    // --- 距離ベースのスポーン処理 (Lazy Spawn) ---
    // スポーン判定距離の2乗 (例: 30m以内なら湧く)
    const float SPAWN_DIST    = 30.0f;
    const float SPAWN_DIST_SQ = SPAWN_DIST * SPAWN_DIST;

    // 全スポーンポイントをチェック (数百個程度なら毎フレーム回しても負荷は軽微)
    for (auto &info : map.enemySpawns) {

        // 既に湧いているならスキップ
        if (info.isSpawned) continue;

        // 距離チェック
        float distSq = Vector3::DistanceSquared(playerPos, info.position);
        if (distSq <= SPAWN_DIST_SQ) {

            // --- 範囲内なので生成を実行 ---
            SpawnEnemy(info);

            // 「生成済み」にマークして、二重生成を防ぐ
            info.isSpawned = true;
        }
    }
}

// スポーン処理の本体
void EnemySpawnSystem::SpawnEnemy(const EnemySpawnInfo &info)
{
    // int型から EnemyAIType への変換
    EnemyAIType aiType     = (EnemyAIType)info.type;
    std::string prefabPath = "";

    // ★修正: Enum ID ではなく、対応する JSONファイルのパス を指定する
    switch (aiType) {
    case EnemyAIType::Pawn: prefabPath = "Assets/GamePlay/Prefabs/Enemy/EnemyPawn.json"; break;
    case EnemyAIType::Tank: prefabPath = "Assets/GamePlay/Prefabs/Enemy/EnemyTank.json"; break;
    case EnemyAIType::Jumper: prefabPath = "Assets/GamePlay/Prefabs/Enemy/EnemyJumper.json"; break;
    case EnemyAIType::Evader: prefabPath = "Assets/GamePlay/Prefabs/Enemy/EnemyEvader.json"; break;
    case EnemyAIType::Boid: prefabPath = "Assets/GamePlay/Prefabs/Enemy/EnemyBoid.json"; break;
    case EnemyAIType::Fire: prefabPath = "Assets/GamePlay/Prefabs/Enemy/EnemyFire.json"; break;
    default: prefabPath = "Assets/GamePlay/Prefabs/Enemy/EnemyPawn.json"; break;
    }

    if (!prefabPath.empty()) {
        // ★修正: SpawnPrefab を使用 (座標とY軸回転を指定するオーバーロードを使用)
        // ※ Prefab.h に (World&, path, pos, rotationY) のオーバーロードがある前提です
        CCL::ECS::EntityID enemyID =
            Prefab::SpawnPrefab(*_world, prefabPath, info.position, info.rotationY);

        // 生成に成功した場合のみ初期化を行う
        if (enemyID != CCL::ECS::InvalidEntityID) {

            // =========================================================
            //  生成直後に個体差（隊形・速度）をランダム設定する
            // =========================================================
            if (auto *enemy = _world->GetComponent<EnemyComponent>(enemyID)) {
                // 1. 隊形スロットのランダム化 (0:正面, 1:右, 2:左, 3:裏)
                // これにより、生まれた瞬間から別々のルートを通ろうとする
                enemy->formationSlot = rand() % 4;

                // 2. 速度の個体差 (0.8倍 ~ 1.2倍)
                // 同じルートでも足の速さが違うため、団子にならず縦に伸びる
                enemy->speedRandomness = 0.8f + (static_cast<float>(rand()) / RAND_MAX) * 0.4f;

                // 3. 動きのノイズ用オフセット
                // Evaderの「様子見」タイミングなどをズラす
                enemy->noiseOffset = static_cast<float>(rand()) / RAND_MAX * 100.0f;
            }
        }
    }
}

// -------------------------------------------------------------
// デバッグGUIの実装
// -------------------------------------------------------------
void EnemySpawnSystem::OnGui()
{
    // 1. 全体スイッチ & 状態表示
    ImGui::Checkbox("Spawn Active (Auto)", &isActive);

    static int enemyCount = 0;
    if (ImGui::Button("Count Enemies")) {
        enemyCount = 0;
        auto view  = _world->View<EnemyComponent>();
        enemyCount = static_cast<int>(view.size());
    }
    ImGui::SameLine();
    ImGui::Text("Count: %d", enemyCount);

    ImGui::Separator();

    // 2. スポーン設定 (ランダム or 固定)
    ImGui::Text("Spawn Type Logic:");
    ImGui::Checkbox("Use Fixed Type", &useFixedType);

    if (useFixedType) {
        const char *names[] = {"Pawn (Zako)",
            "Tank (Charge)",
            "Jumper (Jump)",
            "Evader (Dodge)",
            "Boid (Swarm)",
            "Fire (Range)"};
        ImGui::Combo("Select Type", &fixedTypeIndex, names, IM_ARRAYSIZE(names));
    }
    else {
        ImGui::TextDisabled("Mode: Random Probabilities");
    }

    ImGui::Separator();

    // 3. パラメータ調整
    if (ImGui::CollapsingHeader("Auto Spawn Settings")) {
        ImGui::DragFloat("Interval (sec)", &spawnInterval, 0.05f, 0.1f, 10.0f);
        ImGui::SliderInt("Count / Tick", &spawnCount, 1, 10);
        ImGui::Text("Spawn Range:");
        ImGui::DragFloatRange2("Min/Max", &minSpawnDist, &maxSpawnDist, 1.0f, 5.0f, 100.0f);
    }

    ImGui::Separator();

    //// 4. 強制操作ボタン
    //// このボタンは「Auto Spawn」がオフでも動作します
    // if (ImGui::Button("Force Spawn (Near Player)")) {
    //     auto view = _world->View<PlayerComponent, TransformComponent>();
    //     if (!view.empty()) {
    //         auto *t = _world->GetComponent<TransformComponent>(view[0]);
    //         if (_world->HasResource<StageMap>())
    //             TrySpawn(_world->GetResource<StageMap>(), t->position);
    //     }
    // }

    ImGui::SameLine();

    // 赤色ボタンにする
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Kill All")) {
        auto view = _world->View<EnemyComponent>();
        for (auto id : view) {
            _world->Destroy(id);
        }
    }
    ImGui::PopStyleColor();
}

REGISTER_LOGIC_SYSTEM(EnemySpawnSystem, Priority::LogicStage::Input);
