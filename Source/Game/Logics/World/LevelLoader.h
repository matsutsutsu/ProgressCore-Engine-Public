#pragma once
#include "ECS/Core/CCL_World.h"
#include "Generation/MapGenerator.h"
#include <d3d11.h>
#include <string>

class LevelLoader {
  public:
    static void Initialize();

    // ★統合: これ1つで「固定マップ（Tiled形式/MapDebug形式）」を読み込む
    static void Load(
        CCL::ECS::Core::World *world, ID3D11Device *device, const std::string &jsonPath);

        static void DebugLoad(
        CCL::ECS::Core::World *world, ID3D11Device *device, const std::string &jsonPath);

    // 自動生成マップ
    static void LoadProcedural(
        CCL::ECS::Core::World *world, ID3D11Device *device, const MapGenerator::Config &config);

    static void Rebuild(CCL::ECS::Core::World *world, ID3D11Device *device);

    // --- ★追加: プレイヤーデータの引継ぎヘルパー ---
    // シーン遷移前に呼ぶ（GateSystemなどから）
    static void SavePlayerData(CCL::ECS::Core::World *world);

    // シーン開始時に呼ぶ（Load関数内で自動的に呼ばれる）
    static void RestorePlayerData(CCL::ECS::Core::World *world, CCL::ECS::EntityID playerID);

  private:
    static void BuildStageMesh(
        CCL::ECS::Core::World *world, ID3D11Device *device, const StageMap &map);
};