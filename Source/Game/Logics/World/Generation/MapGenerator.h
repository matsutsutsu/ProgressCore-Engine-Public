#pragma once
#include "../StageMap.h"
#include <SimpleMath.h>
#include <unordered_map>
#include <vector>

// ===================================================================================
// ファイル名: MapGenerator.h
// クラス名: MapGenerator
//
// 【役割・責任】
//   「ローグライクなダンジョン」を自動生成するためのアルゴリズムクラスです。
//   何もない空間に、部屋と通路を配置し、敵やプレイヤーの場所を決定します。
//
// 【生成ステップ】
//   Step 1: 論理グラフ生成 (部屋と部屋のつながりを決める)
//   Step 2: フェーズ割り当て (入り口から奥に行くほど難しくする)
//   Step 3: 物理配置 (部屋同士が重ならないように物理演算で広げる)
//   Step 4: 書き込み (計算結果を StageMap のグリッドデータに変換する)
// ===================================================================================


// フェーズ定義
enum class MapPhase {
    Early, // 序盤：安全
    Mid,   // 中盤：探索
    Late,  // 終盤：危険
    Boss   // ボス
};

// 部屋の種類
enum class RoomType {
    Start,
    Normal,
    Key, // 鍵や重要アイテム
    Boss
};

// Step 1: 論理ノード（部屋の抽象データ）
struct RoomNode {
    int      id    = -1;
    RoomType type  = RoomType::Normal;
    MapPhase phase = MapPhase::Early;

    // グラフ構造
    int              depth = 0;      // スタートからの距離
    std::vector<int> connectedRooms; // 繋がっている部屋のID

    // Step 3: 物理情報
    DirectX::SimpleMath::Vector2 position = {0, 0}; // グリッド上の中心座標(float)
    DirectX::SimpleMath::Vector2 velocity = {0, 0}; // 物理演算用
    int                          width    = 10;     // 部屋の幅（グリッド数）
    int                          height   = 10;     // 部屋の奥行き（グリッド数）

    // 矩形取得ヘルパー
    struct Rect {
        int x, y, w, h;
    };
    Rect GetRect() const
    {
        return {(int)(position.x - width * 0.5f), (int)(position.y - height * 0.5f), width, height};
    }
};

class MapGenerator {
  public:
    // 部屋サイズの確率設定用
    struct RoomSizeSetting {
        int   width;
        int   height;
        float weight; // 確率の重み (例: 10.0f とか 0.5f とか)
    };

    struct Config {
        // テーマではなく「ステージ番号 (1~5)」で管理
        int stageIndex = 1;

        int mapSizeW    = 50;
        int mapSizeH    = 50;
        int roomCount   = 20;

        // このステージで出現する部屋サイズのリスト
        std::vector<RoomSizeSetting> sizeSettings;


        // --- 通路と間隔の設定 ---
        int corridorWidth  = 2; // 通路の太さ
        int corridorLength = 4; // 通路の長さ（部屋と部屋の理想距離）
        int roomMargin     = 2; // 部屋同士の最低間隔（これより近づくと反発する）
        int physicsIterations = 200;  // 物理演算の回数もパラメータ化
    };

    MapGenerator() = default;

    // ステージ番号に応じた設定を返す関数
    static Config GetStageConfig(int stageIndex);

    // マップ生成のメイン関数
    // 成功したら true を返し、outMap にデータを書き込む
    bool Generate(StageMap &outMap, const Config &config);

  private:
    Config                _config;
    std::vector<RoomNode> _rooms;

    // --- 内部ステップ ---

    // Step 1: 論理グラフ生成 (Start -> Boss までのつながりを作る)
    void Step1_GenerateGraph();

    // Step 2: フェーズ割り当て (Depthに応じて危険度を設定)
    void Step2_AssignPhases();

    // Step 3: 物理配置シミュレーション (Force-Directed Graph)
    // 部屋同士が重ならないように物理演算で広げる
    void Step3_SimulateLayout();

    // Step 4: 盤面への書き込み (RoomNode -> StageMap)
    void Step4_RasterizeToMap(StageMap &map);

    // ヘルパー: 部屋をつなぐ
    void ConnectNodes(int idA, int idB);

    // 部屋の中身（壁や柱）を生成する関数
    void GenerateRoomInterior(const RoomNode &room, StageMap &map);

    // 敵の配置を決める関数
    void GenerateEnemySpawns(const RoomNode &room, StageMap &map);
};