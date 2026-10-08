#pragma once
// ===========================================================================
// 概要: タイトル画面の構図を「画面のどこに置くか」で表すための定義。
//       TitleCameraSystem と TitleSwarmSystem が共有する。
//
// 責務:
//   - 構図の入力（揺らす前の視点・注視点・画角）を受け渡す CompositionBoard を定義する
//   - 画面上の割合と奥行きからワールド座標を求める FrameBasis を作る
//
// 備考: 座標をワールドで直接書かないのは、カメラを 1m 動かすだけで全部の値を引き直すことになるため。
//       画面の割合（中心を 0、端を ±1）で書いておけば、カメラをギズモで動かしても構図は崩れない。
//       群れの側からカメラを探させないのは、カメラが揺れているため。揺れた視点で並べると群れが
//       カメラに合わせて動き、視差が消える。ボードには揺らす前の視点だけを載せる。
// ===========================================================================

#include <DirectXMath.h>
#include <SimpleMath.h>
#include <cmath>
#include <cstdint>

namespace CCL::Title
{
    // 注視点の目印にするエンティティの名前。レベルに1つ置き、ギズモで動かして狙いを決める。
    // 名前で探すのは、タイトル1画面のために専用のコンポーネント（登録・メタ・保存）を作るより軽いため
    inline constexpr const char *kLookTargetName = "TitleLookTarget";

    // 目印が無いときの注視点。ボスの胸の高さで、ボスが画面の下 1/3、群れが上 2/3 に分かれる
    inline DirectX::SimpleMath::Vector3 DefaultLookTarget()
    {
        return {5.0f, 9.0f, -6.0f};
    }

    // 構図の入力をまとめた World リソース。TitleCameraSystem が毎フレーム書き、TitleSwarmSystem が読む
    struct CompositionBoard
    {
        DirectX::SimpleMath::Vector3 eye{};                            // 揺らす前の視点
        DirectX::SimpleMath::Vector3 lookTarget = DefaultLookTarget(); // 注視点
        float                        fov        = 32.0f;               // 垂直画角（度）
        float                        aspect     = 16.0f / 9.0f;        // 画面のアスペクト比

        // 構図が変わったときだけ増える番号。読む側はこれが変わったら並べ直す。
        // 毎フレーム増やすと、群れが毎フレーム並べ直してしまう
        uint32_t revision = 0;
    };

    // 画面の割合（fx, fy）と奥行き（depth）からワールド座標を求めるための基底。
    // fx / fy は画面の中心を 0、右端・上端を +1 とした値
    struct FrameBasis
    {
        DirectX::SimpleMath::Vector3 eye;
        DirectX::SimpleMath::Vector3 forward;
        DirectX::SimpleMath::Vector3 right;
        DirectX::SimpleMath::Vector3 up;

        float tanHalfFov = 0.3f;         // 垂直画角の半分の tan
        float aspect     = 16.0f / 9.0f; // 画面のアスペクト比

        // カメラから depth（m）離れた平面の上で、画面の (fx, fy) に写るワールド座標
        DirectX::SimpleMath::Vector3 Point(float depth, float fx, float fy) const
        {
            const float halfH = depth * tanHalfFov;
            const float halfW = halfH * aspect;
            return eye + forward * depth + right * (fx * halfW) + up * (fy * halfH);
        }
    };

    // 視点・注視点・画角から基底を作る。right = up × forward（左手系）
    inline FrameBasis MakeFrameBasis(const DirectX::SimpleMath::Vector3 &eye,
                                     const DirectX::SimpleMath::Vector3 &lookTarget, float fovDeg, float aspect)
    {
        using namespace DirectX::SimpleMath;

        FrameBasis basis;
        basis.eye     = eye;
        basis.forward = lookTarget - eye;
        if (basis.forward.LengthSquared() < 1e-6f) basis.forward = Vector3::UnitZ;
        basis.forward.Normalize();

        basis.right = Vector3::Up.Cross(basis.forward);
        // 真上・真下を向いたとき right が 0 になり、up も求められなくなるので、代わりの軸を使う
        if (basis.right.LengthSquared() < 1e-6f) basis.right = Vector3::UnitX;
        basis.right.Normalize();

        basis.up = basis.forward.Cross(basis.right);

        basis.tanHalfFov = std::tan(DirectX::XMConvertToRadians(fovDeg) * 0.5f);
        basis.aspect     = (aspect > 0.01f) ? aspect : (16.0f / 9.0f);
        return basis;
    }

    // CompositionBoard から基底を作る
    inline FrameBasis MakeFrameBasis(const CompositionBoard &board)
    {
        return MakeFrameBasis(board.eye, board.lookTarget, board.fov, board.aspect);
    }

} // namespace CCL::Title
