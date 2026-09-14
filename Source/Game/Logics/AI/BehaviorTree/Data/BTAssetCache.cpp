// BTアセットキャッシュの実装。
//
// 注意点:
//   設計の理由と使い方は BTAssetCache.h に書いてある。ここには書かない
//   （同じ説明が2箇所にあると、片方だけ古くなって嘘になる）。
#include "BTAssetCache.h"

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeLoader.h"
#include "Engine/Platform/Core/Logger.h"

BTAssetCache &BTAssetCache::Instance()
{
    static BTAssetCache cache;
    return cache;
}

std::shared_ptr<const BTAsset> BTAssetCache::GetOrLoad(const std::string &path)
{
    if (path.empty()) return nullptr;

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_assets.find(path);
    if (it != m_assets.end()) return it->second;

    // 失敗済みは黙って諦める（ログは初回の1回だけ出してある）
    if (m_failed.count(path)) return nullptr;

    return LoadLocked(path);
}

std::shared_ptr<const BTAsset> BTAssetCache::Reload(const std::string &path)
{
    if (path.empty()) return nullptr;

    std::lock_guard<std::mutex> lock(m_mutex);

    // 失敗の記録も消して、m_assets と m_failed に同じパスが入らない状態を保つ。
    //
    //   消し忘れても読み直し自体は成功する。Reload は GetOrLoad を経由せず
    //   LoadLocked を直接呼ぶので、m_failed の関所（毎フレーム再試行しないための
    //   足切り）を通らないため。
    //   ただし両方にパスが残り、「m_assets を先に見る」という GetOrLoad の順番に
    //   依存して動いているだけの状態になる。順番を入れ替えた瞬間に
    //   「直したのに読まれない」が本当に起きる。
    m_assets.erase(path);
    m_failed.erase(path);

    return LoadLocked(path);
}

std::shared_ptr<const BTAsset> BTAssetCache::LoadLocked(const std::string &path)
{
    auto asset = std::make_shared<BTAsset>();
    if (!BehaviorTreeLoader::LoadFromJson(path, *asset)) {
        m_failed.insert(path);
        CCL_LOG_WARN(LogCategory::AI,
            "[BTAssetCache] 読み込みに失敗したので、このパスは以降スキップする: %s "
            "(直したらインスペクタの「AI再読込」を押す)", path.c_str());
        return nullptr;
    }

    std::shared_ptr<const BTAsset> shared = asset;
    m_assets[path] = shared;
    return shared;
}

void BTAssetCache::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_assets.clear();
    m_failed.clear();
}

size_t BTAssetCache::Count() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_assets.size();
}
