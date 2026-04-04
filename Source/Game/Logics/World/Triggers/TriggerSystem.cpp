#include "TriggerSystem.h"
#include "ECS/System/CCL_SystemManager.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Engine/Scripting/LuaScriptSystem.h" // LuaSystemの定義が必要
#include <SimpleMath.h>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace DirectX::SimpleMath;
using namespace CCL::ECS;

// LuaScriptSystem を安全に取得・キャッシュする
LuaScriptSystem *TriggerSystem::GetLuaSystem()
{
    if (_luaSystemCache) return _luaSystemCache;

    if (_world) {
        // Worldのリソースとして登録されている SystemManager を取得する
        if (!_world->HasResource<CCL::ECS::SystemManager *>()) return nullptr;


        auto *sysMgr       = _world->GetResource<CCL::ECS::SystemManager *>();
        auto &logicSystems = sysMgr->GetLogicSystems();

        for (auto &sys : 
            logicSystems) {
            // 名前判定や型判定で探す。ここではdynamic_castを使用
            if (auto *p = dynamic_cast<LuaScriptSystem *>(sys.get())) {
                _luaSystemCache = p;
                return p;
            }
        }
    }
    return nullptr;
}

void TriggerSystem::Update(float dt)
{
    // 1. 安全装置: シーン開始直後は判定しない
    if (_startDelayTimer < START_DELAY) {
        _startDelayTimer += dt;
        return;
    }

    // 2. プレイヤーの位置を取得
    //    (本来はTag検索やSingletonComponentなどで取得するのが高速ですが、現状のView方式で実装します)
    Vector3 playerPos   = Vector3::Zero;
    bool    playerFound = false;

    // PlayerComponentを持つエンティティを探す
    auto playerView = _world->View<PlayerComponent, TransformComponent>();
    if (!playerView.empty()) {
        // 最初の1人だけを対象とする
        EntityID playerID = playerView[0];
        if (auto *trans = _world->GetComponent<TransformComponent>(playerID)) {
            playerPos   = trans->GetWorldPosition();
            playerFound = true;
        }
    }

    // プレイヤーがいなければ判定できないので終了
    if (!playerFound) return;

    // LuaSystemの準備
    LuaScriptSystem *luaSys = GetLuaSystem();
    if (!luaSys) return; // スクリプトシステムがないならイベントも起きない

    // 3. 全てのTriggerに対して判定
    ForEachWithID(
        [&](CCL::ECS::EntityID id, TriggerComponent &trigger, const TransformComponent &trans) {
        // 既に発動済みのOneShotトリガーは無視
        if (trigger.isOneShot && trigger.hasTriggered) return;

        float dist = Vector3::Distance(trans.GetWorldPosition(), playerPos);
        bool isInside = (dist <= trigger.radius);
        
        // 範囲に入った【瞬間】 (Enter)
        if (isInside && !trigger.wasInside) {
            if (!(trigger.isOneShot && trigger.hasTriggered)) {
                luaSys->CallFunction(id, "OnTriggerEnter");
                trigger.hasTriggered = true;
            }
        }
        // 範囲に居続けている (Stay)
        else if (isInside && trigger.wasInside) {
            // luaSys->CallFunction(id, "OnTriggerStay");
        }
        // 範囲から出た【瞬間】 (Exit)
        else if (!isInside && trigger.wasInside) {
            // luaSys->CallFunction(id, "OnTriggerExit");
        }
        
        // 状態を更新
        trigger.wasInside = isInside;
    });
}


REGISTER_LOGIC_SYSTEM(TriggerSystem, Priority::LogicStage::L06_Resolution);