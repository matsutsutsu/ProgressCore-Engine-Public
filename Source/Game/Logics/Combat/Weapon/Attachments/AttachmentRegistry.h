#pragma once
#include "ECS/Common/CCL_Common.h"
#include "Game/Logics/Combat/Weapon/Core/BulletBlueprint.h"
#include "Game/Logics/Combat/Weapon/Core/BulletEnums.h"
#include <vector>
#include <DirectXMath.h>

// 前方宣言
namespace CCL::ECS::Core {
    class World;
}

// ===========================================================================
// ① Spawnフェーズ用（Archetype生成前・発射パラメータ計算用）
// ===========================================================================
struct SpawnContext {
    int                     &bulletCount;
    float                   &spread;
    float                   &speed;
    int                     &pierceCount;
    DirectX::XMFLOAT3       &direction;
    const DirectX::XMFLOAT3 &startPos;
    CCL::ECS::Core::World   *world;
};

struct SpawnBinding {
    EffectID effect;
    // level(モッドの重複度) に応じてパラメータを書き換える関数ポインタ
    void (*applySpawn)(int level, SpawnContext &ctx);
};


// ===========================================================================
// ② Travel / OnHit フェーズ用（Archetype動的合成・データ注入用）
// ===========================================================================
struct AttachmentBinding {
    BulletPhase phase;
    EffectID    effect;
    void (*addToArchetype)(CCL::ECS::Archetype &);
    void (*injectData)(CCL::ECS::Core::World *, CCL::ECS::EntityID, int, const BulletBlueprint *);
};



// ===========================================================================
// クラス名: AttachmentRegistry
// 役割: アタッチメントの「初期化・生成ルール」を全てここに集約する
// ===========================================================================
class AttachmentRegistry {
  public:
    static const std::vector<SpawnBinding>      &GetSpawnBindings();
    static const std::vector<AttachmentBinding> &GetBindings();
};