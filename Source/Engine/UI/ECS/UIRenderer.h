#pragma once
// ===========================================================================
// 概要: UIDrawList を 2D パスで描くクラス UIRenderer（画像は Sprite、ゲージは UIGaugeSprite、文字は TextRenderer）。
//
// 責務:
//   - 描画アイテムを sortOrder 昇順に描き、加算合成のものは最後にまとめて描く
//   - 描き終えたら描画リストをクリアする
//   - Sprite をパスごとにキャッシュする（texturePath が空なら白テクスチャで単色塗り）
//
// 備考: BaseScene::Render() の 2D パス（ブレンド・深度の設定済み）から Flush を呼ぶ。
// ===========================================================================
#include <d3d11.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Sprite;
class UIGaugeSprite;
class TextRenderer;
class RenderState;
struct UIDrawList;
struct UIDrawItem;

class UIRenderer
{
public:
    // Sprite/TextRenderer が前方宣言（unique_ptr）のため特殊メンバは .cpp で定義する。
    UIRenderer();
    ~UIRenderer();

    /**
     * @brief 描画リストを sortOrder 昇順で描画し、末尾でリストをクリアする（2Dパス内から呼ぶ）。
     * @param renderState 加算合成へ切り替えるために使う。nullptr なら additive 指定を無視して
     *        呼び出し側が設定済みの合成のまま1巡で描く（発光しないが消えはしない）。
     */
    void Flush(ID3D11DeviceContext *dc, ID3D11Device *device, UIDrawList &list,
               RenderState *renderState = nullptr);

private:
    // DrawItems がどのアイテムを描くか
    enum class BlendPass
    {
        All,      // additive を無視して全部（合成を切り替えられない時のフォールバック）
        Normal,   // additive == false のものだけ
        Additive, // additive == true  のものだけ
    };

    // 並び替え済みのアイテム列から、pass に該当するものだけを描く
    void DrawItems(ID3D11DeviceContext *dc, ID3D11Device *device,
                   const std::vector<const UIDrawItem *> &ordered, BlendPass pass);

    Sprite       *GetWhite(ID3D11Device *device);                            // 単色塗り用の白テクスチャ（初回に生成）
    Sprite       *GetSprite(ID3D11Device *device, const std::string &path); // 空パスなら白テクスチャ
    TextRenderer *GetText(ID3D11Device *device);                            // 初回に初期化
    UIGaugeSprite *GetGauge(ID3D11Device *device);                          // 初回に生成

    std::unique_ptr<Sprite>        m_white; // 単色塗り用
    std::unique_ptr<UIGaugeSprite> m_gauge; // ゲージ用（全ゲージで1つを使い回す）

    // テクスチャ別のキャッシュ。値が null のエントリは「そのパスは読めなかった」印で、
    // 毎フレームの再読み込みと警告を止めるために残す（描画には白テクスチャを使う）
    std::unordered_map<std::string, std::unique_ptr<Sprite>> m_cache;
    std::unique_ptr<TextRenderer>                            m_text;              // フォント（初回に初期化）
    bool                                                     m_textTried = false; // 初期化を試みたか（失敗しても再試行しない）
};
