// 陣形アセット（Assets/Formations/*.json）の読み書きと検証。
//
// 責務: ディスク上の表現と FormationAsset の相互変換だけ。形の意味は知らない。
//       BehaviorTreeData / BehaviorTreeLoader と同じ分け方で、理由も同じ——
//       アセットが壊れていることに気づける場所はここしかないため。
//
// nlohmann::json をヘッダへ出さない理由:
//   このヘッダはエディタとランタイムの両方が持つ。to_json/from_json を公開すると
//   「どこでも陣形を直接 JSON 化できる」ことになり、検証を通らない経路が生まれる。
//   入口を Load/Save の2つに絞れば、検証は必ず通る。
#pragma once
#include <string>

#include "Game/Logics/AI/Drone/Formation/FormationAsset.h"

namespace CCL::AI::Formation
{

class FormationAssetIO
{
  public:
    /// JSON を読んで asset を組み立てる。ファイルが無い・壊れている場合のみ false。
    /// 値の範囲が不正なだけなら既定値へ直して true を返す（その技だけ静止するより、
    /// 直った形が出て ERROR ログでどのファイルの何が悪いか分かる方が復帰が早い）。
    static bool Load(const std::string &virtualPath, FormationAsset &outAsset);

    /// エディタからの保存。書き出す前に Validate を通すので、保存したものは必ず読める。
    static bool Save(const std::string &virtualPath, const FormationAsset &asset);

    /// 範囲外の値を安全な既定値へ直す。直した箇所は ERROR で出す。
    ///
    /// WARN ではなく ERROR なのは、Development / Shipping では WARN と INFO が
    /// ((void)0) に潰れて見えなくなるため。作者が直すべき間違いは常に見える必要がある。
    ///
    /// @param  context ログに出す文脈（ファイルパス等）。空でもよい
    /// @return 1箇所でも直したら true
    static bool Validate(FormationAsset &asset, const std::string &context);
};

} // namespace CCL::AI::Formation
