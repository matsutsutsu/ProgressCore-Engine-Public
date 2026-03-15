#pragma once

#include <vector>
#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <DirectXCollision.h> 

class ShapeRenderer
{
public:
	ShapeRenderer(ID3D11Device* device);
	~ShapeRenderer() {}

	void SetCullingEnabled(bool enable) { m_cullingEnabled = enable; }

	// 箱描画
	void DrawBox(
		const DirectX::XMFLOAT3& position,
		const DirectX::XMFLOAT3& angle,
		const DirectX::XMFLOAT3& size,
		const DirectX::XMFLOAT4& color);

	// 球描画
	void DrawSphere(
		const DirectX::XMFLOAT3& position,
		float radius,
		const DirectX::XMFLOAT4& color);

	// カプセル描画
	void DrawCapsule(
		const DirectX::XMFLOAT4X4& transform,
		float radius,
		float height,
		const DirectX::XMFLOAT4& color);

	// ワイヤーフレーム円柱 (AIの範囲表示などに便利)
    void DrawWireframeCylinder(
        const DirectX::XMFLOAT3& position, // 中心の位置
        float radius,                      // 半径
        float height,                      // 高さ
        const DirectX::XMFLOAT4& color);   // 色

	// 円（ワイヤーフレーム）の描画
    // rotation はクォータニオン (XMFLOAT4) で指定します
    void DrawWireframeCircle(const DirectX::XMFLOAT3 &center,
        float                                         radius,
        const DirectX::XMFLOAT4                      &color,
        const DirectX::XMFLOAT4                      &rotation);

    // 矢印 (向きやベクトルの表示に便利)
    void DrawArrow(
        const DirectX::XMFLOAT3& start,    // 始点
        const DirectX::XMFLOAT3& end,      // 終点
        float headSize,                    // 鏃（やじり）の大きさ
        const DirectX::XMFLOAT4& color);   // 色

	// 骨描画
	void DrawBone(
		const DirectX::XMFLOAT4X4& transform,
		float length,
		const DirectX::XMFLOAT4& color);

	// 描画実行
	void Render(
		ID3D11DeviceContext* dc,
		const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& projection);

	// 線描画 (始点と終点を指定)
	void DrawLine(
		const DirectX::XMFLOAT3& p1,
		const DirectX::XMFLOAT3& p2,
		const DirectX::XMFLOAT4& color);

	// レイ描画 (始点と方向と長さを指定)
	void DrawRay(
		const DirectX::XMFLOAT3& origin,
		const DirectX::XMFLOAT3& direction,
		float length,
		const DirectX::XMFLOAT4& color);

	// グリッド描画 (地面の確認用)
	void DrawGrid(
		float spacing,          // 格子の間隔
		int subdivisions,       // 分割数
		const DirectX::XMFLOAT4& color);

	// 座標軸描画 (Transformの向き確認用: 赤X, 緑Y, 青Z)
	void DrawAxis(
		const DirectX::XMFLOAT4X4& transform,
		float length);

private:
        // ★追加
        DirectX::BoundingFrustum m_frustum;
        bool                     m_cullingEnabled = true;

        bool IsVisible(const DirectX::BoundingSphere &sphere);
        bool IsVisible(const DirectX::BoundingOrientedBox &box);

	struct Mesh
	{
		Microsoft::WRL::ComPtr<ID3D11Buffer>	vertexBuffer;
		UINT									vertexCount;
	};

	struct Instance
	{
		Mesh* mesh;
		DirectX::XMFLOAT4X4		worldTransform;
		DirectX::XMFLOAT4		color;
	};

	struct CbMesh
	{
		DirectX::XMFLOAT4X4		worldViewProjection;
		DirectX::XMFLOAT4		color;
	};

	// メッシュ生成
	void CreateMesh(ID3D11Device* device, const std::vector<DirectX::XMFLOAT3>& vertices, Mesh& mesh);

	// 箱メッシュ作成
	void CreateBoxMesh(ID3D11Device* device, float width, float height, float depth);

	// 球メッシュ作成
	void CreateSphereMesh(ID3D11Device* device, float radius, int subdivisions);

	// 半球メッシュ作成
	void CreateHalfSphereMesh(ID3D11Device* device, float radius, int subdivisions);

	// 円柱
	void CreateCylinderMesh(ID3D11Device* device, float radius1, float radius2, float start, float height, int subdivisions);

	// 骨メッシュ作成
	void CreateBoneMesh(ID3D11Device* device, float length);

	// 線分メッシュ作成
	void CreateLineMesh(ID3D11Device* device);

private:
	Mesh										boxMesh;
	Mesh										sphereMesh;
	Mesh										halfSphereMesh;
	Mesh										cylinderMesh;
	Mesh										boneMesh;
	Mesh										lineMesh;
	std::vector<Instance>						instances;
	Microsoft::WRL::ComPtr<ID3D11VertexShader>	vertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>	pixelShader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout>	inputLayout;
	Microsoft::WRL::ComPtr<ID3D11Buffer>		constantBuffer;
};
