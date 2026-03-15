#pragma once
#include <sol/sol.hpp>
namespace CCL::ECS::Core {
    class World;
}

class LuaBind_Boss {
  public:
    static void Bind(sol::state &lua, CCL::ECS::Core::World *world);
};