#pragma once
// ===========================================================================
// 概要: BT アセット（木の実体）をパスごとに1つだけ持つキャッシュ（シングルトン）。
//
// 責務:
//   - パスに対応する木を初回だけ読み込み、以降は同じものを返す
//   - 読み込みに失敗したパスを記録し、再試行しない
//   - 指定したパスを読み直す（Reload）
//
// 備考:
//   木はアセットで、個体の持ち物ではない。個体が持つのは実行状態
//   （nodeTimers / runningNodes）だけでよい。個体ごとに木を作ると、同じ敵を
//   10体出したとき同じ JSON を10回パースし、それが並列の ForEach の中に入ると
//   ファイル I/O がワーカースレッドから同時に走る。
//
//   BehaviorTreeSystem が毎フレーム、並列ループの前に GetOrLoad を呼んで配る。
//   返すのは const。全エンティティで共有するので、実行中に書き換えてはいけない。
//   m_mutex で守るので、どのスレッドから呼んでもよい。
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

    /**
     * @brief パスに対応する木を返す。初回だけ読み込み、以降は同じものを返す（失敗なら nullptr）。
     *
     * 失敗したパスは記録して、二度と読みに行かない。毎フレーム再試行するとログが埋まって
     * 他の警告が見えなくなるため。直したら Reload を呼ぶ。
     */
    std::shared_ptr<const BTAsset> GetOrLoad(const std::string &path);

    /**
     * @brief ファイルを読み直して差し替える。エディタで木を保存した後に使う。
     * @param path 読み直す BT ファイルのパス。
     * @return 読み直した木。パスが空・読めなかったときは nullptr。
     * @note 各エンティティは次のフレームの BehaviorTreeSystem の先頭で受け取り直す。
     */
    std::shared_ptr<const BTAsset> Reload(const std::string &path);

    // 全部捨てる。シーンの切り替えなどで使う想定。
    void Clear();

    // 現在キャッシュしている木の数（デバッグ表示用）。
    size_t Count() const;

private:
    BTAssetCache()                                = default;
    BTAssetCache(const BTAssetCache &)            = delete;
    BTAssetCache &operator=(const BTAssetCache &) = delete;

    /**
     * @brief ファイルから木を読み、キャッシュに入れる。GetOrLoad と Reload の共通部分。
     * @param path 読み込む BT ファイルのパス。
     * @return 読み込んだ木。読めなければ nullptr（失敗の記録を残す）。
     * @note m_mutex をロックした状態で呼ぶこと。
     */
    std::shared_ptr<const BTAsset> LoadLocked(const std::string &path);

    mutable std::mutex                                              m_mutex;
    std::unordered_map<std::string, std::shared_ptr<const BTAsset>> m_assets;
    std::unordered_set<std::string>                                 m_failed; // 読み込みに失敗したパス
};
