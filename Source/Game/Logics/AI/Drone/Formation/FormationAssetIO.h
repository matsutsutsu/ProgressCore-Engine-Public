// 陣形アセット（Assets/Formations/*.json）の読み書きと検証。
//
// 責務: ディスク上の表現と FormationAsset の相互変換だけ。形の意味は知らない。
//       どの項目をどのキーで書くかは FormationAssetMeta.h の表が決め、ここはそれを回すだけ。
//
// 入口を Load / Save の2つに絞っているのは、検証を必ず通すため。
// JSON を直接組み立てる経路を外へ出すと、Validate を通らないアセットが作れてしまう。
#pragma once
#include <string>
#include <vector>

#include <json_fwd.hpp>

#include "Engine/Serialization/Meta/ComponentMeta.h"
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

    private:
        static nlohmann::json ToJson(const FormationAsset &asset);
        static void           FromJson(const nlohmann::json &j, FormationAsset &asset);

        /// 表の1つぶんを obj から j へ書く。enum は数値でなく名前で書く。
        static void WriteFields(nlohmann::json &j, const std::vector<FieldDescriptor> &fields, const void *obj);

        /// 表の1つぶんを j から obj へ読む。キーが無い項目は既定値のまま残す。
        static void ReadFields(const nlohmann::json &j, const std::vector<FieldDescriptor> &fields, void *obj);

        /// float を「float として元に戻せる最短の10進表記」の double にする。
        /// JSON の数値は double なので、そのまま渡すと 0.8 が 0.800000011920929 と出る。
        static double ShortestDouble(float v);
    };

} // namespace CCL::AI::Formation
