// BTアセット（木の実体）をパス単位で1つだけ持つキャッシュ。
//
// なぜ要るか:
//   以前は BehaviorTreeComponent が自分で make_shared<BTAsset>() して
//   LoadFromJson を呼んでいた。フィールド名は sharedAsset だが、実際には
//   エンティティごとに別の木を持っていて、何も共有していなかった。
//   同じ種類の敵を10体出せば、同じJSONを10回パースし、木を10個抱える。
//   しかもその処理は BehaviorTreeSystem の並列ForEachの内側にあったので、
//   ファイルI/Oとロガー呼び出しがワーカースレッドから同時に走っていた。
//
//   敵AIを本格的に増やす前提では、木は「アセット」であって「個体の持ち物」ではない。
//   個体が持つのは実行状態（nodeTimers / runningNodes）だけでよい。
//
// 使い方:
//   BehaviorTreeSystem が毎フレーム、並列ループの手前（＝メインスレッド）で
//   GetOrLoad を呼び、返ってきた shared_ptr を各コンポーネントへ配る。
//
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

    /**
     * パスに対応する木を返す。初回だけ読み込み、以降は同じものを返す。
     * @return 読み込めなければ nullptr。
     * @note   失敗したパスは記録して二度と読みに行かない。毎フレーム再試行すると、
     *         パスを1つ間違えただけでログが埋まり、他の警告が見えなくなるため。
     *         直したら Reload（インスペクタの「AI再読込」）を通す。
     */
    std::shared_ptr<const BTAsset> GetOrLoad(const std::string &path);

    // ファイルを読み直して差し替える。エディタで木を保存した後に使う。
    //
    // 差し替えるのはキャッシュの中身。各エンティティは次のフレームの
    // BehaviorTreeSystem の先頭で、新しいポインタを受け取り直す。
    std::shared_ptr<const BTAsset> Reload(const std::string &path);

    // 全部捨てる。シーン切り替えなどで使う想定。
    void Clear();

    // 現在キャッシュしている木の数（デバッグ表示用）。
    size_t Count() const;

private:
    BTAssetCache() = default;
    BTAssetCache(const BTAssetCache &)            = delete;
    BTAssetCache &operator=(const BTAssetCache &) = delete;

    // 実際に読む。m_mutex を持った状態で呼ぶこと。
    std::shared_ptr<const BTAsset> LoadLocked(const std::string &path);

    mutable std::mutex                                            m_mutex;
    std::unordered_map<std::string, std::shared_ptr<const BTAsset>> m_assets;
    std::unordered_set<std::string>                               m_failed;
};
