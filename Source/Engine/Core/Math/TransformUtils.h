#pragma once
// ===========================================================================
// 概要: 回転表現の変換ユーティリティ TransformUtils
//
// 責務:
//   - 回転行列・クォータニオンからピッチ・ヨー・ロール（ラジアン）を求める
// ===========================================================================
#include <DirectXMath.h>

class TransformUtils
{
public:
    // 回転行列からピッチ・ヨー・ロール（ラジアン）を求める
    // ピッチがちょうど ±90°（ジンバルロック）なら false を返し、ヨーを 0 にして回転をロールへ寄せる
    static bool MatrixToRollPitchYaw(const DirectX::XMFLOAT4X4 &m, float &pitch, float &yaw, float &roll);

    // クォータニオンからピッチ・ヨー・ロールを求める。戻り値は MatrixToRollPitchYaw と同じ
    static bool QuaternionToRollPitchYaw(const DirectX::XMFLOAT4 &q, float &pitch, float &yaw, float &roll);
};
