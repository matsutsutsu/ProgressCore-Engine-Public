#include "RegisterComponentSerializer.h"

#include "Engine/Serialization/ComponentRegistry.h"
#include "ComponentSerializers.h"

void ComponentRegister::RegisterAllComponentsForSerialization()
{
    // 自動登録マクロの代わりに、明示的な登録関数を呼び出す
    ComponentSerializers::RegisterAll();
}