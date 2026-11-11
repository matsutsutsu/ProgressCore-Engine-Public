#pragma once
// ===========================================================================
// 概要: BT アセット（木の実体）をパスごとに1つだけ持つキャッシュ（シングルトン）
//
// 責務:
//   - パスに対応する木を初回だけ読み込み、以降は同じものを返す
//   - 読み込みに失敗したパスを記録し、再試行しない
//   - 指定したパスを読み直す（Reload）
//
// 備考: 木はアセットで、個体が持つのは実行状態（nodeTimers / runningNodes）だけ
//       個体ごとに木を作ると、同じ敵を10体出したとき同じ JSON を10回、ワーカースレッドから同時に読む
//       返すのは const で、全エンティティで共有するので書き換えない。m_mutex で守るので、どのスレッドから呼んでもよい
// ===========================================================================
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"

class BTAssetCache
{
public:
    static BTAssetCache &Instance();

    // パスに対応する木を返す。初回だけ読み込み、以降は同じものを返す。読めなければ nullptr
    // 一度失敗したパスは読みに行かないので、ファイルを直したら Reload を呼ぶ
    std::shared_ptr<const BTAsset> GetOrLoad(const std::string &path);

    // ファイルを読み直して差し替える（エディタで保存した後に使う）。各個体は次のフレームに受け取り直す
    std::shared_ptr<const BTAsset> Reload(const std::string &path);

    // 読み込んだ木と失敗の記録を全部捨てる
    void Clear();

    // 現在キャッシュしている木の数（デバッグ表示用）
    size_t Count() const;

private:
    BTAssetCache()                                = default;
    BTAssetCache(const BTAssetCache &)            = delete;
    BTAssetCache &operator=(const BTAssetCache &) = delete;

    // ファイルから木を読み、キャッシュに入れる（GetOrLoad と Reload の共通部分）。m_mutex をロックして呼ぶ
    std::shared_ptr<const BTAsset> LoadLocked(const std::string &path);

    mutable std::mutex                                              m_mutex;  // m_assets と m_failed を守る
    std::unordered_map<std::string, std::shared_ptr<const BTAsset>> m_assets; // パス → 読み込んだ木
    std::unordered_set<std::string>                                 m_failed; // 読み込みに失敗したパス
};
