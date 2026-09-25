#pragma once
// ===========================================================================
// 概要: ナビメッシュで移動するキャラクターの目的地・経路・希望速度を持つコンポーネント。
//
// 責務:
//   - AI が決めた目的地と速度を、経路探索（NavPathfindingSystem）と
//     操舵（NavSteeringSystem）へ受け渡す
//
// 備考: AI の状態や物理の計算は持たず、移動に必要な値だけを持つ。
// ===========================================================================
#include <DirectXMath.h>
#include <vector>

struct NavAgentComponent
{
    DirectX::XMFLOAT3 targetPosition = {0.0f, 0.0f, 0.0f}; // 目的地（ワールド座標）

    // String Pulling で求めた最短経路の頂点（ワールド座標）
    std::vector<DirectX::XMFLOAT3> waypoints;

    int currentWaypointIndex = 0; // 今向かっている waypoints の番号

    bool isPathRequested = false; // true にすると次の経路探索で計算し直される

    float currentDesiredSpeed = 0.0f; // AI が指定する移動速度（m/秒）

    float stopDistance = 0.5f; // この距離（m）まで近づいたら次の waypoint へ進む
};