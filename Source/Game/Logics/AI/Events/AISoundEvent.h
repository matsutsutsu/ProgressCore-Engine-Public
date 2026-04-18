#pragma once
#include "SimpleMath.h"

// 空間で発生した音の情報をAIに伝えるイベント
struct AISoundEvent {
    DirectX::SimpleMath::Vector3 position; // 音が発生したワールド座標
    float volumeRadius;                    // 音が届く半径（強さ）
};