/// @file HitEffectSystem.cpp
/// 役割・設計方針は HitEffectSystem.h を参照。
#include "HitEffectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/GameEvent/GameEvents.h"

#include <DirectXMath.h>

using namespace CCL::ECS;

namespace
{
// スパークのバーストエミッタ・プレハブ（.json は SpawnPrefab が補完する）。
const char *kHitSparkPrefab = "Assets/Prefabs/Particles/VFX_HitSpark";
} // namespace

void HitEffectSystem::Initialize()
{
    // 確定ヒットの通知を受けて、その位置にスパークのバーストを1個出す。
    ListenEvent<HitImpactEvent>([this](const HitImpactEvent &e) {
        // 位置なしで生成 → 直接 Transform をセットして即ワールド行列を焼く。
        // ※ SpawnPrefab(path,pos) の座標セットは遅延適用なので、最初のバーストが原点で
        //   出てしまう。ここでは live なコンポーネントを直接書き換えて確実に位置を決める。
        EntityID rootID = Prefab::SpawnPrefab(*_world, kHitSparkPrefab);
        if (rootID == InvalidEntityID) return;

        // ルートを命中位置へ。ワールド行列もこの場で確定させる。
        DirectX::XMMATRIX rootWorld = DirectX::XMMatrixIdentity();
        if (auto *t = _world->GetComponent<TransformComponent>(rootID))
        {
            // ★重要：プレハブのルートには宙吊りの parent 参照(例: parent=3)が残っており、
            //   単体スポーン時に無関係なエンティティの子として解決されると、その位置・
            //   スケールを継承して「位置ズレ＋細長い粒子」になる。必ず親を切り離す。
            t->parent   = 0; // 親なし＝ワールド直下のルートにする
            t->position = e.position;
            t->rotation = {0.0f, 0.0f, 0.0f, 1.0f}; // 継承していた回転もリセット
            t->scale    = {1.0f, 1.0f, 1.0f};       // 継承していたスケールもリセット
            t->isStatic = false;
            t->isDirty  = true;
            t->UpdateMatrix(); // 原点フラッシュ防止：この場でワールド行列を確定
            rootWorld = DirectX::XMLoadFloat4x4(&t->worldMatrix);
        }
        _active.push_back({rootID, kVfxLifetime});

        // エミッタ(GPUParticle)を探して発火する。
        // プレハブがエディタ保存で「ルート＋子エミッタ」構成になっていても動くよう、
        // ルートに無ければルート直下の子から GPUParticle を探す。
        if (auto *gp = _world->GetComponent<GPUParticleComponent>(rootID))
        {
            gp->hasPrevTransform = false; // スメア防止（前姿勢を現在位置で初期化させる）
            gp->TriggerBurst();
            return;
        }
        for (EntityID cid : _world->View<GPUParticleComponent, TransformComponent>())
        {
            auto *ct = _world->GetComponent<TransformComponent>(cid);
            if (!ct || ct->parent != rootID) continue; // このVFXの子エミッタだけ

            // 子エミッタは命中位置ぴったりに出したいので、姿勢をルート直下(オフセット0)に揃え、
            // ワールド行列も即確定させる（TransformUpdateSystem の反映を待たない）。
            ct->position = {0.0f, 0.0f, 0.0f};
            ct->rotation = {0.0f, 0.0f, 0.0f, 1.0f};
            ct->scale    = {1.0f, 1.0f, 1.0f};
            ct->isDirty  = true;
            ct->UpdateMatrix(rootWorld); // 親(ルート)行列を渡してワールド=命中位置に確定

            if (auto *cgp = _world->GetComponent<GPUParticleComponent>(cid))
            {
                // ★スメア防止：エミッタが原点→命中位置へ動いた分だけ粒子が線状に
                //   引き伸ばされるのを防ぐ。前フレーム姿勢を「現在(命中位置)」で初期化させる。
                cgp->hasPrevTransform = false;
                cgp->TriggerBurst();
            }

            _active.push_back({cid, kVfxLifetime}); // 子も寿命管理して破棄する
            break;
        }
    });
}

void HitEffectSystem::Update(float dt)
{
    // 寿命が尽きたエミッタを破棄する（粒子が消えてから安全に片付ける）。
    for (size_t i = 0; i < _active.size();)
    {
        _active[i].life -= dt;
        if (_active[i].life <= 0.0f)
        {
            if (_world->IsEntityValid(_active[i].id))
                _world->Destroy(_active[i].id);
            _active[i] = _active.back(); // swap-erase（順序不問）
            _active.pop_back();
        }
        else
        {
            ++i;
        }
    }
}

// ダメージ解決(L06)後の後片付け相当のタイミングで寿命管理する。
REGISTER_LOGIC_SYSTEM(HitEffectSystem, Priority::LogicStage::L07_Cleanup);
