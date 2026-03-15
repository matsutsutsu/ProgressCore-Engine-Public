#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "TriggerComponent.h"

// 前方宣言
class LuaScriptSystem;

class TriggerSystem : public CCL::ECS::IfSystem<TriggerSystem,
                          CCL::ECS::Write<TriggerComponent>,
                          CCL::ECS::Read<TransformComponent>>{
  public:
    TriggerSystem() : IfSystem("TriggerSystem") {}

    void Update(float dt) override;

  private:
    // 開始直後の誤作動（プレイヤーの初期位置ロードなどによる）を防ぐタイマー
    float       _startDelayTimer = 0.0f;
    const float START_DELAY      = 0.5f;

    // 毎回検索しなくて済むようにキャッシュしておく
    LuaScriptSystem *_luaSystemCache = nullptr;

    // LuaScriptSystemを取得するヘルパー
    LuaScriptSystem *GetLuaSystem();
};