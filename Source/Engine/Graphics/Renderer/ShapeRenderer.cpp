#include "ShapeRenderer.h"
#include "Engine/Core/Common/Misc.h"
#include "Engine/Graphics/Core/GpuResourceUtils.h"

using namespace DirectX;

// ヘルパー
bool ShapeRenderer::IsVisible(const DirectX::BoundingSphere &sphere)
{
    if (!m_cullingEnabled) return true;
    return m_frustum.Intersects(sphere);
}
bool ShapeRenderer::IsVisible(const DirectX::BoundingOrientedBox &box)
{
    if (!m_cullingEnabled) return true;
    return m_frustum.Intersects(box);
}


// コンストラクタ
ShapeRenderer::ShapeRenderer(ID3D11Device* device)
{
	// 入力レイアウト
	D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	// 頂点シェーダー
	GpuResourceUtils::LoadVertexShader(
		device,
		"Assets/Shader/ShapeRendererVS.cso",
		inputElementDesc,
		_countof(inputElementDesc),
		inputLayout.GetAddressOf(),
		vertexShader.GetAddressOf());

	// ピクセルシェーダー
	GpuResourceUtils::LoadPixelShader(
		device,
		"Assets/Shader/ShapeRendererPS.cso",
		pixelShader.GetAddressOf());

	// 定数バッファ
	GpuResourceUtils::CreateConstantBuffer(
		device,
		sizeof(CbMesh),
		constantBuffer.GetAddressOf());

	// 箱メッシュ生成
	CreateBoxMesh(device, 1.0f, 1.0f, 1.0f);

	// 球メッシュ生成
	CreateSphereMesh(device, 1.0f, 32);

	// 半球メッシュ生成
	CreateHalfSphereMesh(device, 1.0f, 32);

	// 円柱メッシュ生成
	CreateCylinderMesh(device, 1.0f, 1.0f, -0.5f, 1.0f, 32);

	// 骨メッシュ生成
	CreateBoneMesh(device, 1.0f);

	// 線分メッシュ生成 (追加)
	CreateLineMesh(device);
}

// 箱描画
void ShapeRenderer::DrawBox(
	const DirectX::XMFLOAT3& position,
	const DirectX::XMFLOAT3& angle,
	const DirectX::XMFLOAT3& size,
	const DirectX::XMFLOAT4& color)
{
    // ★追加: OBBカリング
    if (m_cullingEnabled) {
        BoundingOrientedBox obb;
        obb.Center  = position;
        obb.Extents = {size.x * 0.5f, size.y * 0.5f, size.z * 0.5f}; // sizeは全幅なので半分に

        // オイラー角(angle)からクォータニオン作成
        XMVECTOR Q = XMQuaternionRotationRollPitchYaw(angle.x, angle.y, angle.z);
        XMStoreFloat4(&obb.Orientation, Q);

        if (!m_frustum.Intersects(obb)) return;
    }

	Instance& instance = instances.emplace_back();
	instance.mesh = &boxMesh;
	instance.color = color;

	DirectX::XMMATRIX S = DirectX::XMMatrixScaling(size.x, size.y, size.z);
	DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYaw(angle.x, angle.y, angle.z);
	DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
	DirectX::XMStoreFloat4x4(&instance.worldTransform, S * R * T);
}

// 球描画
void ShapeRenderer::DrawSphere(
	const DirectX::XMFLOAT3& position,
	float radius,
	const DirectX::XMFLOAT4& color)
{
    // ★追加: 球カリング
    if (m_cullingEnabled) {
        BoundingSphere sphere;
        sphere.Center = position;
        sphere.Radius = radius;
        if (!IsVisible(sphere)) return;
    }

	Instance& instance = instances.emplace_back();
	instance.mesh = &sphereMesh;
	instance.color = color;

	DirectX::XMMATRIX S = DirectX::XMMatrixScaling(radius, radius, radius);
	DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
	DirectX::XMStoreFloat4x4(&instance.worldTransform, S * T);
}

// カプセル描画（修正版）
// height: カプセルの全長 (TopからBottomまでの距離)
void ShapeRenderer::DrawCapsule(const DirectX::XMFLOAT4X4 &transform,
    float                                                  radius,
    float                                                  height,
    const DirectX::XMFLOAT4                               &color)
{
    using namespace DirectX;

	// ★追加: 簡易カリング（全体を覆う球体で判定）
    if (m_cullingEnabled) {
        XMMATRIX M = XMLoadFloat4x4(&transform);
        XMVECTOR scale, rot, trans;
        XMMatrixDecompose(&scale, &rot, &trans, M);
        float maxScale = XMVectorGetX(
            XMVectorMax(scale, XMVectorMax(XMVectorSwizzle<1, 2, 3, 0>(scale), scale)));

        // カプセル全体を含む半径 = (高さ/2) + 半径
        float boundingRadius = (height * 0.5f) + radius;

        BoundingSphere sphere;
        XMStoreFloat3(&sphere.Center, trans);
        sphere.Radius = boundingRadius * maxScale;

        if (!IsVisible(sphere)) return;
    }

    // 全長から半径2つ分を引いて、円柱部分の高さを算出
    // 全長が直径より短い場合は円柱部分を0にする
    float cylinderHeight     = (std::max)(0.0f, height - 2.0f * radius);
    float halfCylinderHeight = cylinderHeight * 0.5f;

    XMMATRIX Transform = XMLoadFloat4x4(&transform);

    // 回転成分を含む基底ベクトルを取得し、半径でスケールしておく
    XMVECTOR AxisX = XMVectorScale(Transform.r[0], radius);
    XMVECTOR AxisY = XMVectorScale(Transform.r[1], radius); // 半球用のY軸（円柱用ではない）
    XMVECTOR AxisZ = XMVectorScale(Transform.r[2], radius);

    // 位置計算用のオフセット（ローカル座標系でのY軸方向）
    XMVECTOR TopOffset    = XMVectorSet(0, halfCylinderHeight, 0, 0);
    XMVECTOR BottomOffset = XMVectorSet(0, -halfCylinderHeight, 0, 0);

    // ■ 上半球
    {
        Instance &instance = instances.emplace_back();
        instance.mesh      = &halfSphereMesh;

        // 中心位置：Transformの回転を考慮してオフセットをワールド座標へ変換
        XMVECTOR Position =
            XMVectorAdd(Transform.r[3], XMVector3TransformNormal(TopOffset, Transform));

        // ワールド行列構築：回転成分(Axis)を適用
        XMMATRIX World;
        World.r[0] = AxisX;
        World.r[1] = AxisY;
        World.r[2] = AxisZ;
        World.r[3] = XMVectorSetW(Position, 1.0f);

        XMStoreFloat4x4(&instance.worldTransform, World);
        instance.color = color;
    }

    // ■ 円柱（高さがある場合のみ描画）
    if (cylinderHeight > 0.0001f) {
        Instance &instance = instances.emplace_back();
        instance.mesh      = &cylinderMesh;

        XMMATRIX World;
        World.r[0] = AxisX; // Xは半径
        World.r[1] = XMVectorScale(
            Transform.r[1], cylinderHeight); // Yは円柱の高さ（radiusではなくcylinderHeight）
        World.r[2] = AxisZ;                  // Zは半径
        World.r[3] = Transform.r[3];         // 中心はカプセル全体の中心と同じ

        XMStoreFloat4x4(&instance.worldTransform, World);
        instance.color = color;
    }

    // ■ 下半球
    {
        Instance &instance = instances.emplace_back();
        instance.mesh      = &halfSphereMesh;

        // 中心位置
        XMVECTOR Position =
            XMVectorAdd(Transform.r[3], XMVector3TransformNormal(BottomOffset, Transform));

        // 下半球はX軸で180度回転させて下を向かせる
        // X軸180度回転 ＝ Y軸とZ軸を反転させることと同義（ローカル座標系において）
        XMMATRIX World;
        World.r[0] = AxisX;
        World.r[1] = XMVectorNegate(AxisY); // Y軸反転
        World.r[2] = XMVectorNegate(AxisZ); // Z軸反転
        World.r[3] = XMVectorSetW(Position, 1.0f);

        XMStoreFloat4x4(&instance.worldTransform, World);
        instance.color = color;
    }
}

// ---------------------------------------------------------
// ワイヤーフレーム円柱
// ---------------------------------------------------------
void ShapeRenderer::DrawWireframeCylinder(
    const DirectX::XMFLOAT3 &position, float radius, float height, const DirectX::XMFLOAT4 &color)
{
    using namespace DirectX;

	// ★追加: 簡易カリング
    if (m_cullingEnabled) {
        // 中心から最も遠い点までの距離
        float          r = sqrtf(radius * radius + (height * 0.5f) * (height * 0.5f));
        BoundingSphere sphere;
        sphere.Center = position;
        sphere.Radius = r;
        if (!IsVisible(sphere)) return;
    }

    float    halfHeight = height * 0.5f;
    XMFLOAT3 topPos     = {position.x, position.y + halfHeight, position.z};
    XMFLOAT3 btmPos     = {position.x, position.y - halfHeight, position.z};

    // XZ平面(地面)に円を描くための回転クォータニオン (X軸回り90度)
    XMFLOAT4 xRotation;
    XMStoreFloat4(&xRotation, XMQuaternionRotationRollPitchYaw(XM_PIDIV2, 0, 0));

    // 上下のリングを描画
    DrawWireframeCircle(topPos, radius, color, xRotation);
    DrawWireframeCircle(btmPos, radius, color, xRotation);

    // 上下を繋ぐ4本の支柱を描画 (東西南北)
    DrawLine({position.x - radius, topPos.y, position.z},
        {position.x - radius, btmPos.y, position.z},
        color);
    DrawLine({position.x + radius, topPos.y, position.z},
        {position.x + radius, btmPos.y, position.z},
        color);
    DrawLine({position.x, topPos.y, position.z - radius},
        {position.x, btmPos.y, position.z - radius},
        color);
    DrawLine({position.x, topPos.y, position.z + radius},
        {position.x, btmPos.y, position.z + radius},
        color);
}

// ---------------------------------------------------------
// ワイヤーフレーム円
// ---------------------------------------------------------
void ShapeRenderer::DrawWireframeCircle(const DirectX::XMFLOAT3 &center,
    float                                                        radius,
    const DirectX::XMFLOAT4                                     &color,
    const DirectX::XMFLOAT4                                     &rotation)
{
    using namespace DirectX;

    // 円の分割数（多いほど滑らか）
    const int   segments = 24;
    const float step     = XM_2PI / segments;

    XMVECTOR Q = XMLoadFloat4(&rotation);
    XMVECTOR C = XMLoadFloat3(&center);

    // 最初の点 (角度0)
    // 基本姿勢として XY平面上の円 (radius, 0, 0) を想定し、それを回転させる
    XMVECTOR P_prev_local = XMVectorSet(radius, 0, 0, 0);
    // 回転
    P_prev_local = XMVector3Rotate(P_prev_local, Q);
    // 平行移動
    XMVECTOR P_prev = XMVectorAdd(C, P_prev_local);

    for (int i = 1; i <= segments; ++i) {
        float theta = i * step;

        // 次の点 (XY平面上で計算)
        XMVECTOR P_curr_local = XMVectorSet(radius * cosf(theta), radius * sinf(theta), 0, 0);

        // 回転と移動
        P_curr_local    = XMVector3Rotate(P_curr_local, Q);
        XMVECTOR P_curr = XMVectorAdd(C, P_curr_local);

        // 線を描画
        XMFLOAT3 p1, p2;
        XMStoreFloat3(&p1, P_prev);
        XMStoreFloat3(&p2, P_curr);
        DrawLine(p1, p2, color);

        P_prev = P_curr;
    }
}

// ---------------------------------------------------------
// 矢印
// ---------------------------------------------------------
void ShapeRenderer::DrawArrow(const DirectX::XMFLOAT3 &start,
    const DirectX::XMFLOAT3                           &end,
    float                                              headSize,
    const DirectX::XMFLOAT4                           &color)
{
	using namespace DirectX;

    // 軸線
    DrawLine(start, end, color);

    // 向きベクトル計算
    XMVECTOR VStart = XMLoadFloat3(&start);
    XMVECTOR VEnd   = XMLoadFloat3(&end);
    XMVECTOR Dir    = XMVectorSubtract(VEnd, VStart);
    float    length = XMVectorGetX(XMVector3Length(Dir));

    // 長さがほぼゼロなら矢印を描かない
    if (length < 0.001f) return;

    Dir = XMVector3Normalize(Dir);

    // 鏃の根元位置
    XMVECTOR Base = XMVectorSubtract(VEnd, XMVectorScale(Dir, headSize));

    // 鏃の向きを計算するための基準ベクトル
    XMVECTOR Up = XMVectorSet(0, 1, 0, 0);
    // 向きが真上/真下に近い場合は、基準をZ軸ずらす
    if (fabsf(XMVectorGetY(Dir)) > 0.99f) Up = XMVectorSet(0, 0, 1, 0);

    XMVECTOR Right = XMVector3Normalize(XMVector3Cross(Up, Dir));
    Up             = XMVector3Normalize(XMVector3Cross(Dir, Right));

    // 鏃の4つの角を計算
    float    halfHead = headSize * 0.5f;
    XMVECTOR P1       = XMVectorAdd(Base, XMVectorScale(Right, halfHead));
    XMVECTOR P2       = XMVectorSubtract(Base, XMVectorScale(Right, halfHead));
    XMVECTOR P3       = XMVectorAdd(Base, XMVectorScale(Up, halfHead));
    XMVECTOR P4       = XMVectorSubtract(Base, XMVectorScale(Up, halfHead));

    XMFLOAT3 tip, p1, p2, p3, p4;
    XMStoreFloat3(&tip, VEnd);
    XMStoreFloat3(&p1, P1);
    XMStoreFloat3(&p2, P2);
    XMStoreFloat3(&p3, P3);
    XMStoreFloat3(&p4, P4);

    // 鏃の線を描画
    DrawLine(tip, p1, color);
    DrawLine(tip, p2, color);
    DrawLine(tip, p3, color);
    DrawLine(tip, p4, color);
    // 鏃の底面を繋ぐ
    DrawLine(p1, p3, color);
    DrawLine(p3, p2, color);
    DrawLine(p2, p4, color);
    DrawLine(p4, p1, color);
}

// 骨描画
void ShapeRenderer::DrawBone(
	const DirectX::XMFLOAT4X4& transform,
	float length,
	const DirectX::XMFLOAT4& color)
{
	Instance& instance = instances.emplace_back();
	instance.mesh = &boneMesh;
	instance.color = color;

	DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&transform);
	W.r[0] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(W.r[0]), length);
	W.r[1] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(W.r[1]), length);
	W.r[2] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(W.r[2]), length);
	DirectX::XMStoreFloat4x4(&instance.worldTransform, W);
}

// 線描画
void ShapeRenderer::DrawLine(
	const DirectX::XMFLOAT3& p1,
	const DirectX::XMFLOAT3& p2,
	const DirectX::XMFLOAT4& color)
{
    // ★追加: 線分のカリング
    // 線分を包含する球を作ってチェックする（高速かつ十分な精度）
    if (m_cullingEnabled) {
        XMVECTOR V1     = XMLoadFloat3(&p1);
        XMVECTOR V2     = XMLoadFloat3(&p2);
        XMVECTOR Center = (V1 + V2) * 0.5f;
        XMVECTOR Diff   = V2 - V1;
        float    radius = XMVectorGetX(XMVector3Length(Diff)) * 0.5f;

        BoundingSphere sphere;
        XMStoreFloat3(&sphere.Center, Center);
        sphere.Radius = radius;

        if (!m_frustum.Intersects(sphere)) return;
    }

	using namespace DirectX;

	Instance& instance = instances.emplace_back();
	instance.mesh = &lineMesh;
	instance.color = color;

	// 2点間のベクトルと距離を計算
	XMVECTOR V1 = XMLoadFloat3(&p1);
	XMVECTOR V2 = XMLoadFloat3(&p2);
	XMVECTOR Diff = XMVectorSubtract(V2, V1);
	XMVECTOR LengthVec = XMVector3Length(Diff);
	float length;
	XMStoreFloat(&length, LengthVec);

	// 長さがほぼ0なら描画しない（計算エラー防止）
	if (length < 0.0001f)
	{
		XMStoreFloat4x4(&instance.worldTransform, XMMatrixIdentity());
		return;
	}

	// 向き（Z軸をこの向きに合わせる）
	XMVECTOR Forward = XMVectorScale(Diff, 1.0f / length);

	// 上ベクトルを仮定して右ベクトルを算出
	// Forwardが真上(0,1,0)に近い場合は、仮の上ベクトルをZ(0,0,1)にする
	XMVECTOR Up = XMVectorSet(0, 1, 0, 0);
	if (fabsf(XMVectorGetY(Forward)) > 0.99f)
	{
		Up = XMVectorSet(0, 0, 1, 0);
	}

	XMVECTOR Right = XMVector3Normalize(XMVector3Cross(Up, Forward));
	Up = XMVector3Normalize(XMVector3Cross(Forward, Right));

	// ワールド行列構築 (Scale * Rotation * Translation)
	// Z軸方向に長さを伸ばす
	XMMATRIX World;
	World.r[0] = Right;
	World.r[1] = Up;
	World.r[2] = XMVectorScale(Forward, length); // Z軸を長さ分スケール
	World.r[3] = XMVectorSetW(V1, 1.0f);         // 始点に移動

	XMStoreFloat4x4(&instance.worldTransform, World);
}

// 3つの頂点を線で結ぶだけなので、内部でDrawLineを再利用する（保守性の担保）
void ShapeRenderer::DrawTriangle(const DirectX::XMFLOAT3 &v1,
    const DirectX::XMFLOAT3                              &v2,
    const DirectX::XMFLOAT3                              &v3,
    const DirectX::XMFLOAT4                              &color)
{
    DrawLine(v1, v2, color);
    DrawLine(v2, v3, color);
    DrawLine(v3, v1, color);
}

// レイ描画
void ShapeRenderer::DrawRay(
	const DirectX::XMFLOAT3& origin,
	const DirectX::XMFLOAT3& direction,
	float length,
	const DirectX::XMFLOAT4& color)
{
	using namespace DirectX;
	XMVECTOR O = XMLoadFloat3(&origin);
	XMVECTOR D = XMLoadFloat3(&direction);
	XMVECTOR End = XMVectorAdd(O, XMVectorScale(XMVector3Normalize(D), length));

	XMFLOAT3 endPos;
	XMStoreFloat3(&endPos, End);

	DrawLine(origin, endPos, color);
}

// グリッド描画
void ShapeRenderer::DrawGrid(float spacing, int subdivisions, const DirectX::XMFLOAT4& color)
{
	// 中心から全幅の半分
	float halfSize = spacing * subdivisions * 0.5f;

	// X軸平行の線 (Zを動かす)
	for (int i = 0; i <= subdivisions; ++i)
	{
		float z = -halfSize + i * spacing;
		DrawLine(
			{ -halfSize, 0, z },
			{ halfSize, 0, z },
			color
		);
	}

	// Z軸平行の線 (Xを動かす)
	for (int i = 0; i <= subdivisions; ++i)
	{
		float x = -halfSize + i * spacing;
		DrawLine(
			{ x, 0, -halfSize },
			{ x, 0,  halfSize },
			color
		);
	}
}

// 座標軸描画 (ギズモ)
void ShapeRenderer::DrawAxis(const DirectX::XMFLOAT4X4& transform, float length)
{
	using namespace DirectX;
	XMMATRIX M = XMLoadFloat4x4(&transform);
	XMVECTOR Pos = M.r[3];

	// 軸の先端位置を計算
	XMVECTOR XEnd = XMVectorAdd(Pos, XMVectorScale(XMVector3Normalize(M.r[0]), length));
	XMVECTOR YEnd = XMVectorAdd(Pos, XMVectorScale(XMVector3Normalize(M.r[1]), length));
	XMVECTOR ZEnd = XMVectorAdd(Pos, XMVectorScale(XMVector3Normalize(M.r[2]), length));

	XMFLOAT3 p, x, y, z;
	XMStoreFloat3(&p, Pos);
	XMStoreFloat3(&x, XEnd);
	XMStoreFloat3(&y, YEnd);
	XMStoreFloat3(&z, ZEnd);

	// 赤(X), 緑(Y), 青(Z) で描画
	DrawLine(p, x, { 1.0f, 0.0f, 0.0f, 1.0f });
	DrawLine(p, y, { 0.0f, 1.0f, 0.0f, 1.0f });
	DrawLine(p, z, { 0.0f, 0.0f, 1.0f, 1.0f });
}

// 線分メッシュ生成 (Z軸方向に長さ1の線)
void ShapeRenderer::CreateLineMesh(ID3D11Device* device)
{
	std::vector<DirectX::XMFLOAT3> vertices;
	// 原点からZ+方向に1単位伸びる線
	vertices.emplace_back(DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
	vertices.emplace_back(DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f));

	CreateMesh(device, vertices, lineMesh);
}

// メッシュ生成
void ShapeRenderer::CreateMesh(ID3D11Device* device, const std::vector<DirectX::XMFLOAT3>& vertices, Mesh& mesh)
{
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = static_cast<UINT>(sizeof(DirectX::XMFLOAT3) * vertices.size());
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;
	D3D11_SUBRESOURCE_DATA subresourceData = {};
	subresourceData.pSysMem = vertices.data();
	subresourceData.SysMemPitch = 0;
	subresourceData.SysMemSlicePitch = 0;

	HRESULT hr = device->CreateBuffer(&desc, &subresourceData, mesh.vertexBuffer.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

	mesh.vertexCount = static_cast<UINT>(vertices.size());
}

// 箱メッシュ作成
void ShapeRenderer::CreateBoxMesh(ID3D11Device* device, float width, float height, float depth)
{
	DirectX::XMFLOAT3 positions[8] =
	{
		// top
		{ -width,  height, -depth},
		{  width,  height, -depth},
		{  width,  height,  depth},
		{ -width,  height,  depth},
		// bottom
		{ -width, -height, -depth},
		{  width, -height, -depth},
		{  width, -height,  depth},
		{ -width, -height,  depth},
	};

	std::vector<DirectX::XMFLOAT3> vertices;
	vertices.resize(32);

	// top
	vertices.emplace_back(positions[0]);
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[0]);
	// bottom
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[6]);
	vertices.emplace_back(positions[6]);
	vertices.emplace_back(positions[7]);
	vertices.emplace_back(positions[7]);
	vertices.emplace_back(positions[4]);
	// side
	vertices.emplace_back(positions[0]);
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[6]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[7]);

	// メッシュ生成
	CreateMesh(device, vertices, boxMesh);
}

// 球メッシュ作成
void ShapeRenderer::CreateSphereMesh(ID3D11Device* device, float radius, int subdivisions)
{
	float step = DirectX::XM_2PI / subdivisions;

	std::vector<DirectX::XMFLOAT3> vertices;

	// XZ平面
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& p = vertices.emplace_back();
			p.x = sinf(theta) * radius;
			p.y = 0.0f;
			p.z = cosf(theta) * radius;
		}
	}
	// XY平面
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& p = vertices.emplace_back();
			p.x = sinf(theta) * radius;
			p.y = cosf(theta) * radius;
			p.z = 0.0f;
		}
	}
	// YZ平面
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& p = vertices.emplace_back();
			p.x = 0.0f;
			p.y = sinf(theta) * radius;
			p.z = cosf(theta) * radius;
		}
	}

	// メッシュ生成
	CreateMesh(device, vertices, sphereMesh);
}

// 半球メッシュ作成
void ShapeRenderer::CreateHalfSphereMesh(ID3D11Device* device, float radius, int subdivisions)
{
	std::vector<DirectX::XMFLOAT3> vertices;

	float theta_step = DirectX::XM_2PI / subdivisions;

	// XZ平面
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = theta_step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& v = vertices.emplace_back();

			v.x = sinf(theta) * radius;
			v.y = 0.0f;
			v.z = cosf(theta) * radius;
		}
	}
	// XY平面
	for (int i = 0; i < subdivisions / 2; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = theta_step * ((i + j) % subdivisions) - DirectX::XM_PIDIV2;

			DirectX::XMFLOAT3& v = vertices.emplace_back();

			v.x = sinf(theta) * radius;
			v.y = cosf(theta) * radius;
			v.z = 0.0f;
		}
	}
	// YZ平面
	for (int i = 0; i < subdivisions / 2; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = theta_step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& v = vertices.emplace_back();

			v.x = 0.0f;
			v.y = sinf(theta) * radius;
			v.z = cosf(theta) * radius;
		}
	}

	// メッシュ生成
	CreateMesh(device, vertices, halfSphereMesh);
}

// 円柱
void ShapeRenderer::CreateCylinderMesh(ID3D11Device* device, float radius1, float radius2, float start, float height, int subdivisions)
{
	std::vector<DirectX::XMFLOAT3> vertices;

	float theta_step = DirectX::XM_2PI / subdivisions;

	// XZ平面
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = theta_step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& v = vertices.emplace_back();

			v.x = sinf(theta) * radius1;
			v.y = start;
			v.z = cosf(theta) * radius1;
		}
	}
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = theta_step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& v = vertices.emplace_back();

			v.x = sinf(theta) * radius2;
			v.y = start + height;
			v.z = cosf(theta) * radius2;
		}
	}
	// XY平面
	{
		vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start, radius1));
		vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start + height, radius2));
		vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start, -radius1));
		vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start + height, -radius2));
	}
	// YZ平面
	{
		vertices.emplace_back(DirectX::XMFLOAT3(radius1, start, 0.0f));
		vertices.emplace_back(DirectX::XMFLOAT3(radius2, start + height, 0.0f));
		vertices.emplace_back(DirectX::XMFLOAT3(-radius1, start, 0.0f));
		vertices.emplace_back(DirectX::XMFLOAT3(-radius2, start + height, 0.0f));
	}

	// メッシュ生成
	CreateMesh(device, vertices, cylinderMesh);
}

// 骨メッシュ作成
void ShapeRenderer::CreateBoneMesh(ID3D11Device* device, float length)
{
	float width = length * 0.25f;
	DirectX::XMFLOAT3 positions[8] =
	{
		{ -0.00f,  0.00f,  0.00f},
		{  width,  0.00f,  width},
		{  0.00f,  0.00f,  length},
		{ -width,  0.00f,  width},
		{  0.00f,  width,  width},
		{  0.00f, -width,  width},
	};

	std::vector<DirectX::XMFLOAT3> vertices;
	vertices.reserve(24);

	// xz
	vertices.emplace_back(positions[0]);
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[0]);
	// yz
	vertices.emplace_back(positions[0]);
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[0]);
	// xy
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[1]);

	// メッシュ生成
	CreateMesh(device, vertices, boneMesh);
}

// 描画実行
void ShapeRenderer::Render(
	ID3D11DeviceContext* dc,
	const DirectX::XMFLOAT4X4& view,
	const DirectX::XMFLOAT4X4& projection)
{
    // Frustum更新
    BoundingFrustum frustumLocal;
    BoundingFrustum::CreateFromMatrix(frustumLocal, XMLoadFloat4x4(&projection));
    XMMATRIX V    = XMLoadFloat4x4(&view);
    XMMATRIX invV = XMMatrixInverse(nullptr, V);
    frustumLocal.Transform(m_frustum, invV);


	// シェーダー設定
	dc->VSSetShader(vertexShader.Get(), nullptr, 0);
	dc->PSSetShader(pixelShader.Get(), nullptr, 0);
	dc->IASetInputLayout(inputLayout.Get());

	// 定数バッファ設定
	dc->VSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());

	// ビュープロジェクション行列作成
	DirectX::XMMATRIX P = DirectX::XMLoadFloat4x4(&projection);
	DirectX::XMMATRIX VP = V * P;

	// プリミティブ設定
	UINT stride = sizeof(DirectX::XMFLOAT3);
	UINT offset = 0;
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	for (const Instance& instance : instances)
	{
		// 頂点バッファ設定
		dc->IASetVertexBuffers(0, 1, instance.mesh->vertexBuffer.GetAddressOf(), &stride, &offset);

		// ワールドビュープロジェクション行列作成
		DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&instance.worldTransform);
		DirectX::XMMATRIX WVP = W * VP;

		// 定数バッファ更新
		CbMesh cbMesh;
		DirectX::XMStoreFloat4x4(&cbMesh.worldViewProjection, WVP);
		cbMesh.color = instance.color;

		dc->UpdateSubresource(constantBuffer.Get(), 0, 0, &cbMesh, 0, 0);

		// 描画
		dc->Draw(instance.mesh->vertexCount, 0);
	}
	instances.clear();
}
