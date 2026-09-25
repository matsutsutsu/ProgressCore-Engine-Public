#pragma once
// ===========================================================================
// 概要: スプライン軌道上を等速で進むレールカメラ（台車）のコンポーネントと、
//       読み込んだ軌道データ。
//
// 責務:
//   - 軌道ファイルのパスと再生設定（速度・先読み）を持つ
//   - 読み込んだ制御点と、距離→t の対応表（LUT）を保持する
//
// 備考: 移動と LUT 構築は CameraDollySystem が行う。
// ===========================================================================
#include <SimpleMath.h>
#include <string>
#include <vector>
#include <memory>

// 距離→t の対応表（LUT）の1サンプル
struct PathSample
{
    float distance; // 開始点からの累積距離（m）
    float t;        // その距離に対応するスプラインのパラメータ t
};

// スプライン軌道の制御点を保持する実行時リソース
struct CameraPathAsset
{
    std::vector<DirectX::SimpleMath::Vector3> points;         // 制御点（ワールド座標）
    bool                                      isLoop = false; // true なら終点と始点をつないだ閉曲線

    // 等速移動のためのデータ
    std::vector<PathSample> lut;                // 距離と t の対応表
    float                   totalLength = 0.0f; // 軌道の総延長（m）
};

// レールカメラの台車コンポーネント
struct CameraDollyCartComponent
{
    std::string pathAsset = ""; // 軌道 JSON のパス

    float currentDistance = 0.0f; // 軌道上の現在位置（開始点からの距離、m）
    float moveSpeed       = 5.0f; // 進む速さ（m/秒）
    bool  lookForward     = true; // true なら進行方向の先を注視する
    float lookAheadOffset = 2.0f; // 注視する先読み距離（m）

    std::shared_ptr<CameraPathAsset> loadedPath = nullptr; // 読み込み済みの軌道。未読み込みなら nullptr

    // 読み込みに失敗したか。立っていれば再試行しない（毎フレームのエラーログを防ぐ）
    bool hasLoadError = false;
};
