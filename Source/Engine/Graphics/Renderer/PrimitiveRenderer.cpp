#include "PrimitiveRenderer.h"
#include <stdio.h>
#include <memory>
#include "Engine/Core/Common/Misc.h"
#include "Engine/Graphics/Core/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/Pass/PrimitiveVS.h"
#include "Engine/Graphics/Shader/Pass/PrimitivePS.h"

using namespace DirectX;

PrimitiveRenderer::PrimitiveRenderer(ID3D11Device* device)
{
	// 頂点シェーダー
	{
		// 頂点シェーダー生成
		HRESULT hr = device->CreateVertexShader(PrimitiveVS, sizeof(PrimitiveVS), nullptr, vertexShader.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		// 入力レイアウト
		D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		hr = device->CreateInputLayout(inputElementDesc, ARRAYSIZE(inputElementDesc), PrimitiveVS, sizeof(PrimitiveVS), inputLayout.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	// ピクセルシェーダー
	{
		// ピクセルシェーダー生成
		HRESULT hr = device->CreatePixelShader(PrimitivePS, sizeof(PrimitivePS), nullptr, pixelShader.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	// 定数バッファ
	{
		// シーン用バッファ
		D3D11_BUFFER_DESC desc;
		::memset(&desc, 0, sizeof(desc));
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.ByteWidth = sizeof(CbScene);
		desc.StructureByteStride = 0;

		HRESULT hr = device->CreateBuffer(&desc, 0, sceneConstantBuffer.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		// メッシュ用バッファ
		desc.ByteWidth = sizeof(CbMesh);

		hr = device->CreateBuffer(&desc, 0, meshConstantBuffer.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	// ブレンドステート
	{
		D3D11_BLEND_DESC desc;
		::memset(&desc, 0, sizeof(desc));
		desc.AlphaToCoverageEnable = false;
		desc.IndependentBlendEnable = false;
		desc.RenderTarget[0].BlendEnable = false;
		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		HRESULT hr = device->CreateBlendState(&desc, opaqueBlendState.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		desc.RenderTarget[0].BlendEnable = true;
		hr = device->CreateBlendState(&desc, alphaBlendState.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	// 深度ステンシルステート
	{
		D3D11_DEPTH_STENCIL_DESC desc;
		::memset(&desc, 0, sizeof(desc));
		desc.DepthEnable = true;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

		HRESULT hr = device->CreateDepthStencilState(&desc, depthStencilState.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	// ラスタライザーステート
	{
		D3D11_RASTERIZER_DESC desc;
		::memset(&desc, 0, sizeof(desc));
		desc.FrontCounterClockwise = false;
		desc.DepthBias = 0;
		desc.DepthBiasClamp = 0;
		desc.SlopeScaledDepthBias = 0;
		desc.DepthClipEnable = true;
		desc.ScissorEnable = false;
		desc.MultisampleEnable = true;
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_BACK;
		desc.AntialiasedLineEnable = false;

		HRESULT hr = device->CreateRasterizerState(&desc, rasterizerState.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		desc.FillMode = D3D11_FILL_WIREFRAME;
		desc.CullMode = D3D11_CULL_NONE;
		desc.AntialiasedLineEnable = true;

		hr = device->CreateRasterizerState(&desc, wireframeRasterizerState.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}


	// 球メッシュ作成
	CreateSphereMesh(device, 1.0f, 16, 16, sphereBuffer);

	// 半球メッシュ作成
	CreateHalfSphereMesh(device, 1.0f, 16, 16, halfSphereBuffer);

	// 円柱メッシュ作成
	CreateCylinderMesh(device, 1.0f, 1.0f, -0.5f, 1.0f, 16, 1, cylinderBuffer);

	// 円錐メッシュ作成
	CreateCylinderMesh(device, 1.0f, 0.0f, 0.0f, 1.0f, 16, 1, coneBuffer);

	// 箱メッシュ作成
	CreateBoxMesh(device, 1.0f, 1.0f, 1.0f, boxBuffer);
}

// =========================================================
// ★追加: カリング判定用ヘルパー実装
// =========================================================
bool PrimitiveRenderer::IsVisible(const DirectX::BoundingSphere &sphere)
{
    if (!m_cullingEnabled) return true;
    return m_frustum.Intersects(sphere);
}

bool PrimitiveRenderer::IsVisible(const DirectX::BoundingOrientedBox &box)
{
    if (!m_cullingEnabled) return true;
    return m_frustum.Intersects(box);
}

// 描画開始
void PrimitiveRenderer::Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& projection)
{
    // 1. 次のフレームのカリング用に視錐台を更新する
    // プロジェクション行列から基本形状を作成
    BoundingFrustum frustumLocal;
    BoundingFrustum::CreateFromMatrix(frustumLocal, XMLoadFloat4x4(&projection));

    // ビュー行列の逆行列（カメラのワールド行列）を使って、視錐台をワールド空間へ変換
    XMMATRIX V    = XMLoadFloat4x4(&view);
    XMMATRIX invV = XMMatrixInverse(nullptr, V);
    frustumLocal.Transform(m_frustum, invV);

	// シェーダー設定
	context->VSSetShader(vertexShader.Get(), nullptr, 0);
	context->PSSetShader(pixelShader.Get(), nullptr, 0);
	context->IASetInputLayout(inputLayout.Get());

	// 定数バッファ設定
	context->VSSetConstantBuffers(0, 1, sceneConstantBuffer.GetAddressOf());
	context->VSSetConstantBuffers(1, 1, meshConstantBuffer.GetAddressOf());
	//context->PSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());

	// ビュープロジェクション行列作成
	DirectX::XMMATRIX P = DirectX::XMLoadFloat4x4(&projection);
	DirectX::XMMATRIX VP = V * P;

	// シーン定数バッファ設定
	CbScene cbScene;
	DirectX::XMStoreFloat4x4(&cbScene.viewProjection, VP);
	cbScene.lightDirection = { 0.0f, -1.0f, 0.0f, 0.0f };

	// プリミティブ設定
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 不透明描画実行
	{
		// シーン定数バッファ更新
		cbScene.lightDirection.w = 0.0f;
		context->UpdateSubresource(sceneConstantBuffer.Get(), 0, 0, &cbScene, 0, 0);

		// レンダーステート設定
		const float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context->OMSetBlendState(opaqueBlendState.Get(), blendFactor, 0xFFFFFFFF);
		context->OMSetDepthStencilState(depthStencilState.Get(), 0);
		context->RSSetState(rasterizerState.Get());

		// 描画
		RenderSpheres(context, opaqueSpheres);
		RenderBoxes(context, opaqueBoxes);
		RenderCylinders(context, opaqueCylinders);
		RenderCones(context, opaqueCones);
		RenderCapsules(context, opaqueCapsules);
		RenderArrows(context, opaqueArrows);
	}

	// ワイヤーフレーム
	{
		// シーン定数バッファ更新
		cbScene.lightDirection.w = 1.0f;
		context->UpdateSubresource(sceneConstantBuffer.Get(), 0, 0, &cbScene, 0, 0);

		// レンダーステート設定
		const float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context->OMSetBlendState(opaqueBlendState.Get(), blendFactor, 0xFFFFFFFF);
		context->OMSetDepthStencilState(depthStencilState.Get(), 0);
		context->RSSetState(wireframeRasterizerState.Get());

		// 描画
		RenderSpheres(context, wireframeSpheres);
		RenderBoxes(context, wireframeBoxes);
		RenderCylinders(context, wireframeCylinders);
		RenderCones(context, wireframeCones);
		RenderCapsules(context, wireframeCapsules);
		RenderArrows(context, wireframeArrows);
	}

	// 半透明描画実行（面倒なのでカメラソートはしない）
	{
		// シーン定数バッファ更新
		cbScene.lightDirection.w = 0.0f;
		context->UpdateSubresource(sceneConstantBuffer.Get(), 0, 0, &cbScene, 0, 0);

		// レンダーステート設定
		const float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context->OMSetBlendState(alphaBlendState.Get(), blendFactor, 0xFFFFFFFF);
		context->OMSetDepthStencilState(depthStencilState.Get(), 0);
		context->RSSetState(rasterizerState.Get());

		// 描画
		RenderSpheres(context, alphaSpheres);
		RenderBoxes(context, alphaBoxes);
		RenderCylinders(context, alphaCylinders);
		RenderCones(context, alphaCones);
		RenderCapsules(context, alphaCapsules);
		RenderArrows(context, alphaArrows);
	}
}

// 球描画
void PrimitiveRenderer::DrawSphere(const DirectX::XMFLOAT4X4& transform, float radius, const DirectX::XMFLOAT4& color)
{
	DrawSphere(color.w < 1.0f ? alphaSpheres : opaqueSpheres, transform, radius, color);
}

void PrimitiveRenderer::DrawWireframeSphere(const DirectX::XMFLOAT4X4& transform, float radius, const DirectX::XMFLOAT4& color)
{
	DrawSphere(wireframeSpheres, transform, radius, color);
}


// 箱描画
void PrimitiveRenderer::DrawBox(const DirectX::XMFLOAT4X4& transform, const DirectX::XMFLOAT3& size, const DirectX::XMFLOAT4& color)
{
	DrawBox(color.w < 1.0f ? alphaBoxes : opaqueBoxes, transform, size, color);
}

void PrimitiveRenderer::DrawWireframeBox(const DirectX::XMFLOAT4X4& transform, const DirectX::XMFLOAT3& size, const DirectX::XMFLOAT4& color)
{
	DrawBox(wireframeBoxes, transform, size, color);
}

// 円柱描画
void PrimitiveRenderer::DrawCylinder(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color)
{
	DrawCylinder(color.w < 1.0f ? alphaCylinders : opaqueCylinders, transform, radius, height, color);
}

void PrimitiveRenderer::DrawWireframeCylinder(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color)
{
	DrawCylinder(wireframeCylinders, transform, radius, height, color);
}

// カプセル描画
void PrimitiveRenderer::DrawCapsule(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color)
{
	DrawCapsule(color.w < 1.0f ? alphaCapsules : opaqueCapsules, transform, radius, height, color);
}

void PrimitiveRenderer::DrawWireframeCapsule(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color)
{
	DrawCapsule(wireframeCapsules, transform, radius, height, color);
}

// 円錐描画
void PrimitiveRenderer::DrawCone(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color)
{
	DrawCone(color.w < 1.0f ? alphaCones : opaqueCones, transform, radius, height, color);
}

void PrimitiveRenderer::DrawWireframeCone(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color)
{
	DrawCone(wireframeCones, transform, radius, height, color);
}

// 矢印描画
void PrimitiveRenderer::DrawArrow(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color, bool boxFlag)
{
	DrawArrow(color.w < 1.0f ? alphaArrows : opaqueArrows, transform, radius, height, color, boxFlag);
}

void PrimitiveRenderer::DrawWireframeArrow(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color, bool boxFlag)
{
	DrawArrow(wireframeArrows, transform, radius, height, color, boxFlag);
}

// 軸描画
void PrimitiveRenderer::DrawAxis(const DirectX::XMFLOAT4X4& transform, const DirectX::XMFLOAT3& size, float radius)
{
	DirectX::XMMATRIX M = DirectX::XMLoadFloat4x4(&transform);
	DirectX::XMMATRIX X = DirectX::XMMatrixRotationX(DirectX::XM_PIDIV2);
	DirectX::XMMATRIX Z = DirectX::XMMatrixRotationZ(-DirectX::XM_PIDIV2);
	DirectX::XMFLOAT4X4 m;

	// X軸
	DirectX::XMStoreFloat4x4(&m, Z * M);
	DrawArrow(opaqueArrows, m, radius, size.x, { 1, 0, 0, 1 });

	// Y軸
	DrawArrow(opaqueArrows, transform, radius, size.y, { 0, 1, 0, 1 });

	// Z軸
	DirectX::XMStoreFloat4x4(&m, X * M);
	DrawArrow(opaqueArrows, m, radius, size.x, { 0, 0, 1, 1 });

	// 原点
	float r = radius * 2.0f;
	DrawBox(opaqueBoxes, transform, { r, r, r }, { 1, 1, 0, 1 });
}

// グリッド描画
void PrimitiveRenderer::DrawGrid(int subdivisions, float scale)
{
	const float corner = 0.5f;
	const float step = 1.0f / static_cast<float>(subdivisions);

	//const DirectX::XMFLOAT4 gridColor = { 0.5f, 0.5f, 0.5f, 1.0f };
	const DirectX::XMFLOAT4 gridColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	const DirectX::XMFLOAT4 xAxisColor = { 1, 0, 0, 1 };
	const DirectX::XMFLOAT4 yAxisColor = { 0, 1, 0, 1 };
	const DirectX::XMFLOAT4 zAxisColor = { 0, 0, 1, 1 };

	const float scaling = static_cast<float>(subdivisions) * scale;
	const float radius = 0.01f;

	const DirectX::XMMATRIX rotZNeg90 = DirectX::XMMatrixRotationZ(-DirectX::XM_PIDIV2);
	const DirectX::XMMATRIX rotX90 = DirectX::XMMatrixRotationX(DirectX::XM_PIDIV2);

	// --- グリッド線 ---
// Z方向の線群 (Xを変えて並べる)
	for (int i = 0; i <= subdivisions; i++)
	{
		float offset = (-corner + i * step) * scaling;

		// 回転 → 平行移動 の順序
		DirectX::XMMATRIX transformMat =
			DirectX::XMMatrixRotationX(DirectX::XM_PIDIV2) * // X軸回転（線をZ方向に）
			DirectX::XMMatrixTranslation(offset, 0.0f, 0.0f); // X位置にずらす

		DirectX::XMFLOAT4X4 transform;
		DirectX::XMStoreFloat4x4(&transform, transformMat);

		DrawCylinder(transform, radius, scaling, gridColor);
	}

	// X方向の線群 (Zを変えて並べる)
	for (int i = 0; i <= subdivisions; i++)
	{
		float offset = (-corner + i * step) * scaling;

		DirectX::XMMATRIX transformMat =
			DirectX::XMMatrixRotationZ(-DirectX::XM_PIDIV2) * // Z軸回転（線をX方向に）
			DirectX::XMMatrixTranslation(0.0f, 0.0f, offset); // Z位置にずらす

		DirectX::XMFLOAT4X4 transform;
		DirectX::XMStoreFloat4x4(&transform, transformMat);

		DrawCylinder(transform, radius, scaling, gridColor);
	}




	// --- 座標軸 ---

	// シリンダーの中心を原点から軸の端 (scaling * 0.5) に移動させる

	// X軸 (赤)
	{
		// 1. X軸方向に中心を移動
		DirectX::XMMATRIX trans = DirectX::XMMatrixTranslation(scaling * corner, 0.0f, 0.0f);

		// 2. Z軸周り回転を適用 (Y軸シリンダーをX軸に倒す)
		DirectX::XMFLOAT4X4 transform;
		DirectX::XMStoreFloat4x4(&transform, rotZNeg90 * trans);

		DrawCylinder(transform, radius * 1.5f, scaling, xAxisColor);
	}

	// Y軸 (緑) - 回転は不要
	{
		DirectX::XMFLOAT4X4 transform;
		// Y軸方向に中心を移動 (回転は行わない)
		DirectX::XMStoreFloat4x4(&transform, DirectX::XMMatrixTranslation(0.0f, scaling * corner, 0.0f));

		DrawCylinder(transform, radius * 1.5f, scaling, yAxisColor);
	}

	// Z軸 (青)
	{
		// 1. Z軸方向に中心を移動
		DirectX::XMMATRIX trans = DirectX::XMMatrixTranslation(0.0f, 0.0f, scaling * corner);

		// 2. X軸周り回転を適用 (Y軸シリンダーをZ軸に倒す)
		DirectX::XMFLOAT4X4 transform;
		DirectX::XMStoreFloat4x4(&transform, rotX90 * trans);

		DrawCylinder(transform, radius * 1.5f, scaling, zAxisColor);
	}
}





// 球描画実行
void PrimitiveRenderer::RenderSpheres(ID3D11DeviceContext* context, std::vector<Sphere>& spheres)
{
	// 頂点バッファ設定
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, sphereBuffer.vertexBuffer.GetAddressOf(), &stride, &offset);
	context->IASetIndexBuffer(sphereBuffer.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	for (const Sphere& sphere : spheres)
	{
		// ワールドビュープロジェクション行列作成
		DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&sphere.transform);
		W.r[0] = DirectX::XMVectorScale(W.r[0], sphere.radius);
		W.r[1] = DirectX::XMVectorScale(W.r[1], sphere.radius);
		W.r[2] = DirectX::XMVectorScale(W.r[2], sphere.radius);

		// 定数バッファ更新
		CbMesh cbMesh;
		cbMesh.color = sphere.color;
		DirectX::XMStoreFloat4x4(&cbMesh.world, W);

		context->UpdateSubresource(meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);
		context->DrawIndexed(sphereBuffer.indexCount, 0, 0);
	}
	spheres.clear();
}


// 箱描画実行
void PrimitiveRenderer::RenderBoxes(ID3D11DeviceContext* context, std::vector<Box>& boxes)
{
	// 頂点バッファ設定
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, boxBuffer.vertexBuffer.GetAddressOf(), &stride, &offset);
	context->IASetIndexBuffer(boxBuffer.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	for (const Box& box : boxes)
	{
		// ワールドビュープロジェクション行列作成
		DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&box.transform);
		W.r[0] = DirectX::XMVectorScale(W.r[0], box.size.x);
		W.r[1] = DirectX::XMVectorScale(W.r[1], box.size.y);
		W.r[2] = DirectX::XMVectorScale(W.r[2], box.size.z);

		// 定数バッファ更新
		CbMesh cbMesh;
		cbMesh.color = box.color;
		DirectX::XMStoreFloat4x4(&cbMesh.world, W);

		context->UpdateSubresource(meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);
		context->DrawIndexed(boxBuffer.indexCount, 0, 0);
	}
	boxes.clear();
}

// 円柱描画実行
void PrimitiveRenderer::RenderCylinders(ID3D11DeviceContext* context, std::vector<Cylinder>& cylinders)
{
	// 頂点バッファ設定
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, cylinderBuffer.vertexBuffer.GetAddressOf(), &stride, &offset);
	context->IASetIndexBuffer(cylinderBuffer.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	for (const Cylinder& cylinder : cylinders)
	{
		// ワールドビュープロジェクション行列作成
		DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&cylinder.transform);
		W.r[0] = DirectX::XMVectorScale(W.r[0], cylinder.radius);
		W.r[1] = DirectX::XMVectorScale(W.r[1], cylinder.height);
		W.r[2] = DirectX::XMVectorScale(W.r[2], cylinder.radius);

		// 定数バッファ更新
		CbMesh cbMesh;
		cbMesh.color = cylinder.color;
		DirectX::XMStoreFloat4x4(&cbMesh.world, W);

		context->UpdateSubresource(meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);
		context->DrawIndexed(cylinderBuffer.indexCount, 0, 0);
	}
	cylinders.clear();
}

// 円錐描画実行
void PrimitiveRenderer::RenderCones(ID3D11DeviceContext* context, std::vector<Cone>& cones)
{
	// 頂点バッファ設定
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, coneBuffer.vertexBuffer.GetAddressOf(), &stride, &offset);
	context->IASetIndexBuffer(coneBuffer.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	for (const Cone& cone : cones)
	{
		// ワールドビュープロジェクション行列作成
		DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&cone.transform);
		W.r[0] = DirectX::XMVectorScale(W.r[0], cone.radius);
		W.r[1] = DirectX::XMVectorScale(W.r[1], cone.height);
		W.r[2] = DirectX::XMVectorScale(W.r[2], cone.radius);

		// 定数バッファ更新
		CbMesh cbMesh;
		cbMesh.color = cone.color;
		DirectX::XMStoreFloat4x4(&cbMesh.world, W);

		context->UpdateSubresource(meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);
		context->DrawIndexed(coneBuffer.indexCount, 0, 0);
	}
	cones.clear();
}


// カプセル描画実行
void PrimitiveRenderer::RenderCapsules(ID3D11DeviceContext* context, std::vector<Capsule>& capsules)
{
	UINT stride = sizeof(Vertex);
	UINT offset = 0;

	for (const Capsule& capsule : capsules)
	{
		// 定数バッファ更新
		CbMesh cbMesh;
		cbMesh.color = capsule.color;

		// 上半球
		{
			// 頂点バッファ設定
			context->IASetVertexBuffers(0, 1, halfSphereBuffer.vertexBuffer.GetAddressOf(), &stride, &offset);
			context->IASetIndexBuffer(halfSphereBuffer.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

			// ワールドビュープロジェクション行列作成
			DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&capsule.transform);
			W.r[3] = DirectX::XMVector3Transform(DirectX::XMVectorSet(0, capsule.height * 0.5f + capsule.radius, 0.0f, 0.0f), W);
			W.r[0] = DirectX::XMVectorScale(W.r[0], capsule.radius);
			W.r[1] = DirectX::XMVectorScale(W.r[1], capsule.radius);
			W.r[2] = DirectX::XMVectorScale(W.r[2], capsule.radius);
			DirectX::XMStoreFloat4x4(&cbMesh.world, W);

			// 定数バッファ更新
			context->UpdateSubresource(meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);
			context->DrawIndexed(halfSphereBuffer.indexCount, 0, 0);
		}
		// 円柱
		{
			// 頂点バッファ設定
			context->IASetVertexBuffers(0, 1, cylinderBuffer.vertexBuffer.GetAddressOf(), &stride, &offset);
			context->IASetIndexBuffer(cylinderBuffer.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

			// ワールドビュープロジェクション行列作成
			DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&capsule.transform);
			//W.r[3] = DirectX::XMVector3Transform(DirectX::XMVectorSet(0, 0 -capsule.height * 0.5f + capsule.radius, 0.0f, 0.0f), W);
			W.r[0] = DirectX::XMVectorScale(W.r[0], capsule.radius);
			W.r[1] = DirectX::XMVectorScale(W.r[1], capsule.height);
			W.r[2] = DirectX::XMVectorScale(W.r[2], capsule.radius);
			W.r[3] = DirectX::XMVector3Transform(DirectX::XMVectorSet(0, capsule.radius, 0.0f, 0.0f), W);
			DirectX::XMStoreFloat4x4(&cbMesh.world, W);

			// 定数バッファ更新
			context->UpdateSubresource(meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);
			context->DrawIndexed(cylinderBuffer.indexCount, 0, 0);
		}
		// 下半球
		{
			// 頂点バッファ設定
			context->IASetVertexBuffers(0, 1, halfSphereBuffer.vertexBuffer.GetAddressOf(), &stride, &offset);
			context->IASetIndexBuffer(halfSphereBuffer.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

			// ワールドビュープロジェクション行列作成
			DirectX::XMMATRIX R = DirectX::XMMatrixRotationX(DirectX::XM_PI);
			DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&capsule.transform);
			DirectX::XMVECTOR T = DirectX::XMVector3Transform(DirectX::XMVectorSet(0, -capsule.height * 0.5f + capsule.radius, 0.0f, 0.0f), W);
			W.r[0] = DirectX::XMVectorScale(W.r[0], capsule.radius);
			W.r[1] = DirectX::XMVectorScale(W.r[1], capsule.radius);
			W.r[2] = DirectX::XMVectorScale(W.r[2], capsule.radius);
			W.r[3] = DirectX::XMVectorZero();
			W = R * W;
			W.r[3] = T;
			DirectX::XMStoreFloat4x4(&cbMesh.world, W);

			// 定数バッファ更新
			context->UpdateSubresource(meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);
			context->DrawIndexed(halfSphereBuffer.indexCount, 0, 0);
		}
	}
	capsules.clear();
}

// 矢印描画実行
void PrimitiveRenderer::RenderArrows(ID3D11DeviceContext* context, std::vector<Arrow>& arrows)
{
	UINT stride = sizeof(Vertex);
	UINT offset = 0;

	for (const Arrow& arrow : arrows)
	{
		// 定数バッファ更新
		CbMesh cbMesh;
		cbMesh.color = arrow.color;

		// 円錐
		float coneHeight = arrow.radius * 6.0f;
		if (coneHeight > arrow.height)
		{
			coneHeight = arrow.height;
		}
		{
			float radius = arrow.radius * 3.0f;


			// 頂点バッファ設定
			context->IASetVertexBuffers(0, 1, coneBuffer.vertexBuffer.GetAddressOf(), &stride, &offset);
			context->IASetIndexBuffer(coneBuffer.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

			// ワールドビュープロジェクション行列作成
			DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&arrow.transform);
			W.r[3] = DirectX::XMVector3Transform(DirectX::XMVectorSet(0, arrow.height - coneHeight, 0.0f, 0.0f), W);
			W.r[0] = DirectX::XMVectorScale(W.r[0], radius);
			W.r[1] = DirectX::XMVectorScale(W.r[1], coneHeight);
			W.r[2] = DirectX::XMVectorScale(W.r[2], radius);
			DirectX::XMStoreFloat4x4(&cbMesh.world, W);

			// 定数バッファ更新
			context->UpdateSubresource(meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);
			context->DrawIndexed(coneBuffer.indexCount, 0, 0);
		}
		// 円柱
		if (arrow.boxFlg == false)
		{
			// 頂点バッファ設定
			context->IASetVertexBuffers(0, 1, cylinderBuffer.vertexBuffer.GetAddressOf(), &stride, &offset);
			context->IASetIndexBuffer(cylinderBuffer.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

			// ワールドビュープロジェクション行列作成
			DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&arrow.transform);
			W.r[3] = DirectX::XMVector3Transform(DirectX::XMVectorSet(0, (arrow.height - coneHeight) * 0.5f, 0.0f, 0.0f), W);
			W.r[0] = DirectX::XMVectorScale(W.r[0], arrow.radius);
			W.r[1] = DirectX::XMVectorScale(W.r[1], arrow.height - coneHeight);
			W.r[2] = DirectX::XMVectorScale(W.r[2], arrow.radius);
			DirectX::XMStoreFloat4x4(&cbMesh.world, W);

			// 定数バッファ更新
			context->UpdateSubresource(meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);
			context->DrawIndexed(cylinderBuffer.indexCount, 0, 0);
		}
		else
		{
			// 頂点バッファ設定
			context->IASetVertexBuffers(0, 1, boxBuffer.vertexBuffer.GetAddressOf(), &stride, &offset);
			context->IASetIndexBuffer(boxBuffer.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

			// ワールドビュープロジェクション行列作成
			DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&arrow.transform);
			W.r[3] = DirectX::XMVector3Transform(DirectX::XMVectorSet(0, (arrow.height - coneHeight) * 0.5f, 0.0f, 0.0f), W);
			W.r[0] = DirectX::XMVectorScale(W.r[0], arrow.radius);
			W.r[1] = DirectX::XMVectorScale(W.r[1], (arrow.height - coneHeight) * 0.5f);
			W.r[2] = DirectX::XMVectorScale(W.r[2], arrow.radius);
			DirectX::XMStoreFloat4x4(&cbMesh.world, W);

			// 定数バッファ更新
			context->UpdateSubresource(meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);
			context->DrawIndexed(boxBuffer.indexCount, 0, 0);
		}
	}
	arrows.clear();
}

// 球メッシュ作成
void PrimitiveRenderer::CreateSphereMesh(ID3D11Device* device, float radius, int slices, int stacks, GeometryBuffer& buffer)
{
	int vertex_count = slices * (stacks - 1) + 2;
	int index_count = (slices - 1) * 3
		+ 3
		+ (stacks - 2) * (slices - 1) * 6
		+ (stacks - 2) * 6
		+ (slices - 1) * 3
		+ 3;
	std::unique_ptr<Vertex[]> vertices = std::make_unique<Vertex[]>(vertex_count);
	std::unique_ptr<UINT[]> indices = std::make_unique<UINT[]>(index_count);

	// caches sin cos.
	std::unique_ptr<float[]> cos_phi = std::make_unique<float[]>(stacks);
	std::unique_ptr<float[]> sin_phi = std::make_unique<float[]>(stacks);
	float phi_step = DirectX::XM_PI / stacks;

	std::unique_ptr<float[]> cos_theta = std::make_unique<float[]>(slices);
	std::unique_ptr<float[]> sin_theta = std::make_unique<float[]>(slices);
	float theta_step = DirectX::XM_2PI / slices;

	float phi = 0;
	for (int s = 0; s < stacks; s++, phi += phi_step)
	{
		sin_phi[s] = sinf(phi);
		cos_phi[s] = cosf(phi);
	}

	float theta = 0;
	for (int s = 0; s < slices; s++, theta += theta_step)
	{
		sin_theta[s] = sinf(theta);
		cos_theta[s] = cosf(theta);
	}

	Vertex* vertex = vertices.get();

	// north pole. 
	vertex->position.x = 0;
	vertex->position.y = radius;
	vertex->position.z = 0;

	DirectX::XMVECTOR Position = DirectX::XMLoadFloat3(&vertex->position);
	DirectX::XMVECTOR Normal = DirectX::XMVector3Normalize(Position);
	DirectX::XMStoreFloat3(&vertex->normal, Normal);

	vertex++;

	for (int s = 1; s < stacks; s++)
	{
		float y = radius * cos_phi[s];
		float r = radius * sin_phi[s];
		for (int l = 0; l < slices; l++)
		{
			vertex->position.x = r * sin_theta[l];
			vertex->position.y = y;
			vertex->position.z = r * cos_theta[l];

			Position = DirectX::XMLoadFloat3(&vertex->position);
			Normal = DirectX::XMVector3Normalize(Position);
			DirectX::XMStoreFloat3(&vertex->normal, Normal);

			vertex++;
		}
	}

	// south_pole
	vertex->position.x = 0;
	vertex->position.y = -radius;
	vertex->position.z = 0;

	Position = DirectX::XMLoadFloat3(&vertex->position);
	Normal = DirectX::XMVector3Normalize(Position);
	DirectX::XMStoreFloat3(&vertex->normal, Normal);

	// 2l + (s-2) * 2l
	// 2l * ( 1 + s-2)
	// 2l * ( s- 1)

	UINT* index = indices.get();

	// create index of north pole cap.
	for (int l = 1; l < slices; l++)
	{
		index[0] = l + 1;
		index[1] = 0;
		index[2] = l;

		index += 3;
	}
	index[0] = 1;
	index[1] = 0;
	index[2] = slices;

	index += 3;

	for (int s = 0; s < (stacks - 2); s++)
	{
		int l = 1;
		for (; l < slices; l++)
		{
			index[0] = ((s + 1) * slices + l + 1);	// bottom right.
			index[1] = (s * slices + l + 1);		// top right.
			index[2] = (s * slices + l);			// top left.

			index[3] = ((s + 1) * slices + l);		// bottom left.
			index[4] = ((s + 1) * slices + l + 1);	// bottom right.
			index[5] = (s * slices + l);			// top left.

			index += 6;
		}

		index[0] = ((s + 1) * slices + 1);			// bottom right.
		index[1] = (s * slices + 1);				// top right.
		index[2] = (s * slices + slices);			// top left.


		index[3] = ((s + 1) * slices + slices);		// bottom left.
		index[4] = ((s + 1) * slices + 1);			// bottom right.
		index[5] = (s * slices + slices);			// top left.

		index += 6;
	}

	// create index for south pole cap.
	int base_index = slices * (stacks - 2);
	int last_index = vertex_count - 1;
	for (int l = 1; l < slices; l++)
	{
		index[0] = (base_index + l);
		index[1] = (last_index);
		index[2] = (base_index + l + 1);

		index += 3;
	}
	index[0] = (base_index + slices);
	index[1] = last_index;
	index[2] = base_index + 1;

	// 頂点バッファ
	{
		D3D11_BUFFER_DESC desc = {};
		D3D11_SUBRESOURCE_DATA subresourceData = {};

		desc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * vertex_count);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		subresourceData.pSysMem = vertices.get();
		subresourceData.SysMemPitch = 0;
		subresourceData.SysMemSlicePitch = 0;

		HRESULT hr = device->CreateBuffer(&desc, &subresourceData, buffer.vertexBuffer.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}
	// インデックスバッファ
	{
		D3D11_BUFFER_DESC desc = {};
		D3D11_SUBRESOURCE_DATA subresourceData = {};

		desc.ByteWidth = static_cast<UINT>(sizeof(UINT) * index_count);
		//desc.Usage = D3D11_USAGE_DEFAULT;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		subresourceData.pSysMem = indices.get();
		subresourceData.SysMemPitch = 0;
		subresourceData.SysMemSlicePitch = 0;
		HRESULT hr = device->CreateBuffer(&desc, &subresourceData, buffer.indexBuffer.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	buffer.indexCount = index_count;
}

// 半球メッシュ作成
void PrimitiveRenderer::CreateHalfSphereMesh(ID3D11Device* device, float radius, int slices, int stacks, GeometryBuffer& buffer)
{
	int vertex_count = slices * (stacks - 1) + 1;
	int index_count = (slices - 1) * 3
		+ 3
		+ (stacks - 2) * (slices - 1) * 6
		+ (stacks - 2) * 6;

	std::unique_ptr<Vertex[]> vertices = std::make_unique<Vertex[]>(vertex_count);
	std::unique_ptr<UINT[]> indices = std::make_unique<UINT[]>(index_count);

	// caches sin cos.
	std::unique_ptr<float[]> cos_phi = std::make_unique<float[]>(stacks);
	std::unique_ptr<float[]> sin_phi = std::make_unique<float[]>(stacks);
	float phi_step = DirectX::XM_PI / (stacks - 1) * 0.5f;

	std::unique_ptr<float[]> cos_theta = std::make_unique<float[]>(slices);
	std::unique_ptr<float[]> sin_theta = std::make_unique<float[]>(slices);
	float theta_step = DirectX::XM_2PI / slices;

	float phi = 0;
	for (int s = 0; s < stacks; s++, phi += phi_step)
	{
		sin_phi[s] = sinf(phi);
		cos_phi[s] = cosf(phi);
	}

	float theta = 0;
	for (int s = 0; s < slices; s++, theta += theta_step)
	{
		sin_theta[s] = sinf(theta);
		cos_theta[s] = cosf(theta);
	}

	Vertex* vertex = vertices.get();

	// north pole. 
	vertex->position.x = 0;
	vertex->position.y = radius;
	vertex->position.z = 0;

	DirectX::XMVECTOR Position = DirectX::XMLoadFloat3(&vertex->position);
	DirectX::XMVECTOR Normal = DirectX::XMVector3Normalize(Position);
	DirectX::XMStoreFloat3(&vertex->normal, Normal);

	vertex++;

	for (int s = 1; s < stacks; s++)
	{
		float y = radius * cos_phi[s];
		float r = radius * sin_phi[s];
		for (int l = 0; l < slices; l++)
		{
			vertex->position.x = r * sin_theta[l];
			vertex->position.y = y;
			vertex->position.z = r * cos_theta[l];

			Position = DirectX::XMLoadFloat3(&vertex->position);
			Normal = DirectX::XMVector3Normalize(Position);
			DirectX::XMStoreFloat3(&vertex->normal, Normal);

			vertex++;
		}
	}

	// 2l + (s-2) * 2l
	// 2l * ( 1 + s-2)
	// 2l * ( s- 1)

	UINT* index = indices.get();

	// create index of north pole cap.
	for (int l = 1; l < slices; l++)
	{
		index[0] = l + 1;
		index[1] = 0;
		index[2] = l;

		index += 3;
	}
	index[0] = 1;
	index[1] = 0;
	index[2] = slices;

	index += 3;

	for (int s = 0; s < (stacks - 2); s++)
	{
		int l = 1;
		for (; l < slices; l++)
		{
			index[0] = ((s + 1) * slices + l + 1);	// bottom right.
			index[1] = (s * slices + l + 1);		// top right.
			index[2] = (s * slices + l);			// top left.

			index[3] = ((s + 1) * slices + l);		// bottom left.
			index[4] = ((s + 1) * slices + l + 1);	// bottom right.
			index[5] = (s * slices + l);			// top left.

			index += 6;
		}

		index[0] = ((s + 1) * slices + 1);			// bottom right.
		index[1] = (s * slices + 1);				// top right.
		index[2] = (s * slices + slices);			// top left.


		index[3] = ((s + 1) * slices + slices);		// bottom left.
		index[4] = ((s + 1) * slices + 1);			// bottom right.
		index[5] = (s * slices + slices);			// top left.

		index += 6;
	}

	// 頂点バッファ
	{
		D3D11_BUFFER_DESC desc = {};
		D3D11_SUBRESOURCE_DATA subresourceData = {};

		desc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * vertex_count);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		subresourceData.pSysMem = vertices.get();
		subresourceData.SysMemPitch = 0;
		subresourceData.SysMemSlicePitch = 0;

		HRESULT hr = device->CreateBuffer(&desc, &subresourceData, buffer.vertexBuffer.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}
	// インデックスバッファ
	{
		D3D11_BUFFER_DESC desc = {};
		D3D11_SUBRESOURCE_DATA subresourceData = {};

		desc.ByteWidth = static_cast<UINT>(sizeof(UINT) * index_count);
		//desc.Usage = D3D11_USAGE_DEFAULT;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		subresourceData.pSysMem = indices.get();
		subresourceData.SysMemPitch = 0;
		subresourceData.SysMemSlicePitch = 0;
		HRESULT hr = device->CreateBuffer(&desc, &subresourceData, buffer.indexBuffer.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	buffer.indexCount = index_count;
}

// 箱メッシュ作成
void PrimitiveRenderer::CreateBoxMesh(ID3D11Device* device, float radius, float height, float depth, GeometryBuffer& buffer)
{
	HRESULT hr = S_OK;

	// 頂点データの定義
	//     ___________
	//   ／|        ／|
	// ／__|______／  |
	// |   |_____|____|
	// |  ／     |  ／
	// |／_______|／
	//            
	// 頂点データに法線データを定義する
	Vertex vertices[] = {
		// 正面
		{ DirectX::XMFLOAT3(-radius, +height, -depth), DirectX::XMFLOAT3(0, 0, -1) },
		{ DirectX::XMFLOAT3(+radius, +height, -depth), DirectX::XMFLOAT3(0, 0, -1) },
		{ DirectX::XMFLOAT3(-radius, -height, -depth), DirectX::XMFLOAT3(0, 0, -1) },
		{ DirectX::XMFLOAT3(+radius, -height, -depth), DirectX::XMFLOAT3(0, 0, -1) },
		// 背面
		{ DirectX::XMFLOAT3(-radius, +height, +depth), DirectX::XMFLOAT3(0, 0, 1) },
		{ DirectX::XMFLOAT3(+radius, +height, +depth), DirectX::XMFLOAT3(0, 0, 1) },
		{ DirectX::XMFLOAT3(-radius, -height, +depth), DirectX::XMFLOAT3(0, 0, 1) },
		{ DirectX::XMFLOAT3(+radius, -height, +depth), DirectX::XMFLOAT3(0, 0, 1) },
		// 右面
		{ DirectX::XMFLOAT3(+radius, +height, -depth), DirectX::XMFLOAT3(1, 0, 0) },
		{ DirectX::XMFLOAT3(+radius, +height, +depth), DirectX::XMFLOAT3(1, 0, 0) },
		{ DirectX::XMFLOAT3(+radius, -height, -depth), DirectX::XMFLOAT3(1, 0, 0) },
		{ DirectX::XMFLOAT3(+radius, -height, +depth), DirectX::XMFLOAT3(1, 0, 0) },
		// 左面
		{ DirectX::XMFLOAT3(-radius, +height, -depth), DirectX::XMFLOAT3(-1, 0, 0) },
		{ DirectX::XMFLOAT3(-radius, +height, +depth), DirectX::XMFLOAT3(-1, 0, 0) },
		{ DirectX::XMFLOAT3(-radius, -height, -depth), DirectX::XMFLOAT3(-1, 0, 0) },
		{ DirectX::XMFLOAT3(-radius, -height, +depth), DirectX::XMFLOAT3(-1, 0, 0) },
		// 上面
		{ DirectX::XMFLOAT3(-radius, +height, +depth), DirectX::XMFLOAT3(0, 1, 0) },
		{ DirectX::XMFLOAT3(+radius, +height, +depth), DirectX::XMFLOAT3(0, 1, 0) },
		{ DirectX::XMFLOAT3(-radius, +height, -depth), DirectX::XMFLOAT3(0, 1, 0) },
		{ DirectX::XMFLOAT3(+radius, +height, -depth), DirectX::XMFLOAT3(0, 1, 0) },
		// 下面
		{ DirectX::XMFLOAT3(-radius, -height, +depth), DirectX::XMFLOAT3(0, -1, 0) },
		{ DirectX::XMFLOAT3(+radius, -height, +depth), DirectX::XMFLOAT3(0, -1, 0) },
		{ DirectX::XMFLOAT3(-radius, -height, -depth), DirectX::XMFLOAT3(0, -1, 0) },
		{ DirectX::XMFLOAT3(+radius, -height, -depth), DirectX::XMFLOAT3(0, -1, 0) },
	};

	// インデックスデータ
	UINT indices[] = {
		// 正面
		0, 1, 2,
		2, 1, 3,
		// 背面
		5, 4, 7,
		7, 4, 6,
		// 右面
		8, 9, 10,
		10, 9, 11,
		// 左面
		13, 12, 15,
		15, 12, 14,
		// 上面
		16, 17, 18,
		18, 17, 19,
		// 下面
		21, 20, 23,
		23, 20, 22,
	};

	// 頂点バッファ
	{
		D3D11_BUFFER_DESC desc = {};
		D3D11_SUBRESOURCE_DATA subresourceData = {};

		desc.ByteWidth = static_cast<UINT>(sizeof(vertices));
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		subresourceData.pSysMem = vertices;
		subresourceData.SysMemPitch = 0;
		subresourceData.SysMemSlicePitch = 0;

		HRESULT hr = device->CreateBuffer(&desc, &subresourceData, buffer.vertexBuffer.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}
	// インデックスバッファ
	{
		D3D11_BUFFER_DESC desc = {};
		D3D11_SUBRESOURCE_DATA subresourceData = {};

		desc.ByteWidth = static_cast<UINT>(sizeof(indices));
		//desc.Usage = D3D11_USAGE_DEFAULT;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		subresourceData.pSysMem = indices;
		subresourceData.SysMemPitch = 0;
		subresourceData.SysMemSlicePitch = 0;
		HRESULT hr = device->CreateBuffer(&desc, &subresourceData, buffer.indexBuffer.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	buffer.indexCount = sizeof(indices) / sizeof(indices[0]);
}

// 円柱メッシュ作成
void PrimitiveRenderer::CreateCylinderMesh(ID3D11Device* device, float radius1, float radius2, float start, float height, int slices, int stacks, GeometryBuffer& buffer)
{
	float stack_height = height / stacks;

	// Amount to increment radius as we move up each stack level from bottom to top.
	float radius_step = (radius2 - radius1) / stacks;

	int num_rings = stacks + 1;

	int vertex_count = num_rings * (slices + 1);
	int index_count = stacks * slices * 6;
	if (radius1 > 0)
	{
		vertex_count += slices + 1 + 1;
		index_count += slices * 3;
	}
	if (radius2 > 0)
	{
		vertex_count += slices + 1 + 1;
		index_count += slices * 6;
	}

	std::unique_ptr<Vertex[]> vertices = std::make_unique<Vertex[]>(vertex_count);
	std::unique_ptr<UINT[]> indices = std::make_unique<UINT[]>(index_count);

	Vertex* vertex = vertices.get();

	int vcount = 0;

	// Compute vertices for each stack ring.
	for (int i = 0; i < num_rings; ++i)
	{
		float y = start + i * stack_height;
		float r = radius1 + i * radius_step;

		// Height and radius of next ring up.
		float y_next = start + (i + 1) * stack_height;
		float r_next = radius1 + (i + 1) * radius_step;

		// vertices of ring
		float d_theta = DirectX::XM_2PI / slices;
		for (int j = 0; j <= slices; ++j)
		{
			float c = cosf(j * d_theta);
			float s = sinf(j * d_theta);

			// Partial derivative in theta direction to get tangent vector (this is a unit vector).
			DirectX::XMFLOAT3 t = { -s, 0.0f, c };

			// Compute tangent vector down the slope of the cone (if the top/bottom 
			// radii differ then we get a cone and not a true cylinder).
			DirectX::XMFLOAT3 p = { r * c, y, r * s };
			DirectX::XMFLOAT3 p_next = { r_next * c, y_next, r_next * s };
			DirectX::XMFLOAT3 n;
			DirectX::XMVECTOR P = DirectX::XMLoadFloat3(&p);
			DirectX::XMVECTOR P_Next = DirectX::XMLoadFloat3(&p_next);
			DirectX::XMVECTOR B = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(P, P_Next));
			DirectX::XMVECTOR T = DirectX::XMLoadFloat3(&t);
			DirectX::XMVECTOR N = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(T, B));
			DirectX::XMStoreFloat3(&n, N);

			// flip to right hand
			p.z *= -1;
			n.z *= -1;

			vertex->position = p;
			vertex->normal = n;

			vertex++;
			vcount++;
		}
	}

	int num_ring_vertices = slices + 1;

	UINT* index = indices.get();

	// Compute indices for each stack.
	for (int i = 0; i < stacks; ++i)
	{
		for (int j = 0; j < slices; ++j)
		{
			index[0] = (i * num_ring_vertices + j);
			index[1] = ((i + 1) * num_ring_vertices + j + 1);
			index[2] = ((i + 1) * num_ring_vertices + j);

			index[3] = (i * num_ring_vertices + j);
			index[4] = (i * num_ring_vertices + j + 1);
			index[5] = ((i + 1) * num_ring_vertices + j + 1);

			index += 6;
		}
	}

	// build bottom cap.
	if (radius1 > 0)
	{
		int base_index = vcount;
		// Duplicate cap vertices because the texture coordinates and normals differ.
		float y = start;

		// vertices of ring
		float d_theta = DirectX::XM_2PI / slices;
		for (int i = 0; i <= slices; ++i)
		{
			float x = radius1 * cosf(i * d_theta);
			float z = radius1 * sinf(i * d_theta);

			vertex->position.x = x;
			vertex->position.y = y;
			vertex->position.z = -z;

			vertex->normal.x = 0.0f;
			vertex->normal.y = -1.0f;
			vertex->normal.z = 0.0f;

			vertex++;
			vcount++;
		}


		// cap center vertex
		vertex->position.x = 0;
		vertex->position.y = y;
		vertex->position.z = 0;

		vertex->normal.x = 0.0f;
		vertex->normal.y = -1.0f;
		vertex->normal.z = 0.0f;

		vertex++;
		vcount++;

		// index of center vertex
		int center_index = vcount - 1;
		for (int i = 0; i < slices; ++i)
		{
			index[0] = center_index;
			index[1] = base_index + i + 1;
			index[2] = base_index + i;
			index += 3;
		}
	}

	// build top cap.
	if (radius2 > 0)
	{
		int base_index = vcount;

		// Duplicate cap vertices because the texture coordinates and normals differ.
		float y = start + height;

		// vertices of ring
		float d_theta = DirectX::XM_2PI / slices;
		for (int i = 0; i <= slices; ++i)
		{
			float x = radius2 * cosf(i * d_theta);
			float z = radius2 * sinf(i * d_theta);

			vertex->position.x = x;
			vertex->position.y = y;
			vertex->position.z = -z;

			vertex->normal.x = 0.0f;
			vertex->normal.y = 1.0f;
			vertex->normal.z = 0.0f;

			vertex++;
			vcount++;
		}

		// pos, norm, tex1 for cap center vertex
		vertex->position.x = 0;
		vertex->position.y = y;
		vertex->position.z = 0;

		vertex->normal.x = 0.0f;
		vertex->normal.y = 1.0f;
		vertex->normal.z = 0.0f;

		vertex++;
		vcount++;

		// index of center vertex
		int center_index = vcount - 1;
		for (int i = 0; i < slices; ++i)
		{
			index[0] = center_index;
			index[1] = base_index + i + 1;
			index[2] = base_index + i;

			index[3] = center_index;
			index[4] = base_index + i;
			index[5] = base_index + i + 1;

			index += 6;
		}
	}

	// 頂点バッファ
	{
		D3D11_BUFFER_DESC desc = {};
		D3D11_SUBRESOURCE_DATA subresourceData = {};

		desc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * vertex_count);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		subresourceData.pSysMem = vertices.get();
		subresourceData.SysMemPitch = 0;
		subresourceData.SysMemSlicePitch = 0;

		HRESULT hr = device->CreateBuffer(&desc, &subresourceData, buffer.vertexBuffer.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}
	// インデックスバッファ
	{
		D3D11_BUFFER_DESC desc = {};
		D3D11_SUBRESOURCE_DATA subresourceData = {};

		desc.ByteWidth = static_cast<UINT>(sizeof(UINT) * index_count);
		//desc.Usage = D3D11_USAGE_DEFAULT;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		subresourceData.pSysMem = indices.get();
		subresourceData.SysMemPitch = 0;
		subresourceData.SysMemSlicePitch = 0;
		HRESULT hr = device->CreateBuffer(&desc, &subresourceData, buffer.indexBuffer.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	buffer.indexCount = index_count;
}


// 球描画登録
void PrimitiveRenderer::DrawSphere(std::vector<Sphere>& spheres, const DirectX::XMFLOAT4X4& transform, float radius, const DirectX::XMFLOAT4& color)
{
	Sphere sphere;
	sphere.transform = transform;
	sphere.radius = radius;
	sphere.color = color;
	spheres.emplace_back(sphere);
}

// 箱描画登録
void PrimitiveRenderer::DrawBox(std::vector<Box>& boxes, const DirectX::XMFLOAT4X4& transform, const DirectX::XMFLOAT3& size, const DirectX::XMFLOAT4& color)
{
	Box box;
	box.transform = transform;
	box.size = size;
	box.color = color;
	boxes.emplace_back(box);
}

// 円柱描画登録
void PrimitiveRenderer::DrawCylinder(std::vector<Cylinder>& cylinders, const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color)
{
	Cylinder cylinder;
	cylinder.transform = transform;
	cylinder.radius = radius;
	cylinder.height = height;
	cylinder.color = color;
	cylinders.emplace_back(cylinder);
}

// カプセル描画登録
void PrimitiveRenderer::DrawCapsule(std::vector<Capsule>& capsules, const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color)
{
	Capsule capsule;
	capsule.transform = transform;
	capsule.radius = radius;
	capsule.height = height;
	capsule.color = color;
	capsules.emplace_back(capsule);
}

// 円柱描画登録
void PrimitiveRenderer::DrawCone(std::vector<Cone>& cones, const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color)
{
	Cone cone;
	cone.transform = transform;
	cone.radius = radius;
	cone.height = height;
	cone.color = color;
	cones.emplace_back(cone);
}

// 矢印描画登録
void PrimitiveRenderer::DrawArrow(std::vector<Arrow>& arrows, const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color, bool boxFlag)
{
	Arrow arrow;
	arrow.transform = transform;
	arrow.radius = radius;
	arrow.height = height;
	arrow.color = color;
	arrow.boxFlg = boxFlag;
	arrows.emplace_back(arrow);
}


