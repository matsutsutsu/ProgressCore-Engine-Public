#pragma once

struct TriggerComponent {
    float radius       = 3.0f;  // 反応距離
    bool  isOneShot    = false; // trueなら一度発動したら二度と発動しない
    bool  hasTriggered = false; // 発動済みフラグ
    bool  wasInside    = false; // 前フレームで範囲内にいたか
};