#pragma once
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "PlayerDeathSequenceComponent.h"
#include <vector>

static constexpr const char *kPlayerDeathPhaseNames[] = {
    "InitialShock", "WaitShake", "ZoomAndFog", "Completed"};
static constexpr int kPlayerDeathPhaseCount = 4;

template <> struct ComponentMeta<PlayerDeathSequenceComponent> {
    static constexpr bool        registered  = true;
    static constexpr const char *displayName = "Death Sequence";

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            FD_EnumInt("phase",
                offsetof(PlayerDeathSequenceComponent, phase),
                kPlayerDeathPhaseNames,
                kPlayerDeathPhaseCount,
                "Sequence State"),
            FD_RuntimeOnly(
                FD_Float("stateTimer", offsetof(PlayerDeathSequenceComponent, stateTimer), 0.01f)),
        };
        return fields;
    }
};