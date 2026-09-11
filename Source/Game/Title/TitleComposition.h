#pragma once
/// タイトル画面の構図を「画面のどこに置くか」で表すための共有定義。
///
/// なぜワールド座標で直接書かないか:
///   群れの座標をワールドで置くと、カメラを 1m 動かすだけで全ての値を引き直すことになる。
///   フレーム基準（画面中心を0、端を±1）で宣言しておけば、カメラをギズモで動かしても
///   「右上に斬線、左は文字のために空ける」という構図そのものは保たれる。
///
/// カメラ演出(TitleCameraSystem)と群れの振り付け(TitleSwarmSystem)は同じ絵を共有する。
/// その受け渡しは CompositionBoard（World リソース）1枚だけで行う。
///
/// なぜ群れ側からカメラを探させないか:
///   カメラは注視点まわりに揺れている。その揺れた視点で構図を組むと、群れがカメラに
///   ぴったり追従して視差が消える（＝揺らしている意味が無くなる）。ボードには
///   「揺れる前の視点」だけを載せ、揺れはカメラの中に閉じ込める。

#include <DirectXMath.h>
#include <SimpleMath.h>
#include <cmath>
#include <cstdint>

namespace CCL::Title
{
/// 注視点の目印として置くエンティティ名。レベルに1つ置き、ギズモで動かして狙いを決める。
/// 名前で引くのは、専用コンポーネントを1つ増やして登録・メタ・保存まで面倒を見るより、
/// 「ワールドに置いた点が正」の方がタイトル1画面のためには軽いため。
inline constexpr const char *kLookTargetName = "TitleLookTarget";

/// 目印が居ないときの注視点。ボスの胸の高さで、ボスが画面下1/3・群れが上2/3に分かれる。
inline DirectX::SimpleMath::Vector3 DefaultLookTarget()
{
    return {5.0f, 9.0f, -6.0f};
}

/// 構図の入力を1か所に集めた受け渡し板（World リソース）。TitleCameraSystem が毎フレーム
/// 更新し、TitleSwarmSystem が読む。
struct CompositionBoard
{
    DirectX::SimpleMath::Vector3 eye{};                            // 揺れを含まない視点
    DirectX::SimpleMath::Vector3 lookTarget = DefaultLookTarget(); // 注視点
    float                        fov        = 32.0f;               // 垂直画角[度]
    float                        aspect     = 16.0f / 9.0f;        // 画面のアスペクト比

    /// 構図が実際に変わった時だけ進む番号。読み手はこれを見て組み直す。
    /// 毎フレーム進めてしまうと、群れが毎フレーム組み直して波が進まなくなる。
    uint32_t revision = 0;
};

/// 画面の割合(fx,fy)と奥行き(depth)からワールド座標を引くための基底。
/// fx/fy は画面中心を0、右端／上端を +1 とした値。
struct FrameBasis
{
    DirectX::SimpleMath::Vector3 eye;
    DirectX::SimpleMath::Vector3 forward;
    DirectX::SimpleMath::Vector3 right;
    DirectX::SimpleMath::Vector3 up;

    float tanHalfFov = 0.3f;          // 垂直画角の半分の tan
    float aspect     = 16.0f / 9.0f;  // 画面のアスペクト比

    /// カメラから depth[m] の平面上で、画面割合(fx,fy)に写るワールド座標。
    DirectX::SimpleMath::Vector3 Point(float depth, float fx, float fy) const
    {
        const float halfH = depth * tanHalfFov;
        const float halfW = halfH * aspect;
        return eye + forward * depth + right * (fx * halfW) + up * (fy * halfH);
    }
};

/// 視点・注視点・画角からフレーム基準を組む。right = up × forward（左手系）。
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
    // 真上／真下を向いた時の保険。ここが 0 になると up まで壊れて構図が消える。
    if (basis.right.LengthSquared() < 1e-6f) basis.right = Vector3::UnitX;
    basis.right.Normalize();

    basis.up = basis.forward.Cross(basis.right);

    basis.tanHalfFov = std::tan(DirectX::XMConvertToRadians(fovDeg) * 0.5f);
    basis.aspect     = (aspect > 0.01f) ? aspect : (16.0f / 9.0f);
    return basis;
}

/// ボードからそのままフレーム基準を組む。
inline FrameBasis MakeFrameBasis(const CompositionBoard &board)
{
    return MakeFrameBasis(board.eye, board.lookTarget, board.fov, board.aspect);
}

} // namespace CCL::Title
