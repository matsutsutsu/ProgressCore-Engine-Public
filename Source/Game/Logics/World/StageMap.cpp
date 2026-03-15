#include "StageMap.h"
#include <algorithm>
#include <cmath>
#include <queue> // BFS用

// ===================================================================================
// ファイル名: StageMap.cpp
//
// 【役割・責任】
//   StageMapクラスの複雑な計算ロジックを実装するファイルです。
//
// 【主な実装内容】
//   1. UpdateFlowField:
//      - 「ダイクストラ法（幅優先探索）」を使って、プレイヤーまでの最短ルートを全マス分計算します。
//      - 壁際を避けるコスト計算などもここで行っています。
//
//   2. Raycast:
//      - DDAアルゴリズムを用いて、グリッド上の「視線が通るか」を高速に判定します。
//      - 銃の弾道計算や、敵の視界判定に使われます。
// ===================================================================================

using namespace DirectX::SimpleMath;

// -------------------------------------------------------------
// FlowField 構造体
// -------------------------------------------------------------
void StageMap::FlowField::Resize(int size)
{
    // 65535は「到達不能」あるいは「無限大」として扱う初期値
    costMap.assign(size, StageMap::UNREACHABLE_COST);
    vectors.assign(size, Vector3::Zero);
}

// -------------------------------------------------------------
// StageMap 実装
// -------------------------------------------------------------

void StageMap::Initialize(int w, int h, float size)
{
    width    = w;
    height   = h;
    cellSize = size;
    tiles.assign(w * h, TileType::Floor);
    visitedMask.assign(w * h, 0);

    // リセット
    enemySpawns.clear();

    // フローフィールドも初期化
    // 配列のサイズを確保
    flowFields.resize(FLOW_FIELD_COUNT);
    for (auto &ff : flowFields) {
        ff.Resize(w * h);
    }
}

//  フローフィールド更新ロジック (ダイクストラ法 / 幅優先探索)
void StageMap::UpdateFlowField(int slotIndex, int targetX, int targetZ)
{
    // 安全策
    if (slotIndex < 0 || slotIndex >= flowFields.size()) return;

    // 対象のフィールドへの参照を取得
    auto &targetField = flowFields[slotIndex];

    // 1. 初期化
    int size = width * height;
    targetField.Resize(size);

    // リサイズが必要な場合のみ行う（メモリ確保コスト削減）
    if (targetField.costMap.size() != size) {
        targetField.Resize(size);
    }
    else {
        // 既存バッファのクリアのみ（高速化）
        std::fill(targetField.costMap.begin(), targetField.costMap.end(), UNREACHABLE_COST);
        std::fill(targetField.vectors.begin(), targetField.vectors.end(), Vector3::Zero);
    }

    // ターゲットが無効、または壁の中なら計算しない
    // 重要: もし「包囲位置」が壁の中だった場合、そのフィールドは空っぽになる。
    // その場合、AI側で「Slot 0（プレイヤー位置）」にフォールバックするロジックが必要
    int targetIdx = GetIndex(targetX, targetZ);
    if (targetIdx == -1 || IsWall(targetX, targetZ)) {
        return;
    }

    // 2. Integration Field (コストマップ) 生成
    // 最初に入れたものが最初に出てくるFIFOのデータ構造
    // 探索すべきマスのリストを管理している、ゴールに近い順にマスを処理して
    // 破門のように探索範囲を広げていくアルゴリズム（BFS）のために必要
    std::queue<int> openList;

    // ターゲット地点をスタート(コスト0)とする
    targetField.costMap[targetIdx] = 0; // ターゲット（Player）のスタート地点をコスト０にする
    openList.push(targetIdx);    // 次に調べるべきマスのリストに登録する、最初はゴール地点だけ

    // 上下左右のオフセット
    // 上下左右を調べる際にif文で４回書くのはめんどくさいので配列で回す
    const int neighbors[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};

    // ★追加: 壁チェック用の周囲8方向 (斜め含む)
    // 自分の周囲に壁が1つでもあれば「壁際」とみなす
    const int wallCheckOffsets[8][2] = {
        {0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    // キューが空になるまで（行ける場所全て）を調べつくす
    while (!openList.empty()) {
        // 先頭から1つ取り出す（例：最初はコスト0のゴール地点）
        int currentIdx = openList.front();
        openList.pop();

        int cx          = currentIdx % width;
        int cy          = currentIdx / width;   // cy は z座標のこと
        int currentCost = targetField.costMap[currentIdx];

        // 4方向をチェック
        for (const auto &offset : neighbors) {
            int nx   = cx + offset[0];
            int ny   = cy + offset[1];
            int nIdx = GetIndex(nx, ny);

            // 範囲外・壁・すでに探索済み(今のルートよりコストが低い)場合はスキップ
            if (nIdx != -1 && !IsWall(nx, ny)) {

                // --- ★ここから変更: 移動コストの計算 ---

                int moveCost = 1; // 基本コスト（平地）

                // 「このマス(nx, ny)は壁際か？」を調べる
                bool isNearWall = false;
                for (const auto &wOff : wallCheckOffsets) {
                    // 隣接マスが壁かどうか
                    if (IsWall(nx + wOff[0], ny + wOff[1])) {
                        isNearWall = true;
                        break;
                    }
                }

                // 壁際ならコストを高くする（ペナルティ）
                // 値を大きくするほど、壁を嫌がって大回りするようになる
                if (isNearWall) {
                    moveCost = 5; // ※ここを調整！(3~10くらい推奨)
                }

                // 新しいコスト = 現在のコスト + 移動コスト
                int newCost = currentCost + moveCost;

                // より低コストなルートが見つかったら更新
                if (targetField.costMap[nIdx] > newCost) {
                    targetField.costMap[nIdx] = newCost;
                    openList.push(nIdx);
                }
            }
        }
    }

    // 3. Vector Field (ベクトル場) 生成
    // 各セルについて、最もコストが低い隣接セルの方角を向く
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            int idx = GetIndex(x, z);

            // 壁や到達不能エリアはベクトルなし
            if (IsWall(x, z) || targetField.costMap[idx] == 65535) {
                continue;
            }

            int     minCost = targetField.costMap[idx];
            Vector3 bestDir = Vector3::Zero;

            // 周囲4マスを見て、最もゴールに近い(コストが低い)マスを探す
            for (const auto &offset : neighbors) {
                int nx   = x + offset[0];
                int nz   = z + offset[1];
                int nIdx = GetIndex(nx, nz);

                if (nIdx != -1 && targetField.costMap[nIdx] < minCost) {
                    minCost = targetField.costMap[nIdx];
                    // その方向へのベクトル
                    bestDir = Vector3((float)offset[0], 0, (float)offset[1]);
                }
            }

            // 正規化して保存
            if (bestDir.LengthSquared() > 0.1f) {
                bestDir.Normalize();
                targetField.vectors[idx] = bestDir;
            }
        }
    }
}

// グリッド専用レイキャスト (DDA実装)
MapRaycastHit StageMap::Raycast(
    const Vector3 &origin, const Vector3 &direction, float maxDistance) const
{
    MapRaycastHit info;
    info.isHit = false;

    // レイの準備
    Vector3 rayDir = direction;
    rayDir.Normalize();

    // 現在のグリッド座標
    int currentX, currentZ;
    WorldToGrid(origin, currentX, currentZ);

    // 進行方向（ステップ）
    int stepX = (rayDir.x >= 0) ? 1 : -1;
    int stepZ = (rayDir.z >= 0) ? 1 : -1;

    // 次の境界線までの距離 (Delta)
    float tDeltaX = (std::abs(rayDir.x) < 1e-6f) ? 1e30f : std::abs(cellSize / rayDir.x);
    float tDeltaZ = (std::abs(rayDir.z) < 1e-6f) ? 1e30f : std::abs(cellSize / rayDir.z);

    // 最初の境界線までの距離 (Max)
    float tMaxX, tMaxZ;

    if (rayDir.x > 0) {
        tMaxX = ((currentX + 1) * cellSize - origin.x) / rayDir.x;
    }
    else {
        tMaxX = (origin.x - currentX * cellSize) / -rayDir.x;
    }

    if (rayDir.z > 0) {
        tMaxZ = ((currentZ + 1) * cellSize - origin.z) / rayDir.z;
    }
    else {
        tMaxZ = (origin.z - currentZ * cellSize) / -rayDir.z;
    }

    // 0除算ガード補正
    if (std::abs(rayDir.x) < 1e-6f) tMaxX = 1e30f;
    if (std::abs(rayDir.z) < 1e-6f) tMaxZ = 1e30f;

    // ループ実行
    while (true) {
        // 近い方の軸を進める
        if (tMaxX < tMaxZ) {
            if (tMaxX > maxDistance) break;
            currentX += stepX;
            tMaxX += tDeltaX;

            if (IsWall(currentX, currentZ)) {
                info.isHit    = true;
                info.distance = tMaxX - tDeltaX;
                info.point    = origin + rayDir * info.distance;
                info.normal   = Vector3(-float(stepX), 0, 0); // X面衝突
                return info;
            }
        }
        else {
            if (tMaxZ > maxDistance) break;
            currentZ += stepZ;
            tMaxZ += tDeltaZ;

            if (IsWall(currentX, currentZ)) {
                info.isHit    = true;
                info.distance = tMaxZ - tDeltaZ;
                info.point    = origin + rayDir * info.distance;
                info.normal   = Vector3(0, 0, -float(stepZ)); // Z面衝突
                return info;
            }
        }
    }

    return info;
}


// 周囲４マスのベクトルを取得してブレンドして移動する
Vector3 StageMap::GetFlowDirection(int slotIndex, const Vector3 &worldPos) const
{
    // 安全策
    if (slotIndex < 0 || slotIndex >= (int)flowFields.size()) return Vector3::Zero;
    const auto &field = flowFields[slotIndex];
    if (field.vectors.empty()) return Vector3::Zero;

    // 1. グリッド座標（浮動小数点）を計算
    //    例えば 座標(10.5, 5.0) で cellSize=2.0 なら -> Grid(5.25, 2.5)
    float gx = worldPos.x / cellSize;
    float gz = worldPos.z / cellSize;

    // 2. 基準となる左上の整数座標
    //    Grid(5.25, 2.5) -> x0=5, z0=2
    //    中心基準で計算されている場合、0.5引く調整が必要な場合があるが、
    //    ここでは簡易的に「その座標が含まれるマス」を基準とする
    int x0 = static_cast<int>(std::floor(gx - 0.5f));
    int z0 = static_cast<int>(std::floor(gz - 0.5f));

    // 3. マス内での相対位置 (0.0 ~ 1.0)
    //    これが「ブレンド率」になる
    float u = gx - 0.5f - x0;
    float v = gz - 0.5f - z0;

    // 4. 周囲4マスのベクトルを取得
    //    (x0, z0)     (x1, z0)
    //    (x0, z1)     (x1, z1)
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    // ヘルパーラムダ: 安全にベクトルを取る
    auto GetVec = [&](int x, int z) -> Vector3 {
        int idx = GetIndex(x, z);
        if (idx == -1) return Vector3::Zero;
        return field.vectors[idx];
    };

    Vector3 v00 = GetVec(x0, z0);
    Vector3 v10 = GetVec(x1, z0);
    Vector3 v01 = GetVec(x0, z1);
    Vector3 v11 = GetVec(x1, z1);

    // ★もし4本ともゼロなら（壁の中など）、補間しようがないのでゼロを返す
    if (v00.LengthSquared() == 0 && v10.LengthSquared() == 0 && v01.LengthSquared() == 0 &&
        v11.LengthSquared() == 0) {
        // フォールバック: そのままのマスを見る
        int cx, cz;
        WorldToGrid(worldPos, cx, cz);
        int idx = GetIndex(cx, cz);
        if (idx != -1) return field.vectors[idx];
        return Vector3::Zero;
    }

    // 5. バイリニア補間 (Bilinear Interpolation)
    //    上段の左右ブレンド
    Vector3 top = Vector3::Lerp(v00, v10, u);
    //    下段の左右ブレンド
    Vector3 bottom = Vector3::Lerp(v01, v11, u);
    //    上下のブレンド
    Vector3 result = Vector3::Lerp(top, bottom, v);

    // 正規化して返す
    if (result.LengthSquared() > 0.001f) {
        result.Normalize();
    }
    return result;
}
