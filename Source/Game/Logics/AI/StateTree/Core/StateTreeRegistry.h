/// @file  StateTreeRegistry.h
/// @brief StateTreeAsset を World Resource として管理するレジストリ。
///
/// 役割:
///   - アセット（頭脳の設計図）を名前やハンドルで管理
///   - M1: ハードコードで Build*Asset() を呼んで登録
///   - M2: LoadFromJson() で JSON → StateTreeAsset コンパイルして登録
///
/// 使用例:
/// @code
///   // Scene 初期化時
///   auto& stReg = world->SetResource<StateTreeRegistry>();
///   stReg.Register(BuildGoblinAsset());   // M1 ハードコード版
///
///   // Entity スポーン時
///   auto handle = stReg.FindByName("Goblin");
///   world->Spawn(arch).Set(StateTreeComponent{ .assetHandle = handle });
/// @endcode
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "Game/Logics/AI/StateTree/Core/StateTreeAsset.h"


namespace CCL::AI
{
    // ====================================================================
    // StateTreeRegistry
    // ====================================================================

    /// @brief StateTreeAsset を World Resource として保持・提供するクラス。
    ///
    /// World::SetResource<StateTreeRegistry>() で登録後、
    /// World::GetResource<StateTreeRegistry>() で取得して使用する。
    class StateTreeRegistry
    {
    public:
        // ----------------------------------------------------------------
        // 登録
        // ----------------------------------------------------------------

        /// @brief アセットを登録し、ハンドルを返す。
        /// @param asset 構築済みの StateTreeAsset（ムーブで所有権を渡す）
        StateTreeHandle Register(StateTreeAsset&& asset)
        {
            const uint32_t idx = static_cast<uint32_t>(_assets.size());
            _nameToHandle[asset.name] = StateTreeHandle{ idx };
            _assets.push_back(std::move(asset));
            return StateTreeHandle{ idx };
        }

        // ----------------------------------------------------------------
        // アクセス
        // ----------------------------------------------------------------

        /// @brief ハンドルから StateTreeAsset を取得。無効なら nullptr。
        const StateTreeAsset* Get(StateTreeHandle handle) const
        {
            if (!handle.IsValid()) return nullptr;
            if (handle.value >= _assets.size()) return nullptr;
            return &_assets[handle.value];
        }

        StateTreeAsset* GetMutable(StateTreeHandle handle)
        {
            if (!handle.IsValid()) return nullptr;
            if (handle.value >= _assets.size()) return nullptr;
            return &_assets[handle.value];
        }

        /// @brief 名前からハンドルを取得。見つからなければ無効ハンドル。
        StateTreeHandle FindByName(const std::string& name) const
        {
            auto it = _nameToHandle.find(name);
            return (it != _nameToHandle.end()) ? it->second : StateTreeHandle{};
        }

        /// @brief 登録済みアセット数
        size_t AssetCount() const { return _assets.size(); }

        // ----------------------------------------------------------------
        // デバッグ
        // ----------------------------------------------------------------

        /// @brief 全アセット名を列挙（エディタ用）
        std::vector<std::string> GetAllNames() const
        {
            std::vector<std::string> names;
            names.reserve(_assets.size());
            for (const auto& a : _assets) names.push_back(a.name);
            return names;
        }

    private:
        std::vector<StateTreeAsset>                    _assets;
        std::unordered_map<std::string, StateTreeHandle> _nameToHandle;
    };

} // namespace CCL::AI
