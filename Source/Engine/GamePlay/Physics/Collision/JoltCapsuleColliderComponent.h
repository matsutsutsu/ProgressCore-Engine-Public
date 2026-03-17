#pragma once

// カプセルの形状（設計図）
// Joltの仕様では、「円柱部分の半分の高さ(halfHeight)」と「球部分の半径(radius)」で定義します。
// 全体の高さ = (halfHeight * 2) + (radius * 2) になります。
struct JoltCapsuleColliderComponent {
    float halfHeight = 0.5f; // 円柱部分の半分の高さ
    float radius     = 0.5f; // 半径
};