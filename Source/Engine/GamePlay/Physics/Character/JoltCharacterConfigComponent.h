#pragma once

// キャラクターの運動性能（プランナーが調整する値）
struct JoltCharacterConfigComponent {
    float maxSlopeAngle = 45.0f; // 登れる最大角度（度）
    float maxStepHeight = 0.5f;  // 乗り越えられる段差の最大高
    float walkSpeed     = 6.0f;  // 歩行速度
    float jumpSpeed     = 10.0f; // ジャンプの初速
    float characterMass = 70.0f; // 体重（押し出し計算などに影響）
};