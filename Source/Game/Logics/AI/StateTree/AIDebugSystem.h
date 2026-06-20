/// @file  AIDebugSystem.h
/// @brief AI パイプライン全体を段階的に診断するデバッグ専用システム。
///
/// 使い方:
///   1. REGISTER_LOGIC_SYSTEM マクロで自動登録済み（.cpp 参照）
///   2. 起動後、ログに [AI-DIAG] で始まる行が出る
///   3. どの STAGE で止まっているかを見てバグを特定する
///
/// 診断の流れ:
///   STAGE-1: Resource が存在するか
///   STAGE-2: NavFlowField が有効か（BFS が動いているか）
///   STAGE-3: 敵 Entity が StateTreeComponent を持っているか
///   STAGE-4: assetHandle が有効か
///   STAGE-5: PerceptionComponent が更新されているか
///   STAGE-6: StateTree のアクティブ状態が何か
///   STAGE-7: MoveIntent が書かれているか
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/StateTree/StateTreeComponent.h"
#include "Game/Logics/AI/StateTree/PerceptionComponent.h"
#include "Game/Logics/AI/StateTree/MoveIntentComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

class AIDebugSystem : public CCL::ECS::IfSystem<
    AIDebugSystem,
    CCL::ECS::Read<StateTreeComponent>,
    CCL::ECS::Read<PerceptionComponent>,
    CCL::ECS::Read<MoveIntentComponent>,
    CCL::ECS::Read<TransformComponent>>
{
public:
    AIDebugSystem();
    void Update(float dt) override;

private:
    float   _timer = 0.0f;
    float   _interval = 2.0f;  // 2秒おきにログを出す（毎フレームだと洪水になる）
    int     _frameCount = 0;
    bool    _firstFrame = true;
};