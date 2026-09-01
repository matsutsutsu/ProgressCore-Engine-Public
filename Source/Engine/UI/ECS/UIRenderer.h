#pragma once
// UIDrawList を2Dパスで実際に描画するヘルパー（画像=Sprite / 文字=TextRenderer）。
//
// 設計方針:
//   - BaseScene::Render() の2Dパス（blend/depth 設定済み）から Flush を呼ぶ。
//   - 画像/テキストを sortOrder 昇順で一括描画し、末尾で描画リストをクリアする（消費時クリア）。
//   - Sprite はパス単位でキャッシュ。texturePath 空は白ダミーでSolid塗り。
#include <d3d11.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Sprite;
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
    // DrawItems がどのアイテムを描くか。
    enum class BlendPass
    {
        All,      // additive を無視して全部（合成を切り替えられない時のフォールバック）
        Normal,   // additive == false のものだけ
        Additive, // additive == true  のものだけ
    };

    // 並び替え済みのアイテム列から、pass に該当するものだけを描く。
    void DrawItems(ID3D11DeviceContext *dc, ID3D11Device *device,
                   const std::vector<const UIDrawItem *> &ordered, BlendPass pass);

    Sprite       *GetSprite(ID3D11Device *device, const std::string &path); // 空パス=白ダミー
    TextRenderer *GetText(ID3D11Device *device);                            // 遅延初期化

    std::unique_ptr<Sprite>                                  m_white; // 単色塗り用
    std::unordered_map<std::string, std::unique_ptr<Sprite>> m_cache; // テクスチャ別
    std::unique_ptr<TextRenderer>                            m_text;  // フォント（遅延初期化）
    bool                                                     m_textTried = false;
};
