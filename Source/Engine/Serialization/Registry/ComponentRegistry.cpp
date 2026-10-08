#include "ComponentRegistry.h"

ComponentRegistry &ComponentRegistry::Instance()
{
    static ComponentRegistry s;
    return s;
}