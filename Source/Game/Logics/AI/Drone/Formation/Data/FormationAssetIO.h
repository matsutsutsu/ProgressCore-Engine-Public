#pragma once
// ===========================================================================
// 概要: 陣形アセット（Assets/Formations/*.json）の読み書きと検証。
//
// 責務:
//   - JSON と FormationAsset の相互変換（形の意味は扱わない）
//   - 範囲外の値を安全な既定値へ直す（Validate）
//
// 備考:
//   どの項目をどのキーで書くかは FormationAssetMeta.h の表が決める。
//   表に載らない2つ（layers の配列と、各層の curve）だけを手で扱う。
//
//   enum は数値ではなく名前で保存する（手で読み書きできるアセットにするため）。
//   入口を Load / Save に絞り、Validate を通らないアセットを作れないようにする。
// ===========================================================================
#include <string>
#include <vector>

#include <json_fwd.hpp>

#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Game/Logics/AI/Drone/Formation/Data/FormationAsset.h"

namespace CCL::AI::Formation
{

    class FormationAssetIO
    {
    public:
        /**
         * @brief JSON を読んで asset を組み立てる。
         *
         * 値の範囲が不正なだけなら既定値へ直して true を返す（ERROR ログでどこが悪いか分かる）。
         *
         * @param virtualPath 読むファイル。
         * @param outAsset    組み立てたアセットを受け取る。
         * @return ファイルが無い・壊れているときだけ false。
         */
        static bool Load(const std::string &virtualPath, FormationAsset &outAsset);

        // エディタからの保存。書き出す前に Validate を通すので、保存したものは必ず読める
        static bool Save(const std::string &virtualPath, const FormationAsset &asset);

        /**
         * @brief 範囲外の値を安全な既定値へ直す。直した箇所は ERROR で出す（WARN は Debug 以外で消えるため）。
         * @param asset   直すアセット。
         * @param context ログに出す文脈（ファイルパス等）。空でもよい
         * @return 1箇所でも直したら true
         */
        static bool Validate(FormationAsset &asset, const std::string &context);

    private:
        static nlohmann::json ToJson(const FormationAsset &asset);
        static void           FromJson(const nlohmann::json &j, FormationAsset &asset);

        // 表の1つぶんを obj から j へ書く
        static void WriteFields(nlohmann::json &j, const std::vector<FieldDescriptor> &fields, const void *obj);

        // 表の1つぶんを j から obj へ読む。キーが無い項目は既定値のまま残す
        static void ReadFields(const nlohmann::json &j, const std::vector<FieldDescriptor> &fields, void *obj);

        // float を最短の10進表記の double にする（そのままだと 0.8 が 0.800000011920929 と出る）
        static double ShortestDouble(float v);
    };

} // namespace CCL::AI::Formation
