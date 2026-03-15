// Game/Components/CollisionConfigComponent.h
#pragma once

struct CollisionConfigComponent {
    // 床判定用
    bool checkGround = true;
    float groundRayLength = 0.6f;
    float groundRayOffset = 0.5f;

    // 壁判定用
    bool checkWall = true;
    float wallRayLength = 0.5f; // キャラの半径分
    float wallCheckHeight = 1.0f; // 腰の高さ
};