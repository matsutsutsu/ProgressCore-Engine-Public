#pragma once

struct EnemyComponent {
  float speed       = 5.0f;  // プレイヤーを追いかける速度
  bool  isBlownAway = false; // 吹き飛ばされ中かどうかのフラグ
};