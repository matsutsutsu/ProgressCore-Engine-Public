#include "AttachmentMasterData.h"
#include <fstream>
#include <iostream>
#include <json.hpp>

using json = nlohmann::json;

std::unordered_map<EffectID, AttachmentParam> AttachmentMasterData::_data;
AttachmentParam                               AttachmentMasterData::_defaultParam;

EffectID AttachmentMasterData::StringToEffectID(const std::string &name)
{
    if (name == "Homing") return EffectID::Homing;
    if (name == "Accelerate") return EffectID::Accelerate;
    if (name == "Slow") return EffectID::Slow;
    if (name == "Pierce") return EffectID::Pierce;
    if (name == "Knockback") return EffectID::Knockback;
    if (name == "Explosion") return EffectID::Explosion;
    if (name == "Split") return EffectID::Split;
    if (name == "BlackHole") return EffectID::BlackHole;
    if (name == "Chaos") return EffectID::Chaos;
    if (name == "Reflection") return EffectID::Reflection;
    return EffectID::None;
}

void AttachmentMasterData::LoadFromJson(const std::string &filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[AttachmentMasterData] Failed to open file: " << filepath << std::endl;
        return;
    }

    json root;
    try {
        file >> root;
        _data.clear();

        for (auto it = root.begin(); it != root.end(); ++it) {
            EffectID id = StringToEffectID(it.key());
            if (id == EffectID::None) continue;

            AttachmentParam param;
            auto           &val = it.value();

            //  汎用データ取得ラムダ: JSONにキーがあれば読み込む
            auto tryGet = [&](const char *key, float &out) {
                if (val.contains(key)) out = val[key].get<float>();
            };

            // ★ 追加: Spawnフェーズ用のパラメータ読み込み
            tryGet("SpawnSpeedBase", param.spawnValue1);
            tryGet("SpawnSpeedPerLevel", param.spawnScale1);
            tryGet("SpawnAddCountBase", param.spawnValue1);
            tryGet("SpawnAddCountPerLevel", param.spawnScale1);
            tryGet("SpawnAddSpreadBase", param.spawnValue2);
            tryGet("SpawnAddSpreadPerLevel", param.spawnScale2);

            // ChaosのSpawn専用特殊パラメータ
            tryGet("SpawnMaxAngle", param.spawnValue1);
            tryGet("SpawnSpreadMin", param.spawnValue2);
            tryGet("SpawnSpreadMax", param.spawnScale2);
            tryGet("SpawnSpeedMin", param.baseValue1);
            tryGet("SpawnSpeedMax", param.levelScale1);

            // Homing 用
            tryGet("SpawnSpeedMultBase", param.baseValue1);
            tryGet("SpawnSpeedMultPerLevel", param.levelScale1);
            tryGet("TurnSpeedBase", param.baseValue2);
            tryGet("TurnSpeedPerLevel", param.levelScale2);

            // Accelerate / Slow 用
            tryGet("BaseRate", param.baseValue1);
            tryGet("RatePerLevel", param.levelScale1);
            tryGet("BaseBrakePower", param.baseValue1);
            tryGet("BrakePowerPerLevel", param.levelScale1);

            // Pierce / Reflection / Split(弾数) 用
            tryGet("BaseAddCount", param.baseValue1);
            tryGet("AddCountPerLevel", param.levelScale1);
            tryGet("BaseReflectCount", param.baseValue1);
            tryGet("ReflectCountPerLevel", param.levelScale1);

            // Split(拡散角) 用
            tryGet("BaseAddSpread", param.baseValue2);
            tryGet("AddSpreadPerLevel", param.levelScale2);

            // Knockback 用
            tryGet("BasePowerMult", param.baseValue1);
            tryGet("PowerMultPerLevel", param.levelScale1);

            // Explosion / BlackHole 用
            tryGet("BaseRadius", param.baseValue1);
            tryGet("RadiusPerLevel", param.levelScale1);
            tryGet("BaseDamageMult", param.baseValue2);
            tryGet("DamageMultPerLevel", param.levelScale2);

            // Chaos 用
            tryGet("BaseMaxAngle", param.baseValue1);
            tryGet("MaxAnglePerLevel", param.levelScale1);
            tryGet("BaseSpeedMultRange", param.baseValue2);
            tryGet("SpeedMultRangePerLevel", param.levelScale2);

            // 共通
            tryGet("Interval", param.interval);

            _data[id] = param;
        }
        std::cout << "[AttachmentMasterData] Successfully loaded!" << std::endl;
    }
    catch (const std::exception &e) {
        std::cerr << "[AttachmentMasterData] JSON parse error: " << e.what() << std::endl;
    }
}

const AttachmentParam &AttachmentMasterData::GetParam(EffectID id)
{
    auto it = _data.find(id);
    if (it != _data.end()) return it->second;
    return _defaultParam;
}