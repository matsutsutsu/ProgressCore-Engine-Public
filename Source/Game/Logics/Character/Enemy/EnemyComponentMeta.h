#pragma once
#include "EnemyComponent.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include <vector>

static constexpr const char *kEnemyStateNames[] = {
    "Idle", "Chase", "Attack", "Stun", "Charge", "Jump", "Dodge", "RangeAttack"};
static constexpr int kEnemyStateCount = 8;

static constexpr const char *kEnemyAITypeNames[] = {
    "Pawn", "Tank", "Jumper", "Evader", "Boid", "Fire"};
static constexpr int kEnemyAITypeCount = 6;

template <> struct ComponentMeta<EnemyComponent> {
    static constexpr bool        registered  = true;
    static constexpr const char *displayName = "Enemy";

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            FD_EnumU8("currentState",
                offsetof(EnemyComponent, currentState),
                kEnemyStateNames,
                kEnemyStateCount,
                "State Machine"),
            FD_EnumU8(
                "aiType", offsetof(EnemyComponent, aiType), kEnemyAITypeNames, kEnemyAITypeCount),
            FD_RuntimeOnly(FD_Float("stateTimer", offsetof(EnemyComponent, stateTimer), 0.01f)),

            FD_Float("moveSpeed",
                offsetof(EnemyComponent, moveSpeed),
                0.1f,
                0.0f,
                50.0f,
                "Base Parameters"),
            FD_Float("detectRange", offsetof(EnemyComponent, detectRange), 0.1f, 0.0f, 100.0f),
            FD_Float("attackRange", offsetof(EnemyComponent, attackRange), 0.05f, 0.0f, 20.0f),

            FD_Float("speedRandomness",
                offsetof(EnemyComponent, speedRandomness),
                0.01f,
                0.5f,
                2.0f,
                "Individual Variance"),
            FD_Float("noiseOffset", offsetof(EnemyComponent, noiseOffset), 0.01f),
            FD_Int("formationSlot", offsetof(EnemyComponent, formationSlot), 0.0f, 32.0f),

            FD_Float("chargeSpeed",
                offsetof(EnemyComponent, chargeSpeed),
                0.5f,
                0.0f,
                60.0f,
                "Special Actions"),
            FD_Float("jumpForce", offsetof(EnemyComponent, jumpForce), 0.1f, 0.0f, 30.0f),
            FD_RuntimeOnly(
                FD_Float("dodgeCooldown", offsetof(EnemyComponent, dodgeCooldown), 0.05f)),
            FD_Float(
                "rangeAttackDist", offsetof(EnemyComponent, rangeAttackDist), 0.1f, 0.0f, 50.0f),

            FD_RuntimeOnly(FD_Float("chargeCooldown",
                offsetof(EnemyComponent, chargeCooldown),
                0.05f,
                0.0f,
                0.0f,
                "Tank Charge")),
            FD_Float("chargeInterval", offsetof(EnemyComponent, chargeInterval), 0.1f, 0.5f, 30.0f),
            FD_Float(
                "chargeWindUpTime", offsetof(EnemyComponent, chargeWindUpTime), 0.01f, 0.0f, 5.0f),
            FD_Float("chargeDuration", offsetof(EnemyComponent, chargeDuration), 0.01f, 0.0f, 5.0f),
            FD_Float("chargeRecoveryTime",
                offsetof(EnemyComponent, chargeRecoveryTime),
                0.01f,
                0.0f,
                5.0f),

            FD_RuntimeOnly(FD_Float("jumpCooldown",
                offsetof(EnemyComponent, jumpCooldown),
                0.05f,
                0.0f,
                0.0f,
                "Jumper Settings")),
            FD_Float("jumpInterval", offsetof(EnemyComponent, jumpInterval), 0.1f, 0.5f, 15.0f),

            FD_Float("weightSeek",
                offsetof(EnemyComponent, weightSeek),
                0.05f,
                0.0f,
                10.0f,
                "Steering Weights"),
            FD_Float(
                "weightSeparation", offsetof(EnemyComponent, weightSeparation), 0.05f, 0.0f, 10.0f),
            FD_Float("weightWall", offsetof(EnemyComponent, weightWall), 0.05f, 0.0f, 20.0f),
            FD_Float("weightAlign", offsetof(EnemyComponent, weightAlign), 0.05f, 0.0f, 10.0f),
            FD_Float("keepDistance", offsetof(EnemyComponent, keepDistance), 0.1f, 0.0f, 50.0f),

            FD_RuntimeOnly(FD_Float("attackCooldown",
                offsetof(EnemyComponent, attackCooldown),
                0.05f,
                0.0f,
                0.0f,
                "Attack")),
            FD_Float(
                "attackInterval", offsetof(EnemyComponent, attackInterval), 0.05f, 0.1f, 10.0f),
            FD_Bool("hasActioned", offsetof(EnemyComponent, hasActioned)),

            FD_Int("dropMoney", offsetof(EnemyComponent, dropMoney), 0.0f, 9999.0f, "Misc"),
            FD_Bool("hasAttackToken", offsetof(EnemyComponent, hasAttackToken)),
            FD_Bool("isOrbiting", offsetof(EnemyComponent, isOrbiting)),

            FD_Float("baseDamage", offsetof(EnemyComponent, baseDamage), 1.0f, 0.0f, 9999.0f, "Combat Stats"),
            FD_Float("knockbackPower", offsetof(EnemyComponent, knockbackPower), 0.5f, 0.0f, 100.0f),
            FD_Float("knockbackResistance", offsetof(EnemyComponent, knockbackResistance), 0.05f, 0.0f, 1.0f),
        };
        return fields;
    }
};