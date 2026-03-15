#pragma once
#include <SimpleMath.h>
#include <json.hpp> // nlohmann/json
#include <string>
#include <vector>
#include <limits>


// ===================================================================================
// ファイル名: StageMap.h
// クラス名: StageMap (Resource)
//
// 【役割・責任】
//   マップの「情報」を管理するデータベースです。
//   「どこが壁で、どこが床か」「敵がどこにいるか」といったデータを保持します。
//
// 【主な機能】
//   1. 地形データ管理:
//      - 2次元のグリッドデータを1次元配列で高速に管理します。
//      - IsWall(x, z): 指定した場所が壁かどうかを即座に判定します。
//
//   2. AI用ナビゲーション (FlowField):
//      - 敵がプレイヤーを追いかけるための「匂いの地図（フローフィールド）」を持ちます。
//      - これにより、何百体の敵がいても処理負荷ゼロで最短ルートを検索できます。
//
//   3. 座標変換:
//      - グリッド座標 (x:10, z:5) <--> ワールド座標 (x:21.0f, z:11.0f) の相互変換を行います。
// ===================================================================================


using json = nlohmann::json;

// タイルの種類定義
enum class TileType : uint8_t {
    Floor = 0,  // 床（移動可能）
    Wall  = 1,  // 壁（移動不可・遮蔽物）
    Hole  = 2,  // 穴（落下・移動不可・視線は通る）
    None  = 255 // 範囲外
};

// レイキャスト結果情報
struct MapRaycastHit {
    bool                         isHit    = false;
    float                        distance = 0.0f;
    DirectX::SimpleMath::Vector3 point;
    DirectX::SimpleMath::Vector3 normal;
};


// 敵のスポーン情報
struct EnemySpawnInfo {
    DirectX::SimpleMath::Vector3 position; // ワールド座標
    int                    type;
    float                        rotationY; // 向き（ラジアン）

    // 生成フラグ
    bool                         isSpawned = false;
};

// ★追加: デバッグ表示用の部屋情報
struct DebugRoomInfo {
    int x, y, w, h;
    int type; // 0:Start, 1:Normal, 2:Boss
};

struct StageMapChunkComponent {};

struct StageMap {
    // 定数定義：マジックナンバーを排除
    static constexpr uint16_t UNREACHABLE_COST  = (std::numeric_limits<uint16_t>::max)();
    static constexpr int      FLOW_FIELD_COUNT  = 4;
    static constexpr float    DEFAULT_CELL_SIZE = 2.0f;

    int   width    = 0;    // 横幅（マス数）
    int   height   = 0;    // 縦幅（マス数）
    float cellSize = DEFAULT_CELL_SIZE; // 1マスのワールドサイズ

    // 地形データ（1次元配列）
    std::vector<TileType> tiles;

    // 未踏フラグ管理用
    std::vector<uint8_t> visitedMask;

    // --- フローフィールド用構造体 ---
    struct FlowField {
        std::vector<uint16_t>                     costMap; // プレイヤーからの距離（コスト）
        std::vector<DirectX::SimpleMath::Vector3> vectors; // 進むべき方向ベクトル

        void Resize(int size);
    };

    // フローフィールドを複数持つ (例: 4つのスロット)
    // 0: Center, 1: Right, 2: Left, 3: Back
    std::vector<FlowField> flowFields;

    // 敵のスポーン地点リスト
    std::vector<EnemySpawnInfo> enemySpawns;

    // デバッグ用に部屋のリストを保存しておく
    std::vector<DebugRoomInfo> debugRooms;

    // プレイヤーの初期スポーン位置
    DirectX::SimpleMath::Vector3 playerSpawnPoint = {0, 0, 0};

    // プレイヤーのスポーン位置の下あたりに追加
    DirectX::SimpleMath::Vector3 gateSpawnPoint = {0, 0, 0};

    // --- メソッド宣言 ---

    void Initialize(int w, int h, float size);

    // 座標変換系（頻繁に呼ぶのでインライン推奨だが、今回は分離リクエストに従いCPPへ移動も可。
    // ここではパフォーマンスのため簡単なものはヘッダーに残すのが一般的ですが、
    // 分離をご希望ならCPPに移します。今回は可読性重視でヘルパーはここに残し、重い処理をCPPにします）

    // Grid座標(x, z) -> 配列インデックス
    int GetIndex(int x, int z) const
    {
        if (x < 0 || x >= width || z < 0 || z >= height) return -1;
        return z * width + x;
    }

    // ワールド座標 -> Grid座標
    void WorldToGrid(const DirectX::SimpleMath::Vector3 &pos, int &outX, int &outZ) const
    {
        outX = static_cast<int>(std::floor(pos.x / cellSize));
        outZ = static_cast<int>(std::floor(pos.z / cellSize));
    }

    // Grid座標 -> ワールド中心座標
    DirectX::SimpleMath::Vector3 GridToWorld(int x, int z) const
    {
        return DirectX::SimpleMath::Vector3(
            x * cellSize + cellSize * 0.5f, 0.0f, z * cellSize + cellSize * 0.5f);
    }

    // 壁判定
    bool IsWall(int x, int z) const
    {
        int idx = GetIndex(x, z);
        if (idx == -1) return true;
        return tiles[idx] == TileType::Wall;
    }



    // 指定したスロット(index)に対して更新を行うように引数を追加
    void UpdateFlowField(int slotIndex, int targetX, int targetZ);

    // グリッド専用レイキャスト
    MapRaycastHit Raycast(const DirectX::SimpleMath::Vector3 &origin,
        const DirectX::SimpleMath::Vector3                   &direction,
        float                                                 maxDistance) const;

    // 補間されたフローベクトルを取得する関数
    DirectX::SimpleMath::Vector3 GetFlowDirection(
        int slotIndex, const DirectX::SimpleMath::Vector3 &worldPos) const;

};