#pragma once
#include <DirectXMath.h>
#include <vector>

struct NavAgentComponent {
    // 目的地
    DirectX::XMFLOAT3 targetPosition = { 0.0f, 0.0f, 0.0f };

    // String Pulling で計算された最短経路の頂点リスト（最大16〜32個程度で十分）
    std::vector<DirectX::XMFLOAT3> waypoints;

    // 現在向かっているウェイポイントのインデックス
    int currentWaypointIndex = 0;

    // 経路探索を要求したかどうかのフラグ
    bool isPathRequested = false;


};