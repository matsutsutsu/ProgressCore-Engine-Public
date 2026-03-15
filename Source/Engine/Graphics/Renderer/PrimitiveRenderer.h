#pragma once

#include <vector>
#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>
// ★追加: カリング計算用ヘッダ
#include <DirectXCollision.h>

class PrimitiveRenderer
{
public:
	PrimitiveRenderer(ID3D11Device* device);
	~PrimitiveRenderer() {}

public:
	// 描画実行
	void Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& projection);

	// ★追加: カリングの有効/無効切り替え（デバッグで全部見たい時などに使う）
        void SetCullingEnabled(bool enable) { m_cullingEnabled = enable; }

	// 球描画
	void DrawSphere(const DirectX::XMFLOAT4X4& transform, float radius, const DirectX::XMFLOAT4& color);
	void DrawWireframeSphere(const DirectX::XMFLOAT4X4& transform, float radius, const DirectX::XMFLOAT4& color);

	// 箱描画
	void DrawBox(const DirectX::XMFLOAT4X4& transform, const DirectX::XMFLOAT3& size, const DirectX::XMFLOAT4& color);
	void DrawWireframeBox(const DirectX::XMFLOAT4X4& transform, const DirectX::XMFLOAT3& size, const DirectX::XMFLOAT4& color);

	// 円柱描画
	void DrawCylinder(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color);
	void DrawWireframeCylinder(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color);

	// カプセル描画
	void DrawCapsule(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color);
	void DrawWireframeCapsule(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color);

	// 円柱描画
	void DrawCone(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color);
	void DrawWireframeCone(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color);

	// 矢印描画
	void DrawArrow(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color, bool boxFlag = false);
	void DrawWireframeArrow(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color, bool boxFlag = false);

	// 軸描画
	void DrawAxis(const DirectX::XMFLOAT4X4& transform, const DirectX::XMFLOAT3& size, float radius);

	// グリッド描画
	void DrawGrid(int subdivisions, float scale);

private:
        // ★追加: カリング判定用ヘルパー
        bool IsVisible(const DirectX::BoundingSphere &sphere);
        bool IsVisible(const DirectX::BoundingOrientedBox &box);

        // ★追加: 視錐台データ
        DirectX::BoundingFrustum m_frustum;
        bool                     m_cullingEnabled = true;

	struct GeometryBuffer
	{
		Microsoft::WRL::ComPtr<ID3D11Buffer>	vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer>	indexBuffer;
		UINT									indexCount;
	};

	struct CbScene
	{
		DirectX::XMFLOAT4X4	viewProjection;
		DirectX::XMFLOAT4	lightDirection;
	};

	struct CbMesh
	{
		DirectX::XMFLOAT4X4	world;
		DirectX::XMFLOAT4	color;
	};

	struct Vertex
	{
		DirectX::XMFLOAT3	position;
		DirectX::XMFLOAT3	normal;
	};

	struct Primitive
	{
		DirectX::XMFLOAT4X4	transform;
	};

	struct Sphere : public Primitive
	{
		DirectX::XMFLOAT4	color;
		float				radius;
	};

	struct Box : public Primitive
	{
		DirectX::XMFLOAT4	color;
		DirectX::XMFLOAT3	size;
	};

	struct Cylinder : public Primitive
	{
		DirectX::XMFLOAT4	color;
		float				radius;
		float				height;
	};

	struct Capsule : public Primitive
	{
		DirectX::XMFLOAT4	color;
		float				radius;
		float				height;
	};

	struct Cone : public Primitive
	{
		DirectX::XMFLOAT4	color;
		float				radius;
		float				height;
	};

	struct Arrow : public Primitive
	{
		DirectX::XMFLOAT4	color;
		float				radius;
		float				height;
		bool				boxFlg;
	};

	// 球描画実行
	void RenderSpheres(ID3D11DeviceContext* context, std::vector<Sphere>& spheres);

	// 箱描画実行
	void RenderBoxes(ID3D11DeviceContext* context, std::vector<Box>& boxes);

	// 円柱描画実行
	void RenderCylinders(ID3D11DeviceContext* context, std::vector<Cylinder>& cylinders);

	// 円錐描画実行
	void RenderCones(ID3D11DeviceContext* context, std::vector<Cone>& cones);

	// カプセル描画実行
	void RenderCapsules(ID3D11DeviceContext* context, std::vector<Capsule>& capsules);

	// 矢印描画実行
	void RenderArrows(ID3D11DeviceContext* context, std::vector<Arrow>& arrows);

	// 球メッシュ作成
	static void CreateSphereMesh(ID3D11Device* device, float radius, int slices, int stacks, GeometryBuffer& buffer);

	// 半球メッシュ作成
	static void CreateHalfSphereMesh(ID3D11Device* device, float radius, int slices, int stacks, GeometryBuffer& buffer);

	// 箱メッシュ作成
	static void CreateBoxMesh(ID3D11Device* device, float radius, float height, float depth, GeometryBuffer& buffer);

	// 円柱メッシュ作成
	static void CreateCylinderMesh(ID3D11Device* device, float radius1, float radius2, float start, float height, int slices, int stacks, GeometryBuffer& buffer);

	// 球描画登録
	static void DrawSphere(std::vector<Sphere>& spheres, const DirectX::XMFLOAT4X4& transform, float radius, const DirectX::XMFLOAT4& color);

	// 箱描画登録
	static void DrawBox(std::vector<Box>& boxes, const DirectX::XMFLOAT4X4& transform, const DirectX::XMFLOAT3& size, const DirectX::XMFLOAT4& color);

	// 円柱描画登録
	static void DrawCylinder(std::vector<Cylinder>& cylinders, const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color);

	// カプセル描画登録
	static void DrawCapsule(std::vector<Capsule>& capsules, const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color);

	// 円柱描画登録
	static void DrawCone(std::vector<Cone>& cones, const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color);

	// 矢印描画登録
	static void DrawArrow(std::vector<Arrow>& arrows, const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color, bool boxFlag = false);

private:
	Microsoft::WRL::ComPtr<ID3D11Buffer>			sceneConstantBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer>			meshConstantBuffer;

	Microsoft::WRL::ComPtr<ID3D11VertexShader>		vertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>		pixelShader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout>		inputLayout;

	Microsoft::WRL::ComPtr<ID3D11BlendState>		opaqueBlendState;
	Microsoft::WRL::ComPtr<ID3D11BlendState>		alphaBlendState;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState>	rasterizerState;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState>	wireframeRasterizerState;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState>	depthStencilState;





	GeometryBuffer	sphereBuffer;
	GeometryBuffer	halfSphereBuffer;
	GeometryBuffer	boxBuffer;
	GeometryBuffer	cylinderBuffer;
	GeometryBuffer	coneBuffer;


	std::vector<Sphere>			opaqueSpheres;
	std::vector<Cylinder>		opaqueCylinders;
	std::vector<Capsule>		opaqueCapsules;
	std::vector<Cone>			opaqueCones;
	std::vector<Box>			opaqueBoxes;
	std::vector<Arrow>			opaqueArrows;

	std::vector<Sphere>			alphaSpheres;
	std::vector<Cylinder>		alphaCylinders;
	std::vector<Capsule>		alphaCapsules;
	std::vector<Cone>			alphaCones;
	std::vector<Box>			alphaBoxes;
	std::vector<Arrow>			alphaArrows;

	std::vector<Sphere>			wireframeSpheres;
	std::vector<Cylinder>		wireframeCylinders;
	std::vector<Capsule>		wireframeCapsules;
	std::vector<Cone>			wireframeCones;
	std::vector<Box>			wireframeBoxes;
	std::vector<Arrow>			wireframeArrows;
};
