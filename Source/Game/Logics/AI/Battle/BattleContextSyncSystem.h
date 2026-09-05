// BattleContext（世界の事実）にプレイヤー・ボスの居場所を毎フレーム反映する。
// このシステムが唯一の書き込み元で、登録もここで行う（シーン側に任せると、シーンを
// 増やすたびに書き忘れでAI系が丸ごと no-op になる）。
//
// 実行ステージは L01_WorldState。リソースへのアクセスは Read<>/Write<> 申告に載らず
// スケジューラが依存を検出できないので、読み手と同ステージに置くと順序が決まらない。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

// PlayerComponent はフィルタに使うだけで、実際の更新は Update 内の World::View で
// プレイヤー・ボスの両方を探して行う。
class BattleContextSyncSystem : public CCL::ECS::IfSystem<
                                    BattleContextSyncSystem,
                                    CCL::ECS::Read<PlayerComponent>,
                                    CCL::ECS::Read<TransformComponent>>
{
  public:
    BattleContextSyncSystem()
        : IfSystem("BattleContextSyncSystem")
    {
    }
    void Initialize() override;
    void Update(float dt) override;

  private:
    /// BattleContext が未登録なら登録する。GetResource は未登録だと assert で落ちるので、
    /// Update からも呼んで入口を一本化する。
    void EnsureContext();
};
