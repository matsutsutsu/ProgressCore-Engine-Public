#pragma once
// ===========================================================================
// 概要: シーンの静的な地形から NavMesh を生成する（Recast / Detour）。
//
// 責務:
//   - isStatic な Transform と Model を持つエンティティの三角形を、ワールド座標で集める
//   - Recast で NavMesh を生成し、Detour の dtNavMesh / dtNavMeshQuery を作る
//   - 生成物を World のリソース（NavigationData）に登録する（前回分は解放して差し替える）
//
// 備考: 生成したデータはファイルへ保存しない。NavMeshWindow の Bake のたびに作り直す。
// ===========================================================================
#include <vector>

namespace CCL::ECS
{
    class World;
}

// Bake の設定値。NavMeshWindow で調整する
struct NavMeshBuildSettings
{
    float cellSize      = 0.3f;  // ボクセルの水平（XZ）の大きさ [m]。小さいほど精度が高く、重い
    float cellHeight    = 0.2f;  // ボクセルの高さ（Y）の大きさ [m]
    float agentHeight   = 2.0f;  // エージェントの身長 [m]。これより低い天井の下は通れない
    float agentRadius   = 0.6f;  // エージェントの半径 [m]。壁からこの距離だけ離れる
    float agentMaxClimb = 0.9f;  // 登れる段差の最大の高さ [m]（階段を坂として扱う）
    float agentMaxSlope = 45.0f; // 歩ける最大の傾斜 [度]

    // 領域分割と PolyMesh の設定
    float regionMinSize        = 8.0f;  // これより小さい領域（セル数の平方根）は捨てる
    float regionMergeSize      = 20.0f; // これより小さい領域（セル数の平方根）は隣と結合する
    float edgeMaxLen           = 12.0f; // 輪郭線の辺の最大長 [m]
    float edgeMaxError         = 1.3f;  // 輪郭線を単純化するときの許容誤差（セル単位）
    float vertsPerPoly         = 6.0f;  // ポリゴン1つの最大頂点数
    float detailSampleDist     = 6.0f;  // 詳細メッシュの標本間隔（セル単位。0.9 未満で無効）
    float detailSampleMaxError = 1.0f;  // 詳細メッシュの高さの許容誤差（セルの高さ単位）
};

// 集めた三角形。vertices は xyz の並んだ float 配列、indices は3つで1三角形
struct NavMeshBuildData
{
    std::vector<float> vertices;
    std::vector<int>   indices;
};

class NavMeshBuilder
{
public:
    // シーン中の静的なジオメトリを集め、1つの頂点・インデックス配列にまとめる
    static NavMeshBuildData GatherGeometry(CCL::ECS::World &world);

    // 集めたジオメトリから NavMesh を生成し、World のリソースへ登録する。失敗したら false
    static bool BuildNavMesh(CCL::ECS::World &world, const NavMeshBuildSettings &settings);
};