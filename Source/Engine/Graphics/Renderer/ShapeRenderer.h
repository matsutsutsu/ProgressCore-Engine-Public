#pragma once
// ===========================================================================
// 概要: デバッグ用の線の図形（箱・球・カプセル・矢印・グリッドなど）と塗った三角形を描くクラス。
//
// 責務:
//   - Draw* で図形をインスタンスとして積む（別スレッドから呼んでよい）
//   - Render で、図形の種類ごとにインスタンシングでまとめて描き、積んだものを空にする
//   - 図形の単位で視錐台カリングする
//
// 備考: スレッドごとに別の配列（ShapeDrawList）に積むので、積むときにロックしない。
//       視錐台は Render で更新するので、Draw* のカリングは1フレーム前の視錐台で判定する。
//       サイズ・色を引数で渡す API。行列で渡す塗りの図形は PrimitiveRenderer を使う。
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

class ShapeRenderer
{
public:
    ShapeRenderer(ID3D11Device *device);
    ~ShapeRenderer() {}

    // false にすると視錐台カリングをせず、全部描く
    void SetCullingEnabled(bool enable) { m_cullingEnabled = enable; }

    // 位置・大きさは m、角度はラジアン
    void DrawBox(const DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT3 &angle, const DirectX::XMFLOAT3 &size, const DirectX::XMFLOAT4 &color);
    void DrawSphere(const DirectX::XMFLOAT3 &position, float radius, const DirectX::XMFLOAT4 &color);
    void DrawCapsule(const DirectX::XMFLOAT4X4 &transform, float radius, float height, const DirectX::XMFLOAT4 &color);
    void DrawWireframeCylinder(const DirectX::XMFLOAT3 &position, float radius, float height, const DirectX::XMFLOAT4 &color);
    void DrawWireframeCircle(const DirectX::XMFLOAT3 &center, float radius, const DirectX::XMFLOAT4 &color, const DirectX::XMFLOAT4 &rotation);
    void DrawArrow(const DirectX::XMFLOAT3 &start, const DirectX::XMFLOAT3 &end, float headSize, const DirectX::XMFLOAT4 &color);
    void DrawBone(const DirectX::XMFLOAT4X4 &transform, float length, const DirectX::XMFLOAT4 &color);
    void DrawLine(const DirectX::XMFLOAT3 &p1, const DirectX::XMFLOAT3 &p2, const DirectX::XMFLOAT4 &color);
    void DrawTriangle(const DirectX::XMFLOAT3 &v1, const DirectX::XMFLOAT3 &v2, const DirectX::XMFLOAT3 &v3, const DirectX::XMFLOAT4 &color);
    void DrawSolidTriangle(const DirectX::XMFLOAT3 &v0, const DirectX::XMFLOAT3 &v1, const DirectX::XMFLOAT3 &v2, const DirectX::XMFLOAT4 &color);
    void DrawRay(const DirectX::XMFLOAT3 &origin, const DirectX::XMFLOAT3 &direction, float length, const DirectX::XMFLOAT4 &color);
    void DrawGrid(float spacing, int subdivisions, const DirectX::XMFLOAT4 &color);
    void DrawAxis(const DirectX::XMFLOAT4X4 &transform, float length);

    // 積んだ図形を描いて空にする。視錐台もここで更新する
    void Render(ID3D11DeviceContext *dc, const DirectX::XMFLOAT4X4 &view, const DirectX::XMFLOAT4X4 &projection);

private:
    DirectX::BoundingFrustum m_frustum; // 前の Render で求めたワールド空間の視錐台
    bool                     m_cullingEnabled = true;

    bool IsVisible(const DirectX::BoundingSphere &sphere);
    bool IsVisible(const DirectX::BoundingOrientedBox &box);

    struct Mesh
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
        UINT                                 vertexCount;
    };

    // 大きさ 1 の図形を線のリスト（LINELIST）として作る
    void CreateMesh(ID3D11Device *device, const std::vector<DirectX::XMFLOAT3> &vertices, Mesh &mesh);
    void CreateBoxMesh(ID3D11Device *device, float width, float height, float depth);
    void CreateSphereMesh(ID3D11Device *device, float radius, int subdivisions);
    void CreateHalfSphereMesh(ID3D11Device *device, float radius, int subdivisions);
    void CreateCylinderMesh(ID3D11Device *device, float radius1, float radius2, float start, float height, int subdivisions);
    void CreateBoneMesh(ID3D11Device *device, float length);
    void CreateLineMesh(ID3D11Device *device);
    void CreateTriangleFaceMesh(ID3D11Device *device);

    // カリングせずに線を1本積む。
    //   円やカプセルのように1つの形を線の集まりで表すものは、線ごとに視錐台と比べてはいけない。
    //   m_frustum は Render でしか更新されず、積むときは必ず1フレーム前の視錐台を見るので、
    //   カメラを回すと古い視錐台の面に沿って形がまっすぐ切れて見える。
    //   形の単位でまとめて判定し、中の線はこれで積む
    void DrawLineNoCull(const DirectX::XMFLOAT3 &p1, const DirectX::XMFLOAT3 &p2, const DirectX::XMFLOAT4 &color);

private:
    Mesh boxMesh;
    Mesh sphereMesh;
    Mesh halfSphereMesh;
    Mesh cylinderMesh;
    Mesh boneMesh;
    Mesh lineMesh;
    Mesh triangleFaceMesh; // 塗った三角形（三角形リスト）

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  inputLayout;

    // 深度テストをしないステート（今は使っていない。図形を常に手前に出したいときに Render で設定する）
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_debugDepthState;

    // 塗った三角形の描画用（半透明・深度はテストだけ・両面・深度バイアスあり）
    Microsoft::WRL::ComPtr<ID3D11BlendState>        m_transparentBlendState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthTestNoWriteState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_cullNoneRasterizerState;

    InstanceBuffer m_instanceBuffer;

    // スレッドごとの配列の持ち主を見分ける番号。ShapeRenderer を作り直すと（デバイスロストからの
    // 復帰など）番号が変わり、各スレッドは thread_local に残った古い配列を使わずに登録し直す
    uint64_t                     m_instanceId;
    static std::atomic<uint64_t> s_instanceCounter;

    // スレッド1つ分の、図形の種類ごとのインスタンスの配列
    struct ShapeDrawList
    {
        std::vector<InstanceData> boxes;
        std::vector<InstanceData> spheres;
        std::vector<InstanceData> halfSpheres;
        std::vector<InstanceData> cylinders;
        std::vector<InstanceData> bones;
        std::vector<InstanceData> lines;
        std::vector<InstanceData> solidTriangles;
    };

    std::mutex                                  m_listMutex; // m_drawLists への追加と Render を守る
    std::vector<std::unique_ptr<ShapeDrawList>> m_drawLists; // 全スレッドの配列（ShapeRenderer が持つ）

    // 呼んだスレッドの配列を返す。初めてのスレッドなら作って m_drawLists に登録する
    ShapeDrawList &GetDrawList();
};
