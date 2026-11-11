#include "ScreenToWorld.h"
#include <cmath>

using namespace DirectX;
using namespace DirectX::SimpleMath;

Vector3 ScreenToWorld::GetRayIntersectionY0(
    float mouseX, float mouseY,
    float screenWidth, float screenHeight,
    const Camera &camera)
{
    // 1. 画面領域をビューポートとして定義する（深度は 0..1）
    Viewport vp;
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = screenWidth;
    vp.height   = screenHeight;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    Matrix view  = XMLoadFloat4x4(&camera.GetView());
    Matrix proj  = XMLoadFloat4x4(&camera.GetProjection());
    Matrix world = Matrix::Identity;

    // 2. マウス座標を、ワールド空間の「手前（ニア面）」と「奥（ファー面）」の2点に逆変換する
    Vector3 nearSource(mouseX, mouseY, 0.0f);
    Vector3 farSource(mouseX, mouseY, 1.0f);

    Vector3 nearPoint = vp.Unproject(nearSource, proj, view, world);
    Vector3 farPoint  = vp.Unproject(farSource, proj, view, world);

    // 3. 2点を結んでレイの方向にする
    Vector3 rayDir = farPoint - nearPoint;
    rayDir.Normalize();

    // レイが Y=0 平面と平行（真横を向いているなど）なら交差しない
    if (std::abs(rayDir.y) < 1e-6f)
    {
        return Vector3::Zero;
    }

    // 4. Y=0 平面との交点を求める（始点 O、方向 D として t = -O.y / D.y）
    float   t            = -nearPoint.y / rayDir.y;
    Vector3 intersection = nearPoint + (rayDir * t);

    // 浮動小数点の誤差で残る高さを 0 に揃える
    intersection.y = 0.0f;

    return intersection;
}



void ScreenToWorld::GetRay(
    float mouseX, float mouseY,
    float screenWidth, float screenHeight,
    const Camera &camera,
    Vector3 &outOrigin, Vector3 &outDir)
{
    // GetRayIntersectionY0 と同じ逆変換だが、Y=0 で切らずレイそのものを返す
    Viewport vp;
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = screenWidth;
    vp.height   = screenHeight;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    Matrix view  = XMLoadFloat4x4(&camera.GetView());
    Matrix proj  = XMLoadFloat4x4(&camera.GetProjection());
    Matrix world = Matrix::Identity;

    Vector3 nearSource(mouseX, mouseY, 0.0f);
    Vector3 farSource(mouseX, mouseY, 1.0f);

    Vector3 nearPoint = vp.Unproject(nearSource, proj, view, world);
    Vector3 farPoint  = vp.Unproject(farSource, proj, view, world);

    Vector3 dir = farPoint - nearPoint;
    dir.Normalize();

    outOrigin = nearPoint;
    outDir    = dir;
}
