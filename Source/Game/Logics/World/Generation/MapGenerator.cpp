#include "MapGenerator.h"
#include "RoomTemplates.h"
#include <algorithm>
#include <iostream>
#include <queue>
#include <random>
#include "Game/Logics/Character/Enemy/EnemyComponent.h"

#include "RoomTemplateManager.h"

// ===================================================================================
// ファイル名: MapGenerator.cpp
// ===================================================================================

using namespace DirectX::SimpleMath;
static std::mt19937 g_gen(std::random_device{}());

// --------------------------------------------------------------------------
// テンプレート適用ヘルパー (変更なし)
// --------------------------------------------------------------------------
static void ApplyTemplate10x10(const RoomNode &room, StageMap &map, const int pattern[10][10])
{
    auto rect   = room.GetRect();
    int  startX = rect.x + (rect.w - 10) / 2;
    int  startY = rect.y + (rect.h - 10) / 2;
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            int tx = startX + x;
            int ty = startY + y;
            if (tx >= rect.x && tx < rect.x + rect.w && ty >= rect.y && ty < rect.y + rect.h) {
                int idx = map.GetIndex(tx, ty);
                if (idx != -1) {
                    if (pattern[y][x] == 1)
                        map.tiles[idx] = TileType::Wall;
                    else if (pattern[y][x] == 2)
                        map.tiles[idx] = TileType::Hole;
                    else
                        map.tiles[idx] = TileType::Floor;
                }
            }
        }
    }
}
static void ApplyTemplate20x20(const RoomNode &room, StageMap &map, const int pattern[20][20])
{
    auto rect   = room.GetRect();
    int  startX = rect.x + (rect.w - 20) / 2;
    int  startY = rect.y + (rect.h - 20) / 2;
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            int tx = startX + x;
            int ty = startY + y;
            if (tx >= rect.x && tx < rect.x + rect.w && ty >= rect.y && ty < rect.y + rect.h) {
                int idx = map.GetIndex(tx, ty);
                if (idx != -1) {
                    if (pattern[y][x] == 1)
                        map.tiles[idx] = TileType::Wall;
                    else if (pattern[y][x] == 2)
                        map.tiles[idx] = TileType::Hole;
                    else
                        map.tiles[idx] = TileType::Floor;
                }
            }
        }
    }
}

// ===================================================================================
// Generate メイン関数
// ===================================================================================
bool MapGenerator::Generate(StageMap &outMap, const Config &config)
{
    // 古い GetThemeConfig を削除し、引数をそのまま採用する
    // (呼び出し元で GetStageConfig を使って設定を作ってから渡す設計です)
    _config = config;

    _rooms.clear();

    // 1. 生成 & 2. フェーズ
    Step1_GenerateGraph();
    Step2_AssignPhases();

    // 3. 物理配置 (クランプなしで自由に広げる)
    Step3_SimulateLayout();

    // 4. 近接部屋のショートカット接続
    // 物理配置が完了した後、「近くにあるのに繋がっていない部屋」を探してつなぐ
    for (int i = 0; i < _rooms.size(); ++i) {
        for (int j = i + 1; j < _rooms.size(); ++j) {
            // 既に繋がっていればスキップ
            bool connected = false;
            for (int id : _rooms[i].connectedRooms)
                if (id == j) connected = true;
            if (connected) continue;

            // 距離チェック
            float dist = Vector2::Distance(_rooms[i].position, _rooms[j].position);

            // 判定基準: (部屋Aの半径 + 部屋Bの半径) + 通路長 + α
            // これより近ければ「壁一枚隔てて隣り合っている」状態
            float radiusA   = (std::max)(_rooms[i].width, _rooms[i].height) / 2.0f;
            float radiusB   = (std::max)(_rooms[j].width, _rooms[j].height) / 2.0f;
            float threshold = radiusA + radiusB + _config.corridorLength * 1.5f;

            if (dist < threshold) {
                // 30%くらいの確率でショートカットを開通させる
                if (std::uniform_real_distribution<float>(0.0f, 1.0f)(g_gen) < 0.3f) {
                    ConnectNodes(i, j);
                }
            }
        }
    }

    // -------------------------------------------------------------
    // ★ここが重要: 全部屋を包み込むサイズを計算し、全体をズラす
    // -------------------------------------------------------------
    float minX = 100000.0f, minY = 100000.0f;
    float maxX = -100000.0f, maxY = -100000.0f;

    for (const auto &room : _rooms) {
        float halfW = room.width * 0.5f;
        float halfH = room.height * 0.5f;
        if (room.position.x - halfW < minX) minX = room.position.x - halfW;
        if (room.position.x + halfW > maxX) maxX = room.position.x + halfW;
        if (room.position.y - halfH < minY) minY = room.position.y - halfH;
        if (room.position.y + halfH > maxY) maxY = room.position.y + halfH;
    }

    // 余白(パディング)を追加
    int padding     = 10; // 端っこに少し余裕を持たせる
    int finalWidth  = (int)std::ceil(maxX - minX) + padding * 2;
    int finalHeight = (int)std::ceil(maxY - minY) + padding * 2;

    // 全部屋をプラス座標に移動させるためのオフセット
    Vector2 offset;
    offset.x = (float)padding - minX;
    offset.y = (float)padding - minY;

    for (auto &room : _rooms) {
        room.position += offset;
    }

    // 4. 計算したピッタリサイズでマップを初期化
    outMap.Initialize(finalWidth, finalHeight, 2.0f);
    std::fill(outMap.tiles.begin(), outMap.tiles.end(), TileType::Wall);

    // 書き込み
    Step4_RasterizeToMap(outMap);

    // プレイヤーのスポーン位置 (Start部屋の中心)
    for (const auto &room : _rooms) {
        if (room.type == RoomType::Start) {
            outMap.playerSpawnPoint =
                outMap.GridToWorld((int)room.position.x, (int)room.position.y);
            break;
        }
    }

    // ゲートのスポーン位置 (Boss部屋の中心)
    bool bossFound = false;
    for (const auto &room : _rooms) {
        if (room.type == RoomType::Boss) {
            outMap.gateSpawnPoint = outMap.GridToWorld((int)room.position.x, (int)room.position.y);
            bossFound             = true;
            break;
        }
    }
    // ボス部屋がない場合(エラー回避)は最後の部屋にするなどのフォールバックがあると良い
    if (!bossFound && !_rooms.empty()) {
        const auto &lastRoom = _rooms.back();
        outMap.gateSpawnPoint =
            outMap.GridToWorld((int)lastRoom.position.x, (int)lastRoom.position.y);
    }

    // デバッグ情報
    outMap.debugRooms.clear();
    for (const auto &room : _rooms) {
        auto rect = room.GetRect();
        outMap.debugRooms.push_back({rect.x, rect.y, rect.w, rect.h, (int)room.type});
    }

    return true;
}

// ステージごとの設定定義
MapGenerator::Config MapGenerator::GetStageConfig(int stageIndex)
{
    Config config;
    config.stageIndex = stageIndex;

    switch (stageIndex) {
    case 1: // チュートリアル・遺跡
        config.roomCount      = 8;
        config.mapSizeW       = 60;
        config.mapSizeH       = 60;
        config.corridorLength = 6;

        // ★設定例: 基本10x10のみ
        config.sizeSettings.push_back({15, 15, 0.7f});
        config.sizeSettings.push_back({20, 20, 0.3f});
        break;
    case 2: // 森・機動力
        config.roomCount       = 12;
        config.mapSizeW        = 80;
        config.mapSizeH        = 80;

        config.sizeSettings.push_back({15, 15, 0.5f});
        config.sizeSettings.push_back({20, 20, 0.5f});
        break;
    case 3: // 巣窟・物量
        config.roomCount  = 14;
        config.mapSizeW   = 100;
        config.mapSizeH   = 100;
        config.roomMargin = 2; // 密集させる
        config.sizeSettings.push_back({15, 15, 0.4f});
        config.sizeSettings.push_back({20, 20, 0.6f});
        break;
    case 4: // 荒野・遠距離
        config.roomCount       = 15;
        config.mapSizeW        = 150;
        config.mapSizeH        = 150;
        config.corridorLength  = 10; // 通路長い
        config.sizeSettings.push_back({15, 15, 0.2f});
        config.sizeSettings.push_back({20, 20, 0.8f});
        break;
    case 5: // 深層・複合
        config.roomCount = 20;
        config.mapSizeW  = 120;
        config.mapSizeH  = 120;
        config.sizeSettings.push_back({15, 15, 0.3f});
        config.sizeSettings.push_back({20, 20, 0.7f});
        break;
    default: break;
    }

    // 安全策: 設定し忘れ防止
    if (config.sizeSettings.empty()) {
        config.sizeSettings.push_back({15, 15, 1.0f});
    }

    return config;
}

// --------------------------------------------------------------------------
// Step 1: 論理グラフ生成
// --------------------------------------------------------------------------
void MapGenerator::Step1_GenerateGraph()
{
    RoomNode startNode;
    startNode.id   = 0;
    startNode.type = RoomType::Start;
    // ★修正: 原点(0,0)からスタート。後で全体をズラすのでマイナスに行ってもOK
    startNode.position = {0.0f, 0.0f};
    _rooms.push_back(startNode);

    std::vector<int> mainRoute;
    mainRoute.push_back(0);

    for (int i = 1; i < _config.roomCount; ++i) {
        RoomNode newNode;
        newNode.id = i;

       // 1. 重みの合計を計算
        float totalWeight = 0.0f;
        for (const auto &setting : _config.sizeSettings) {
            totalWeight += setting.weight;
        }

        // 2. ランダム値生成
        float r = std::uniform_real_distribution<float>(0.0f, totalWeight)(g_gen);

        // 3. 抽選
        float currentWeight = 0.0f;
        for (const auto &setting : _config.sizeSettings) {
            currentWeight += setting.weight;
            if (r <= currentWeight) {
                newNode.width  = setting.width;
                newNode.height = setting.height;
                break;
            }
        }

        int                                   parentId = -1;
        std::uniform_real_distribution<float> distBranch(0.0f, 1.0f);
        bool extendMain = (distBranch(g_gen) > 0.3f) || (mainRoute.size() < 4);

        if (extendMain) {
            parentId = mainRoute.back();
            mainRoute.push_back(i);
        }
        else {
            if (_rooms.size() > 1) {
                std::uniform_int_distribution<int> distIndex(0, (int)_rooms.size() - 1);
                parentId = distIndex(g_gen);
            }
            else
                parentId = 0;
        }

        // 親から少し離して配置
        const auto                           &parent = _rooms[parentId];
        std::uniform_real_distribution<float> distDir(-1.0f, 1.0f);
        Vector2                               dir(distDir(g_gen), distDir(g_gen));
        if (dir.LengthSquared() < 0.01f) dir = Vector2(1, 0);
        dir.Normalize();

        float dist       = (float)(parent.width + newNode.width) / 2.0f + _config.corridorLength;
        newNode.position = parent.position + dir * dist;

        _rooms.push_back(newNode);
        ConnectNodes(parentId, i);
    }

    if (!mainRoute.empty()) _rooms[mainRoute.back()].type = RoomType::Boss;

    // ループ接続
    for (int i = 0; i < _rooms.size(); ++i) {
        for (int j = i + 1; j < _rooms.size(); ++j) {
            bool connected = false;
            for (int id : _rooms[i].connectedRooms)
                if (id == j) connected = true;
            if (connected) continue;

            float dist         = Vector2::Distance(_rooms[i].position, _rooms[j].position);
            float combinedSize = (float)(_rooms[i].width + _rooms[j].width) / 2.0f;

            // 近くにあればつなぐ
            if (dist < combinedSize + 5.0f) {
                if (std::uniform_real_distribution<float>(0, 1)(g_gen) < 0.2f) {
                    ConnectNodes(i, j);
                }
            }
        }
    }
}




// --------------------------------------------------------------------------
// Step 3: 物理配置シミュレーション (PBD + 焼きなまし法による収束)
// --------------------------------------------------------------------------
void MapGenerator::Step3_SimulateLayout()
{
    // イテレーション回数（多いほど綺麗に分離する）
    int iterations = _config.physicsIterations;

    // 位置補正率
    // 力(Force)ではなく「1回でどれだけ修正するか」の割合
    const float baseSeparationRate = 0.8f;  // 基本の反発強度
    const float linkRate           = 0.05f; // バネ強度
    const float centerGravity      = 0.005f;

    for (int k = 0; k < iterations; ++k) {

        // --- 焼きなまし法 (Simulated Annealing) ---
        // 進捗率 (0.0 -> 1.0)
        float progress = (float)k / (float)iterations;

        // 温度 (1.0 -> 0.0)
        // ループが進むにつれて「熱」が冷めていく
        float temperature = 1.0f - progress;

        // 現在の補正率を温度で調整
        // 最後の方でも完全に0にはせず、微調整のために半分くらいの強さは残す
        float currentSeparationRate = baseSeparationRate * (0.5f + 0.5f * temperature);

        // ----------------------------------------------------------
        // 1. リンク制約 (通路長を保つ)
        // ----------------------------------------------------------
        for (int i = 0; i < _rooms.size(); ++i) {
            if (_rooms[i].type == RoomType::Start) continue;

            // 中央への重力
            _rooms[i].position -= _rooms[i].position * centerGravity;

            // 繋がっている部屋との距離調整
            for (int neighborId : _rooms[i].connectedRooms) {
                bool  neighborIsStart = (_rooms[neighborId].type == RoomType::Start);
                float moveRatio       = neighborIsStart ? 1.0f : 0.5f;

                Vector2 posA = _rooms[i].position;
                Vector2 posB = _rooms[neighborId].position;
                Vector2 dir  = posA - posB;
                float   dist = dir.Length();

                float radiusA = (std::max)((float)_rooms[i].width, (float)_rooms[i].height) / 2.0f;
                float radiusB =
                    (std::max)((float)_rooms[neighborId].width, (float)_rooms[neighborId].height) /
                    2.0f;
                float targetDist = radiusA + radiusB + (float)_config.corridorLength;

                if (dist > targetDist) {
                    if (dist < 0.001f) {
                        dir  = Vector2(1, 0);
                        dist = 0.001f;
                    }
                    dir.Normalize();

                    // 差分を埋める補正ベクトル
                    Vector2 correction = dir * (dist - targetDist) * linkRate;

                    _rooms[i].position -= correction * moveRatio;
                    if (!neighborIsStart) {
                        _rooms[neighborId].position += correction * moveRatio;
                    }
                }
            }
        }

        // ----------------------------------------------------------
        // 2. 重なり制約 (RoomMarginの強制確保)
        // ----------------------------------------------------------
        for (int i = 0; i < _rooms.size(); ++i) {
            for (int j = i + 1; j < _rooms.size(); ++j) {

                bool fixedA = (_rooms[i].type == RoomType::Start);
                bool fixedB = (_rooms[j].type == RoomType::Start);

                Vector2 posA = _rooms[i].position;
                Vector2 posB = _rooms[j].position;

                float dx = std::abs(posA.x - posB.x);
                float dy = std::abs(posA.y - posB.y);

                float extra = (fixedA || fixedB) ? 10.0f : 0.0f;

                float minW = (_rooms[i].width / 2.0f) + (_rooms[j].width / 2.0f) +
                             (float)_config.roomMargin + extra;
                float minH = (_rooms[i].height / 2.0f) + (_rooms[j].height / 2.0f) +
                             (float)_config.roomMargin + extra;

                // 重なっているか判定
                if (dx < minW && dy < minH) {
                    float overlapX = minW - dx;
                    float overlapY = minH - dy;

                    Vector2 pushDir = posA - posB;

                    if (pushDir.LengthSquared() < 0.001f) {
                        std::uniform_real_distribution<float> rot(0, 6.28f);
                        float                                 theta = rot(g_gen);
                        pushDir = Vector2(cos(theta), sin(theta));
                    }
                    else {
                        pushDir.Normalize();

                        // ★修正: ゆらぎ(Jitter)に温度を掛ける
                        // 最初(temp=1.0)は大きく揺らしてローカルミニマム（整列）から脱出させる
                        // 最後(temp=0.0)は揺らしをゼロにして、ピタッと静止させる
                        float jitterStrength = 0.5f * temperature; // 強さを少し上げる(0.2->0.5)

                        std::uniform_real_distribution<float> jitter(
                            -jitterStrength, jitterStrength);
                        pushDir.x += jitter(g_gen);
                        pushDir.y += jitter(g_gen);
                        pushDir.Normalize();
                    }

                    // 押し出す強さ (温度によって調整された currentSeparationRate を使用)
                    float pushAmount = (std::min)(overlapX, overlapY) * currentSeparationRate;

                    Vector2 separation = pushDir * pushAmount;

                    if (!fixedA && !fixedB) {
                        _rooms[i].position += separation * 0.5f;
                        _rooms[j].position -= separation * 0.5f;
                    }
                    else if (!fixedA) {
                        _rooms[i].position += separation;
                    }
                    else if (!fixedB) {
                        _rooms[j].position -= separation;
                    }
                }
            }
        }
    }

    // 最後に整数座標へスナップ
    for (auto &room : _rooms) {
        room.position.x = std::round(room.position.x);
        room.position.y = std::round(room.position.y);
    }
}



// ConnectNodes, Step2, Step4, GenerateInterior, GenerateEnemySpawns は変更なし
// (以前のコードを使用してください)
void MapGenerator::ConnectNodes(int idA, int idB)
{
    _rooms[idA].connectedRooms.push_back(idB);
    _rooms[idB].connectedRooms.push_back(idA);
}


// --------------------------------------------------------------------------
// Step 2: フェーズ割り当て (BFSで距離計算 & ボス確定)
// --------------------------------------------------------------------------
void MapGenerator::Step2_AssignPhases()
{
    // 幅優先探索(BFS)でStart(0)からの距離(Depth)を測る
    std::queue<int>  q;
    std::vector<int> distances(_rooms.size(), -1);

    q.push(0);
    distances[0] = 0;

    int maxDist        = 0;
    int furthestRoomId = 0; // 最も遠い部屋のID

    while (!q.empty()) {
        int curr = q.front();
        q.pop();

        // 最大距離の更新
        if (distances[curr] > maxDist) {
            maxDist        = distances[curr];
            furthestRoomId = curr; // 候補更新
        }

        for (int neighbor : _rooms[curr].connectedRooms) {
            if (distances[neighbor] == -1) {
                distances[neighbor] = distances[curr] + 1;
                q.push(neighbor);
            }
        }
    }

    // 最も遠い部屋を強制的に「ボス部屋」に再設定する
    // Step1で仮決めしたBossを取り消し、探索距離が一番長い部屋をボスにする
    for (auto &room : _rooms) {
        if (room.type == RoomType::Boss) {
            room.type = RoomType::Normal; // 一旦リセット
        }
    }
    // StartがBossになるのを防ぐ（部屋数が1個の時など）
    if (furthestRoomId != 0) {
        _rooms[furthestRoomId].type = RoomType::Boss;
    }

    // Depthに応じてフェーズを割り当て
    for (auto &room : _rooms) {
        room.depth  = distances[room.id];
        float ratio = (float)room.depth / (float)maxDist;

        if (room.type == RoomType::Start)
            room.phase = MapPhase::Early;
        else if (room.type == RoomType::Boss)
            room.phase = MapPhase::Boss;
        else if (ratio < 0.33f)
            room.phase = MapPhase::Early;
        else if (ratio < 0.66f)
            room.phase = MapPhase::Mid;
        else
            room.phase = MapPhase::Late;
    }
}


void MapGenerator::GenerateRoomInterior(const RoomNode &room, StageMap &map)
{
    auto rect = room.GetRect();

    // Start/Bossは完全に空洞にする
    if (room.type == RoomType::Start || room.type == RoomType::Boss) {
        for (int y = rect.y; y < rect.y + rect.h; ++y) {
            for (int x = rect.x; x < rect.x + rect.w; ++x) {
                int idx = map.GetIndex(x, y);
                if (idx != -1) map.tiles[idx] = TileType::Floor;
            }
        }
        return;
    }

    // --- テンプレート選択ロジック ---

    // JSONテンプレートを取得
    const RoomTemplate *tmpl = RoomTemplateManager::Instance().GetRandomTemplate(
        _config.stageIndex, room.width, room.height);

    if (tmpl) {
        // パターンA: JSONテンプレートが見つかった場合
        // 部屋の中央に配置するようにオフセットを計算
        int startX = rect.x + (rect.w - tmpl->width) / 2;
        int startY = rect.y + (rect.h - tmpl->height) / 2;

        for (int y = 0; y < tmpl->height; ++y) {
            for (int x = 0; x < tmpl->width; ++x) {

                // テンプレートから値を取得 (0:床, 1:壁, 2:穴)
                int tileVal = tmpl->Get(x, y);

                // マップ上の座標
                int tx = startX + x;
                int ty = startY + y;

                // 範囲内チェック & 書き込み
                if (tx >= rect.x && tx < rect.x + rect.w && ty >= rect.y && ty < rect.y + rect.h) {
                    int idx = map.GetIndex(tx, ty);
                    if (idx != -1) {
                        if (tileVal == 1)
                            map.tiles[idx] = TileType::Wall;
                        else if (tileVal == 2)
                            map.tiles[idx] = TileType::Hole;
                        else
                            map.tiles[idx] = TileType::Floor;
                    }
                }
            }
        }
    }
    else {
        // ★テンプレートが見つからなかった場合 (フォールバック)

        // どんなサイズが来ても、とりあえず「何もない空部屋」にする
        // (RoomTemplates::EMPTY_10x10 などを無理やり使うのではなく、動的に床を敷く)

        int startX = rect.x; // 部屋いっぱい使う
        int startY = rect.y;

        for (int y = 0; y < room.height; ++y) {
            for (int x = 0; x < room.width; ++x) {
                int tx = startX + x;
                int ty = startY + y;

                int idx = map.GetIndex(tx, ty);
                if (idx != -1) {
                    // 外周だけ壁にする簡易ロジック
                    if (x == 0 || x == room.width - 1 || y == 0 || y == room.height - 1)
                        map.tiles[idx] = TileType::Wall;
                    else
                        map.tiles[idx] = TileType::Floor;
                }
            }
        }
    }

}



// ===================================================================================
// Step 4: 盤面への書き込み
// ===================================================================================
void MapGenerator::Step4_RasterizeToMap(StageMap &map)
{
    // 1. 部屋を掘る
    for (const auto &room : _rooms) {
        GenerateRoomInterior(room, map);
    }

    int pathWidth = _config.corridorWidth;

    // ヘルパー: 線を引く（スマート・ディギング版）
    // targetRect:
    // 目標とする部屋の範囲。これ指定がある場合、範囲内の床にぶつかったら掘るのを止める。
    auto DrawLine = [&](int                   startX,
                        int                   startZ,
                        int                   length,
                        int                   axis,
                        const RoomNode::Rect *targetRect = nullptr) {
        int steps     = std::abs(length);
        int sign      = (length > 0) ? 1 : -1;
        int thickness = pathWidth;
        int offset    = thickness / 2;

        for (int i = 0; i < steps; ++i) {
            int cx = startX;
            int cz = startZ;

            // 進行
            if (axis == 0)
                cx += i * sign; // X軸移動
            else
                cz += i * sign; // Z軸移動

            // ★スマート判定:
            // 「目標の部屋の中」かつ「すでに床がある」なら、接続完了とみなしてストップ
            if (targetRect) {
                if (cx >= targetRect->x && cx < targetRect->x + targetRect->w &&
                    cz >= targetRect->y && cz < targetRect->y + targetRect->h) {

                    int idx = map.GetIndex(cx, cz);
                    // もし進行方向の先が既に「床」なら、もう掘らなくていい
                    if (idx != -1 && map.tiles[idx] == TileType::Floor) {
                        break;
                    }
                }
            }

            // 太さの分だけ掘る
            for (int t = 0; t < thickness; ++t) {
                int tx = cx;
                int tz = cz;
                // 軸と垂直方向に太みを持たせる
                if (axis == 0)
                    tz = cz - offset + t;
                else
                    tx = cx - offset + t;

                int idx = map.GetIndex(tx, tz);
                if (idx != -1 && map.tiles[idx] == TileType::Wall) {
                    map.tiles[idx] = TileType::Floor;
                }
            }
        }
    };

    // 2. 通路生成
    for (const auto &room : _rooms) {
        for (int neighborId : room.connectedRooms) {
            if (room.id > neighborId) continue;

            const auto    &other = _rooms[neighborId];
            RoomNode::Rect r1    = room.GetRect();
            RoomNode::Rect r2    = other.GetRect(); // 目標の部屋

            int cx1 = (int)room.position.x;
            int cy1 = (int)room.position.y;
            int cx2 = (int)other.position.x;
            int cy2 = (int)other.position.y;

            // 重なり判定用
            int overlapX_min = (std::max)(r1.x, r2.x);
            int overlapX_max = (std::min)(r1.x + r1.w, r2.x + r2.w);
            int overlapW     = overlapX_max - overlapX_min;

            int overlapY_min = (std::max)(r1.y, r2.y);
            int overlapY_max = (std::min)(r1.y + r1.h, r2.y + r2.h);
            int overlapH     = overlapY_max - overlapY_min;

            // ■ 縦並び (直線)
            if (overlapW > pathWidth) {
                int midX = overlapX_min + overlapW / 2;
                // ここではターゲット指定なし（単純接続）でもOKだが、念のため r2 を指定
                DrawLine(midX, cy1, cy2 - cy1, 1, &r2);
                continue;
            }

            // ■ 横並び (直線)
            if (overlapH > pathWidth) {
                int midY = overlapY_min + overlapH / 2;
                DrawLine(cx1, midY, cx2 - cx1, 0, &r2);
                continue;
            }

            // ■ 斜め (Z字型)
            // 最後の「相手の部屋に入っていく線」にだけ &r2 を渡して、内部破壊を防ぐ
            std::uniform_int_distribution<int> coin(0, 1);
            bool                               horizontalFirst = coin(g_gen) == 0;

            if (horizontalFirst) {
                // 横 -> 縦 -> 横
                int midX = cx1 + (cx2 - cx1) / 2;
                DrawLine(cx1, cy1, midX - cx1, 0, nullptr); // 部屋出し
                DrawLine(midX, cy1, cy2 - cy1, 1, nullptr); // 中間
                DrawLine(midX, cy2, cx2 - midX, 0, &r2);    // 部屋入り (★ここでストップ判定)
            }
            else {
                // 縦 -> 横 -> 縦
                int midY = cy1 + (cy2 - cy1) / 2;
                DrawLine(cx1, cy1, midY - cy1, 1, nullptr); // 部屋出し
                DrawLine(cx1, midY, cx2 - cx1, 0, nullptr); // 中間
                DrawLine(cx2, midY, cy2 - midY, 1, &r2);    // 部屋入り (★ここでストップ判定)
            }
        }
    }

    // 3. 敵配置
    for (const auto &room : _rooms) {
        GenerateEnemySpawns(room, map);
    }
}


// ===================================================================================
// 敵配置ロジック (EnemyAIType 対応版)
// ===================================================================================
void MapGenerator::GenerateEnemySpawns(const RoomNode &room, StageMap &map)
{
    if (room.type == RoomType::Start) return;

    // 1. スポーン候補地の取得（部屋の床）
    auto                 rect = room.GetRect();
    std::vector<Vector2> candidates;
    // 壁際すぎると埋まる可能性があるので、+1 / -1 で少し内側を検索
    for (int y = rect.y + 1; y < rect.y + rect.h - 1; ++y) {
        for (int x = rect.x + 1; x < rect.x + rect.w - 1; ++x) {
            if (!map.IsWall(x, y)) {
                candidates.push_back(Vector2((float)x, (float)y));
            }
        }
    }
    if (candidates.empty()) return;

    // シャッフルしてランダム配置
    std::shuffle(candidates.begin(), candidates.end(), g_gen);

    // 2. ステージごとの敵構成定義
    struct EnemySet {
        EnemyAIType type;
        float       probability; // 出現確率 (0.0~1.0, 合計が1でなくても良い＝重み付け)
    };
    std::vector<EnemySet> spawnTable;
    int                   baseCount = 3;    //一部屋に生成される敵の数

    switch (_config.stageIndex) {
    case 1: // [遺跡] 基本の敵のみ
        spawnTable = { {EnemyAIType::Pawn, 0.6f},
            {EnemyAIType::Boid, 0.3f} ,{EnemyAIType::Tank,0.1f}};
        baseCount  = 4;
        break;

    case 2: // [森] 回避兵とジャンパーメイン
        spawnTable = {{EnemyAIType::Pawn, 0.3f},
            {EnemyAIType::Boid, 0.2f},
            {EnemyAIType::Tank, 0.15f}, {EnemyAIType::Jumper, 0.35f}};
        baseCount = 5;
        break;

    case 3: // [巣窟] 群れとタンク
        spawnTable = {
            {EnemyAIType::Pawn, 0.1f},
            {EnemyAIType::Boid, 0.3f},
            {EnemyAIType::Jumper, 0.3f},
            {EnemyAIType::Fire, 0.1f},
            {EnemyAIType::Tank, 0.2f}};
        baseCount  = 6; // 多め
        break;

    case 4: // [荒野] 遠距離メイン
        spawnTable = {
            {EnemyAIType::Boid, 0.2f},
            {EnemyAIType::Jumper, 0.2f},
            {EnemyAIType::Fire, 0.2f},
            {EnemyAIType::Evader, 0.2f},
            {EnemyAIType::Tank, 0.2f}
        };
        baseCount = 7;
        break;

    case 5: // [深層] 全種混合、強力な敵多め
        spawnTable = {{EnemyAIType::Tank, 0.3f},
            {EnemyAIType::Fire, 0.3f},
            {EnemyAIType::Evader, 0.2f},
            {EnemyAIType::Boid, 0.2f}};
        baseCount  = 5;
        break;
    }

    // =========================================================
    // 部屋サイズによる数の補正
    // =========================================================
    float sizeMultiplier = 1.0f;
    int   area           = room.width * room.height;

    // 基準を 10x10 (面積100) とする
    if (area >= 400) {
        // 20x20クラス (面積400) -> 敵の数を2.5倍に
        sizeMultiplier = 2.5f;
    }
    else if (area >= 225) {
        // 15x15クラス (面積225) -> 敵の数を1.5倍に
        sizeMultiplier = 1.5f;
    }

    // 基本数に倍率を適用
    baseCount = (int)(baseCount * sizeMultiplier);

    // 3. 難易度フェーズ(Early/Mid/Late)による数の補正
    int count = baseCount;
    if (room.phase == MapPhase::Early)
        count = (int)(baseCount * 0.7f);
    else if (room.phase == MapPhase::Late)
        count = (int)(baseCount * 1.5f);

    // 4. 生成実行
    for (int i = 0; i < count && i < candidates.size(); ++i) {
        EnemySpawnInfo info;
        float          wx = candidates[i].x * map.cellSize + map.cellSize * 0.5f;
        float          wz = candidates[i].y * map.cellSize + map.cellSize * 0.5f;
        info.position     = Vector3(wx, 0.0f, wz);
        info.rotationY    = 0;

        // 確率抽選
        float totalWeight = 0;
        for (auto &s : spawnTable) totalWeight += s.probability;

        float r       = std::uniform_real_distribution<float>(0, totalWeight)(g_gen);
        float current = 0;
        for (auto &s : spawnTable) {
            current += s.probability;
            if (r <= current) {
                info.type = (int)s.type;
                break;
            }
        }

        map.enemySpawns.push_back(info);
    }
}