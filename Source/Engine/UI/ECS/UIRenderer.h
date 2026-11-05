#pragma once
// ===========================================================================
// 概要: UIDrawList を 2D パスで描くクラス UIRenderer
//       画像は Sprite、ゲージは UIGaugeSprite、文字は TextRenderer で描く
//
// 責務:
//   - 描画アイテムに画面全体の不透明度を掛け、sortOrder の昇順に描く
//   - 加算合成のアイテムは、通常合成のアイテムをすべて描いた後にまとめて描く
//   - 描き終えたら描画リストを空にする
//   - Sprite をテクスチャのパスごとにキャッシュする（パスが空なら白テクスチャで単色の四角形）
//
// 備考: BaseScene::Render() の 2D パス（合成・深度の設定済み）から Flush を呼ぶ
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
    // Sprite / TextRenderer を前方宣言のまま unique_ptr で持つので、コンストラクタとデストラクタは .cpp で定義する
    UIRenderer();
    ~UIRenderer();

    // 描画リストを sortOrder の昇順に描き、最後に items を空にする。BaseScene::Render の 2D パスから呼ぶ
    // renderState が nullptr なら加算に切り替えず、今の合成のまま全部描く（光らないが消えはしない）
    void Flush(ID3D11DeviceContext *dc, ID3D11Device *device, UIDrawList &list,
               RenderState *renderState = nullptr);

private:
    // DrawItems が描くアイテムの種類
    enum class BlendPass
    {
        All,      // additive に関係なく全部（合成を切り替えられないとき）
        Normal,   // additive が false のものだけ
        Additive, // additive が true のものだけ
    };

    // 並べ替え済みのアイテムのうち、pass の種類のものだけを描く
    void DrawItems(ID3D11DeviceContext *dc, ID3D11Device *device,
                   const std::vector<const UIDrawItem *> &ordered, BlendPass pass);

    Sprite        *GetWhite(ID3D11Device *device);                            // 単色の四角形用の白テクスチャ（最初の呼び出しで作る）
    Sprite        *GetSprite(ID3D11Device *device, const std::string &path); // パスのテクスチャ。空・読めないパスなら白テクスチャ
    TextRenderer  *GetText(ID3D11Device *device);                            // フォント（最初の呼び出しで初期化）。失敗なら nullptr
    UIGaugeSprite *GetGauge(ID3D11Device *device);                          // ゲージの描画（最初の呼び出しで作る）

    std::unique_ptr<Sprite>                                  m_white;             // 単色の四角形用
    std::unique_ptr<UIGaugeSprite>                           m_gauge;             // ゲージ用。全ゲージで1つを使い回す
    std::unordered_map<std::string, std::unique_ptr<Sprite>> m_cache;             // テクスチャのパスごとの Sprite。null は読めなかったパス
    std::unique_ptr<TextRenderer>                            m_text;              // フォント
    bool                                                     m_textTried = false; // フォントの初期化を試みたか
};
