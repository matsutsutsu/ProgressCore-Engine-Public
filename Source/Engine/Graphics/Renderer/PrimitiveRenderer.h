#pragma once
// ===========================================================================
// 概要: デバッグ用の塗り・ワイヤーフレームの図形（球・箱・円柱・カプセル・円錐）を描くクラス。
//
// 責務:
//   - Draw* で図形をインスタンスとして積む（別スレッドから呼んでよい）
//   - Render で、図形と塗り方の組ごとにインスタンシングでまとめて描き、積んだものを空にする
//
// 備考: 大きさ 1 の図形を用意しておき、大きさ・向き・位置は呼び出し側が行列に入れて渡す。
//       スレッドごとの配列に積む仕組みは ShapeRenderer と同じ。
//       IsVisible と m_frustum は用意してあるが、今の Draw* はカリングしていない。
// ===========================================================================
#include <vector>
#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <mutex>
#include <memory>
#include <atomic>
#include "Engine/Graphics/Renderer/InstanceBuffer.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/RHI/RenderContext.h"

// 頂点バッファとインデックスバッファの組
struct GeometryBuffer
{
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
    UINT                                 indexCount = 0;
};

// スレッド1つ分の、図形と塗り方ごとのインスタンスの配列
struct PrimitiveDrawList
{
    std::vector<InstanceData> opaqueSpheres;
    std::vector<InstanceData> wireframeSpheres;
    std::vector<InstanceData> opaqueBoxes;
    std::vector<InstanceData> wireframeBoxes;
    std::vector<InstanceData> opaqueCylinders;
    std::vector<InstanceData> wireframeCylinders;
    std::vector<InstanceData> opaqueCapsules;
    std::vector<InstanceData> wireframeCapsules;
    std::vector<InstanceData> opaqueCones;
    std::vector<InstanceData> wireframeCones;
};

class PrimitiveRenderer
{
public:
    PrimitiveRenderer(ID3D11Device *device);
    ~PrimitiveRenderer() {}

    /**
     * @brief 積んだ図形を描いて空にする。
     * @param s renderState と sceneConstantBuffer を使う
     * @param view 視錐台の更新にも使う
     * @param projection 視錐台の更新にも使う
     */
    void Render(ID3D11DeviceContext       *dc,
                const RenderServices      &s,
                const DirectX::XMFLOAT4X4 &view,
                const DirectX::XMFLOAT4X4 &projection);

    // false にすると IsVisible が常に true を返す
    void SetCullingEnabled(bool enable) { m_cullingEnabled = enable; }

    // transform は大きさ 1 の図形に掛けるワールド行列（大きさ・向き・位置を入れる）
    void DrawSphere(const DirectX::XMFLOAT4X4 &transform, const DirectX::XMFLOAT4 &color);
    void DrawWireframeSphere(const DirectX::XMFLOAT4X4 &transform, const DirectX::XMFLOAT4 &color);

    void DrawBox(const DirectX::XMFLOAT4X4 &transform, const DirectX::XMFLOAT4 &color);
    void DrawWireframeBox(const DirectX::XMFLOAT4X4 &transform, const DirectX::XMFLOAT4 &color);

    void DrawCylinder(const DirectX::XMFLOAT4X4 &transform, const DirectX::XMFLOAT4 &color);
    void DrawWireframeCylinder(const DirectX::XMFLOAT4X4 &transform, const DirectX::XMFLOAT4 &color);

    void DrawCapsule(const DirectX::XMFLOAT4X4 &transform, const DirectX::XMFLOAT4 &color);
    void DrawWireframeCapsule(const DirectX::XMFLOAT4X4 &transform, const DirectX::XMFLOAT4 &color);

    void DrawCone(const DirectX::XMFLOAT4X4 &transform, const DirectX::XMFLOAT4 &color);
    void DrawWireframeCone(const DirectX::XMFLOAT4X4 &transform, const DirectX::XMFLOAT4 &color);

private:
    bool IsVisible(const DirectX::BoundingSphere &sphere);
    bool IsVisible(const DirectX::BoundingOrientedBox &box);

    DirectX::BoundingFrustum m_frustum; // 前の Render で求めたワールド空間の視錐台
    bool                     m_cullingEnabled = true;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  inputLayout;

    // 使う PSO はこの2つだけで、ステートを個別に持たずにこれを切り替える
    PipelineState m_psoOpaque;
    PipelineState m_psoWireframe;

    // 大きさ 1 の図形
    GeometryBuffer sphereBuffer;
    GeometryBuffer halfSphereBuffer;
    GeometryBuffer boxBuffer;
    GeometryBuffer cylinderBuffer;
    GeometryBuffer coneBuffer;
    GeometryBuffer capsuleBuffer;

    InstanceBuffer m_instanceBuffer;

    // スレッドごとの配列の持ち主を見分ける番号（ShapeRenderer::m_instanceId と同じ）
    uint64_t                     m_instanceId;
    static std::atomic<uint64_t> s_instanceCounter;

    std::mutex                                      m_listMutex; // m_drawLists への追加と Render を守る
    std::vector<std::unique_ptr<PrimitiveDrawList>> m_drawLists; // 全スレッドの配列（PrimitiveRenderer が持つ）

    // 呼んだスレッドの配列を返す。初めてのスレッドなら作って m_drawLists に登録する
    PrimitiveDrawList &GetDrawList();

    void CreateBoxMesh(ID3D11Device *device, float radius, float height, float depth, GeometryBuffer &buffer);
    void CreateSphereMesh(ID3D11Device *device, float radius, int slices, int stacks, GeometryBuffer &buffer);
    void CreateHalfSphereMesh(ID3D11Device *device, float radius, int slices, int stacks, GeometryBuffer &buffer);
    void CreateCylinderMesh(ID3D11Device *device, float radius1, float radius2, float start, float height, int slices, int stacks, GeometryBuffer &buffer);
    // capsuleBuffer に作る
    void CreateCapsuleMesh(ID3D11Device *device, float radius, float cylinderHeight, int slices, int rings);
};
