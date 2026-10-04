#include "GameSettings.h"

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include <algorithm>
#include <json.hpp>

namespace
{
    constexpr const char *kSettingsPath = "Build/User/GameSettings.json";
}

GameSettings &GameSettings::Instance()
{
    static GameSettings settings = [] {
        GameSettings s;
        s.Load();
        return s;
    }();
    return settings;
}

void GameSettings::Load()
{
    const std::string text = FileSystem::ReadAllText(kSettingsPath);
    if (text.empty()) return; // まだ保存していない（初回起動）

    try
    {
        const nlohmann::json doc = nlohmann::json::parse(text);
        lookSensitivity = std::clamp(doc.value("LookSensitivity", lookSensitivity), kSensitivityMin, kSensitivityMax);
        invertY         = doc.value("InvertY", invertY);
        bgmVolume       = std::clamp(doc.value("BgmVolume", bgmVolume), 0.0f, 1.0f);
        sfxVolume       = std::clamp(doc.value("SfxVolume", sfxVolume), 0.0f, 1.0f);
    }
    catch (const std::exception &ex)
    {
        // 壊れていても遊べるよう、既定値のまま続ける
        (void)ex;
        CCL_LOG_ERROR(LogCategory::Game, "[Settings] 設定ファイルを読めませんでした（既定値で続行）: %s", ex.what());
    }
}

void GameSettings::Save() const
{
    nlohmann::json doc;
    doc["LookSensitivity"] = lookSensitivity;
    doc["InvertY"]         = invertY;
    doc["BgmVolume"]       = bgmVolume;
    doc["SfxVolume"]       = sfxVolume;

    if (!FileSystem::WriteAllText(kSettingsPath, doc.dump(4)))
        CCL_LOG_ERROR(LogCategory::Game, "[Settings] 設定ファイルを書けませんでした: %s", kSettingsPath);
}
