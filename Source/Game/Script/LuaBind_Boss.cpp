#include "LuaBind_Boss.h"
#include "ECS/Core/CCL_World.h"
#include "Game/Logics/Character/Enemy/BossComponent.h"

#include <Windows.h>


#include "ECS/Common/CCL_Common.h" // EntityIDの定義があるヘッダー

void LuaBind_Boss::Bind(sol::state& lua, CCL::ECS::Core::World* world) {

    // 1. BossState (Enum) を登録
    lua.new_enum("BossState",
        "Idle",        BossState::Idle,
        "Move",        BossState::Move,
        "Charge",      BossState::Charge,
        "SweepAttack", BossState::SweepAttack,
        "Summon",      BossState::Summon,
        "Shockwave",   BossState::Shockwave,
        "Death",       BossState::Death
    );

    // 2. BossComponent を登録
    lua.new_usertype<BossComponent>("BossComponent",
        // 変数へのアクセス
        "state",      &BossComponent::state,
        "timer",      &BossComponent::stateTimer,
        "hpPhase",    &BossComponent::currentPhase,
        "dist",       &BossComponent::distanceToPlayer, // C++が計算した距離
        "summonCD",   &BossComponent::summonCooldown,
        
        // ★Luaから命令を出すためのヘルパー関数
        "SetState", [](BossComponent& b, BossState newState) {
            b.state = newState;
            b.stateTimer = 0.0f;      // タイマーリセット
            b.hasActioned = false;    // フラグもリセット
            b.hasHitTarget = false;
        }
    );



    // LuaBind_Boss.cpp
    auto entityTable = lua["Entity"].get_or_create<sol::table>();
    entityTable.set_function("GetBoss", [world](CCL::ECS::EntityID id) -> BossComponent * {
#ifdef _WIN32
        std::string msg = "[LuaBind_Boss] GetBoss called with ID: " + std::to_string(id);
        OutputDebugStringA((msg + "\n").c_str());
#endif

        // ★まずWorldが有効か確認
        if (!world) {
#ifdef _WIN32
            OutputDebugStringA("[LuaBind_Boss] ERROR: World is NULL!\n");
#endif
            return nullptr;
        }

        auto *boss = world->GetComponent<BossComponent>(id);

#ifdef _WIN32
        if (boss) {
            OutputDebugStringA("[LuaBind_Boss] Boss found!\n");
        }
        else {
            OutputDebugStringA("[LuaBind_Boss] Boss NOT found!\n");

            // ★追加デバッグ: そのEntityが存在するか確認
            // (Worldに IsEntityValid のようなメソッドがあれば)
            // bool exists = world->IsEntityValid(id);
            // std::string existMsg = exists ? "Entity EXISTS" : "Entity DOES NOT EXIST";
            // OutputDebugStringA(("[LuaBind_Boss] " + existMsg + "\n").c_str());
        }
#endif

        return boss;
    });

}