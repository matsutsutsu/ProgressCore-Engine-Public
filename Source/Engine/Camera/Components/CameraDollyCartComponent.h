#pragma once
#include <SimpleMath.h>
#include <string>
#include <vector>
#include <memory>

// --- 追加：LUT用構造体 ---
struct PathSample
{
    float distance; // 開始点からの累積距離 (m)
    float t;        // その距離に対応するパラメータ t
};

/// @brief スプライン軌道の座標群を保持する実行時リソース
struct CameraPathAsset
{
    std::vector<DirectX::SimpleMath::Vector3> points;
    bool                                      isLoop = false;

    // --- 追加：等速移動のためのデータ ---
    std::vector<PathSample> lut;                // 距離とtの対応表
    float                   totalLength = 0.0f; // 軌道の総延長 (m)
};

/// @brief レールカメラの台車コンポーネント
struct CameraDollyCartComponent
{
    std::string pathAsset = "";

    float currentDistance = 0.0f;
    float moveSpeed       = 5.0f;
    bool  lookForward     = true;
    float lookAheadOffset = 2.0f;

    std::shared_ptr<CameraPathAsset> loadedPath = nullptr;

    // 毎フレームのエラーログスパムを防止するための実行時フラグ
    bool hasLoadError = false;
};