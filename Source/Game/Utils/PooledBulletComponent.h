#pragma once
#include "ECS/Common/CCL_Common.h"
#include <cstdint>

struct PooledBulletComponent {
    // char配列の文字列比較を廃止し、ハッシュ値(整数)のみで管理する
    uint32_t poolKeyHash = 0;

    // true: 使用中, false: 待機中（プールに戻されている）
    bool isActive = true;
};