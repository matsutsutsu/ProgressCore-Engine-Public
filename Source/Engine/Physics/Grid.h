//#pragma once
//#include "ECS/Common/CCL_Common.h"
//#include <SimpleMath.h>
//#include <algorithm>
//#include <cmath>
//#include <cstring> // memset用
//#include <vector>
//#include "Engine/GamePlay/Physics/Collision/CollisionLayers.h"
//#include <type_traits> // ★追加: is_invocable_v を使うために必要
//
//// ===================================================================================
//// クラス名: Grid
//// 役割: 空間分割（Spatial Hash Grid）による広域衝突判定のデータ構造
////
//// 機能:
////   - 3D空間をセルに分割し、どのセルに誰がいるかを高速に検索可能にする
////   - RaycastUtil や CollisionSystem から利用される
////   - 静的メッシュ（StaticMesh）と動的オブジェクトを管理
//// ===================================================================================
//
//
//// コライダの種類（後の判定でGetComponentを減らすため）
//enum class ColliderType : uint8_t
//{
//    Sphere,
//    Box,
//    Capsule,
//    StaticMesh,
//    None
//};
//
//// Grid内の連結リスト用ノード
//struct GridEntry
//{
//    CCL::ECS::EntityID id;
//    ColliderType       type;
//    Layer              layer; // フィルタリング用レイヤー情報
//
//    // ポインタではなく「配列のインデックス」で繋ぐ
//    // -1 なら「次はない（nullptr相当）」とする
//    int next = -1;
//};
//
//struct Grid
//{
//    using Vector3 = DirectX::SimpleMath::Vector3;
//
//    // 設定定数: マップの広さに応じて調整（2の乗数が計算速い）
//    static constexpr int TABLE_SIZE = 4096;
//    float                cellSize   = 5.0f;
//
//    // --------------------------------------------------------
//    // メモリ管理 (Pool Allocator)
//    // --------------------------------------------------------
//    // メモリ確保・解放を繰り返さないためのプール
//    std::vector<GridEntry> entryPool;
//    size_t                 poolCursor = 0;
//
//    // ハッシュテーブル（バケツ）
//    // バケツには「そのセルの先頭要素が entryPool の何番目にあるか」を入れる
//    // GridEntry *buckets[TABLE_SIZE]; // ← これをやめる
//    int buckets[TABLE_SIZE]; // ← intにする
//
//    // 静的メッシュなど（グリッド管理外の巨大オブジェクト）
//    std::vector<CCL::ECS::EntityID> staticLargeMeshes;
//
//    Grid()
//    {
//        // 初期容量確保（足りなくなったら自動で倍に増える）
//        entryPool.resize(4096);
//        // バケツを空にする
//        std::memset(buckets, -1, sizeof(buckets));
//    }
//
//    // --------------------------------------------------------
//    // 操作メソッド
//    // --------------------------------------------------------
//
//    // フレーム開始時に呼ぶリセット処理 (コスト: ほぼゼロ)
//    void Clear()
//    {
//        // バケツの中身をポインタクリア（メモリ解放はしない）
//        std::memset(buckets, -1, sizeof(buckets));
//
//        // プールの使用位置を先頭に戻す（これで再利用状態になる）
//        poolCursor = 0;
//
//        staticLargeMeshes.clear();
//    }
//
//    // AABB領域(min-max)に対してオブジェクトを登録する
//    void Insert(const Vector3 &min,
//        const Vector3         &max,
//        CCL::ECS::EntityID     id,
//        ColliderType           type,
//        Layer                  layer)
//    {
//        // 1. AABBが重なるセル範囲（インデックス）を計算
//        // floorを使って負の座標も正しく処理
//        int minX = static_cast<int>(std::floor(min.x / cellSize));
//        int maxX = static_cast<int>(std::floor(max.x / cellSize));
//        int minY = static_cast<int>(std::floor(min.y / cellSize));
//        int maxY = static_cast<int>(std::floor(max.y / cellSize));
//        int minZ = static_cast<int>(std::floor(min.z / cellSize));
//        int maxZ = static_cast<int>(std::floor(max.z / cellSize));
//
//        // 2. 範囲内の全セルに登録
//        for (int z = minZ; z <= maxZ; ++z)
//        {
//            for (int y = minY; y <= maxY; ++y)
//            {
//                for (int x = minX; x <= maxX; ++x)
//                {
//                    AddEntryToCell(x, y, z, id, type, layer);
//                }
//            }
//        }
//    }
//
//    // 指定座標のセルにあるオブジェクトを取得する
//    // callback(EntityID id, ColliderType type)
//    template <typename Func> void ForEachInCell(const Vector3 &pos, Func &&callback) const
//    {
//        int x = static_cast<int>(std::floor(pos.x / cellSize));
//        int y = static_cast<int>(std::floor(pos.y / cellSize));
//        int z = static_cast<int>(std::floor(pos.z / cellSize));
//
//        uint32_t bucketIndex = ComputeHash(x, y, z);
//
//        // 連結リストをたどる
//        // ポインタではなくインデックスでループする
//        // i が -1 になるまで回す
//        for (int i = buckets[bucketIndex]; i != -1; i = entryPool[i].next) {
//            // entryPool[i] でアクセスする
//            const GridEntry &e = entryPool[i];
//            // ★修正: コールバック関数が「3つの引数を受け取れるか」をコンパイル時に判定
//            // 受け取れるなら Layer も渡す (CollisionSystem用)
//            // 受け取れないなら 従来通り2つだけ渡す (RaycastUtil用)
//            if constexpr (std::is_invocable_v<Func, CCL::ECS::EntityID, ColliderType, Layer>) {
//                callback(e.id, e.type, e.layer);
//            }
//            else {
//                callback(e.id, e.type);
//            }
//        }
//    }
//
//  private:
//    // ハッシュ計算関数
//    uint32_t ComputeHash(int x, int y, int z) const
//    {
//        // 座標(x,y,z)をめちゃくちゃ混ぜて1つの数字にする
//        // 大きな素数を掛けてXOR（空間ハッシュの定番）
//        uint32_t h = (x * 73856093) ^ (y * 19349663) ^ (z * 83492791);
//        return h % TABLE_SIZE; // ビットAND (h & (TABLE_SIZE - 1)) だともっと速い
//    }
//
//    void AddEntryToCell(int x, int y, int z, CCL::ECS::EntityID id, ColliderType type, Layer layer)
//    {
//        uint32_t index = ComputeHash(x, y, z);
//
//        // プール容量チェック & 拡張
//        if (poolCursor >= entryPool.size()) {
//            entryPool.resize(entryPool.size() * 2);
//            // ※インデックス管理に切り替えたので、リサイズしてもクラッシュしません！
//        }
//
//        // 現在のインデックスを取得
//        int entryIndex = static_cast<int>(poolCursor++);
//
//        // プールから参照を取得
//        GridEntry &entry = entryPool[entryIndex];
//
//        // データセット
//        entry.id   = id;
//        entry.type = type;
//        entry.layer = layer; // ★保存
//
//        // 連結リストの繋ぎ変え（ポインタではなくインデックスで）
//        entry.next     = buckets[index]; // 現在の先頭（のインデックス）を next に
//        buckets[index] = entryIndex;     // 自分を新しい先頭（のインデックス）に
//    }
//};
//
// // namespace CCL::ECS::Physics