#include "PrimitiveRenderSystem.h"

// 実装に必要なヘッダ
#include "ECS/Core/CCL_World.h"
#include "Engine/Graphics/Renderer/PrimitiveRenderer.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

#ifdef TRACY_ENABLE
#include "tracy/Tracy.hpp"
#endif


using namespace CCL::ECS;

PrimitiveRenderSystem::PrimitiveRenderSystem() : IfSystem("PrimitiveRenderSystem") {}

void PrimitiveRenderSystem::Update(float dt)
{
    // Worldからレンダラーのポインタを取得
    if (!_world->HasResource<PrimitiveRenderer *>()) return;
    auto *renderer = _world->GetResource<PrimitiveRenderer *>();

    if (!renderer) return;

    ForEachParallel([&](const TransformComponent& trans, const PrimitiveComponent& prim) {

        DirectX::XMMATRIX finalWorldMat;
//        {
//#ifdef TRACY_ENABLE
//            ZoneScopedN("1. Matrix Math");
//#endif
            DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(&trans.worldMatrix);
            DirectX::XMMATRIX scaleMat = DirectX::XMMatrixIdentity();

            // ★防弾：スケールが完全な0になると法線がNaNになり画面が崩壊するため、最小値を保証する
            float minS = 0.0001f;

            // 1. 古い設計(引数3つ)との違い: 
            // ここで Component が持っているサイズ情報を行列（スケール）に変換してしまいます
            switch (prim.type) {
            case PrimitiveType::Box:
                scaleMat = DirectX::XMMatrixScaling(
                    (std::max)(prim.size.x, minS),
                    (std::max)(prim.size.y, minS),
                    (std::max)(prim.size.z, minS));
                break;
            case PrimitiveType::Sphere:
                scaleMat = DirectX::XMMatrixScaling(
                    (std::max)(prim.radius, minS),
                    (std::max)(prim.radius, minS),
                    (std::max)(prim.radius, minS));
                break;
            case PrimitiveType::Cylinder:
            case PrimitiveType::Capsule:
            case PrimitiveType::Cone:
                scaleMat = DirectX::XMMatrixScaling(
                    (std::max)(prim.radius, minS),
                    (std::max)(prim.height, minS),
                    (std::max)(prim.radius, minS));
                break;
            }

            // 2. 元の Transform にスケールを掛け合わせる
            finalWorldMat = scaleMat * worldMat;

        //}


        // ==========================================================
        // 【フェーズ2：異常検知トラップ（犯人候補）】
        // ==========================================================
//        {
//#ifdef TRACY_ENABLE
//            ZoneScopedN("2. NaN Check (Trap)");
//#endif
            // =========================================================
            // ★ ここにトラップを仕掛ける（NaN または 異常な巨大数 を検知）
            // =========================================================
            DirectX::XMFLOAT4X4 checkMat;
            DirectX::XMStoreFloat4x4(&checkMat, finalWorldMat);


            // 1. 非数(NaN)や無限大(Inf)が混じっていないか？
            // 2. 座標(4行目)が 100000.0 などの異常に吹き飛んだ値になっていないか？
            if (std::isnan(checkMat._41) || std::isinf(checkMat._41) || abs(checkMat._41) > 100000.0f ||
                std::isnan(checkMat._11) || std::isinf(checkMat._11))
            {
                // ★ 異常検知！ ここでVisual Studioのデバッガが強制停止します
                __debugbreak();
            }
        //}


        DirectX::XMFLOAT4X4 finalWorld;
        DirectX::XMStoreFloat4x4(&finalWorld, finalWorldMat);

//        {
//#ifdef TRACY_ENABLE
//            ZoneScopedN("3. Renderer Push");
//#endif

            // 3. レンダラーの呼び出し (引数は finalWorld と color の 2つだけになる！)
            switch (prim.type) {
            case PrimitiveType::Box:
                if (prim.isWireframe) renderer->DrawWireframeBox(finalWorld, prim.color);
                else                  renderer->DrawBox(finalWorld, prim.color);
                break;

            case PrimitiveType::Sphere:
                if (prim.isWireframe) renderer->DrawWireframeSphere(finalWorld, prim.color);
                else                  renderer->DrawSphere(finalWorld, prim.color);
                break;

            case PrimitiveType::Cylinder:
                if (prim.isWireframe) renderer->DrawWireframeCylinder(finalWorld, prim.color);
                else                  renderer->DrawCylinder(finalWorld, prim.color);
                break;

            case PrimitiveType::Capsule:
                if (prim.isWireframe) renderer->DrawWireframeCapsule(finalWorld, prim.color);
                else                  renderer->DrawCapsule(finalWorld, prim.color);
                break;

            case PrimitiveType::Cone:
                if (prim.isWireframe) renderer->DrawWireframeCone(finalWorld, prim.color);
                else                  renderer->DrawCone(finalWorld, prim.color);
                break;
            }

        //}
    });
}

// ==========================================
// マクロは必ず .cpp の末尾に1回だけ書く
// ==========================================
REGISTER_RENDER_SYSTEM(PrimitiveRenderSystem, Priority::RenderStage::Main);