#pragma once
// ===========================================================================
// 概要: 回転表現の変換ユーティリティ TransformUtils。
//
// 責務:
//   - 回転行列・クォータニオンからピッチ・ヨー・ロール（ラジアン）を求める
// ===========================================================================
#include <DirectXMath.h>

class TransformUtils
{
public:
    /**
     * @brief 回転行列からピッチ・ヨー・ロール（ラジアン）を求める。
     * @return ピッチがちょうど ±90°（ジンバルロック）なら false。このときヨーを 0 に固定し、回転をロールへ寄せる
     */
    static bool MatrixToRollPitchYaw(const DirectX::XMFLOAT4X4 &m, float &pitch, float &yaw, float &roll);

    // クォータニオンからピッチ・ヨー・ロールを求める。戻り値は MatrixToRollPitchYaw と同じ
    static bool QuaternionToRollPitchYaw(const DirectX::XMFLOAT4 &q, float &pitch, float &yaw, float &roll);
};
