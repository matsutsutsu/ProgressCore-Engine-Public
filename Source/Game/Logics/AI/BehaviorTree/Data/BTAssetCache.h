// BTアセット（木の実体）をパス単位で1つだけ持つキャッシュ。
//
// 木は「アセット」であって「個体の持ち物」ではない。個体が持つのは実行状態
// （nodeTimers / runningNodes）だけでよい。以前は個体ごとに木を作っていたので、
// 同じ敵を10体出すと同じJSONを10回パースし、しかもその処理が並列ForEachの内側に
// あったためファイルI/Oがワーカースレッドから同時に走っていた。
//
// BehaviorTreeSystem が毎フレーム、並列ループの手前で GetOrLoad を呼んで配る。
// 返すのは const。全Entityで共有する実体なので、実行時に書き換えてはいけない。
#pragma once
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

    /// パスに対応する木を返す。初回だけ読み込み、以降は同じものを返す（失敗なら nullptr）。
    /// 失敗したパスは記録して二度と読みに行かない。毎フレーム再試行するとログが埋まって
    /// 他の警告が見えなくなるため。直したら Reload を通す。
    std::shared_ptr<const BTAsset> GetOrLoad(const std::string &path);

    /// ファイルを読み直して差し替える。エディタで木を保存した後に使う。
    /// 各エンティティは次のフレームの BehaviorTreeSystem の先頭で受け取り直す。
    std::shared_ptr<const BTAsset> Reload(const std::string &path);

    /// 全部捨てる。シーン切り替えなどで使う想定。
    void Clear();

    /// 現在キャッシュしている木の数（デバッグ表示用）。
    size_t Count() const;

private:
    BTAssetCache() = default;
    BTAssetCache(const BTAssetCache &)            = delete;
    BTAssetCache &operator=(const BTAssetCache &) = delete;

    /// 実際に読む。m_mutex を持った状態で呼ぶこと。
    std::shared_ptr<const BTAsset> LoadLocked(const std::string &path);

    mutable std::mutex                                            m_mutex;
    std::unordered_map<std::string, std::shared_ptr<const BTAsset>> m_assets;
    std::unordered_set<std::string>                               m_failed;
};
