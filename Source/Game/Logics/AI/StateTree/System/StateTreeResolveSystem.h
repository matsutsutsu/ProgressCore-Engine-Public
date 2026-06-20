/// @file  StateTreeResolveSystem.h
/// @brief assetHandle が未解決（= プレハブロード時に Registry がまだなかった）の
///        StateTreeComponent を、実行開始後の最初のフレームで一括解決するシステム。
///
/// なぜ必要か:
///   プレハブの Load 時点で StateTreeRegistry が World に登録済みなら
///   Meta の Load() で即解決される。しかし「先にプレハブをロードして
///   後で Registry を登録」という順序になると未解決が残る。
///   このシステムが L01_Input で毎フレーム未解決分を拾って解決する。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeComponent.h"

class StateTreeResolveSystem : public CCL::ECS::IfSystem<
    StateTreeResolveSystem,
    CCL::ECS::Write<StateTreeComponent>>
{
public:
    StateTreeResolveSystem();
    void Update(float dt) override;
};