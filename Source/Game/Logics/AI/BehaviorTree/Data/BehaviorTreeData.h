// ビヘイビアツリーの静的データ構造。
//
// 子をポインタではなく配列の添字(BTNodeID)で持つ。理由は3つ。
//   ・JSONにそのまま書ける。ポインタは保存できないので、ポインタ木にすると
//     保存と読み込みのたびに「ポインタ⇔添字」の変換が要る
//   ・ノードIDがそのまま添字になるので、個体ごとの実行状態を木と同じ長さの
//     配列（BehaviorTreeComponent の nodeTimers / runningNodes）で持てる
//   ・ノード本体の確保が「ノードの数だけ」から1回にまとまり、添字の近い
//     ノードが同じキャッシュラインに載る
//
// 誇張しないための但し書き。children は std::vector なので、子を辿るときの
// ヒープ参照はノードごとに1回残る（確保回数は N → 1+N であって 1 ではない）。
// また木の走査は深さ優先で添字が飛ぶため、連続アクセスを前提とする
// ハードウェアプリフェッチが効く形ではない。効いているのは空間的局所性の方。
#pragma once
#include <cstdint>
#include <vector>

using BTNodeID = uint16_t; // ノードのインデックス（配列の添字）
using ActionID = uint16_t; // 行動・条件の種類を判別するID
using AssetID  = uint32_t; // BTAsset の種類を判別するID

// デバッグ用のノード状態（実行時の可視化専用）。
enum class BTDebugState : uint8_t
{
    None = 0,
    Success,
    Failure,
    Running
};

// 修飾の種類。アセットが実際に使っているのは Cooldown だけで、
// 残り2つは実装だけがある状態。
//
// 旧 Async(3) は削除した。「子を開始させて手を離す」という作りが、
// currentActionId を毎フレーム書き直して硬直終了時に下ろす行動の実装と
// 噛み合わず、技が終わっても体が動き続けたため。同じ用途は
// BTNodeType::Parallel（主は評価され続ける）で書くこと。
enum class BTDecoratorType : uint8_t
{
    Inverter = 0, // 結果を反転 (Success <-> Failure)。Running は反転しない
    Cooldown = 1, // クールダウン (指定秒数経過するまでFailure)
    Retry    = 2  // 失敗しても指定回数リトライする。
                  //   ただし繰り返すのは「同じフレームの中で」。条件のように
                  //   同一フレームでは結果が変わらない子に付けても意味がない
};

enum class BTNodeType : uint8_t
{
    Selector,  // 順に評価し、最初に成功した子の結果を返す
    Sequence,  // 順に評価し、最初に失敗した子の結果を返す
    Condition, // 条件を評価して成功/失敗を返す
    Action,    // 行動を実行して成功/失敗/実行中を返す
    Decorator, // 子の結果を修飾する
    // 子の weight に比例して1つ引き、成功するまで引き直す。
    //   失敗した枝は候補から外して残りから引き直すので、条件で弾かれる枝が
    //   あっても「出せる技のどれか」が必ず出る。全部の重みを同じにすれば
    //   単純な均等ランダムになる（既定の weight は 1.0）。
    //
    //   このプロジェクトの「ランダムに技を選ぶ」はすべてこのノードに寄せてある。
    //   今後の袋方式（引いた技を一巡するまで出さない）も、条件付きの重み変更も、
    //   このノードの重みの決め方を差し替えるだけで足せる。
    //
    WeightedSelector,

    // 子を並行して動かす。0番目の子が「主」で、その結果がこのノードの結果になる。
    // 1番目以降は「従」で、主が Running の間だけ毎フレーム評価され、結果は捨てられる。
    //
    //   「技を出しながら、その間ドローンにも指示を出す」を書くためのノード。
    //   主は毎フレーム評価され続けるので、毎フレームの後始末を持つ行動
    //   （currentActionId を書く技）をそのまま置ける。削除した Async 修飾は
    //   そこが逆で、手を離した技の要求が下りないまま残る作りだった。
    //
    //   従には「毎フレーム評価されても安いもの」だけを置くこと。主が終わると従は
    //   評価されなくなるので、従に currentActionId を書く行動を置くと、要求が
    //   下りないまま残る（読み込み時に ValidateNodes が警告する）。
    Parallel
};

// ノードの評価結果。Running は Action だけが返しうる（次フレームも同じノードを評価する）。
enum class BTNodeState : uint8_t
{
    Success,
    Failure,
    Running
};

// WeightedSelector が抽選できる子の上限。候補バッファをスタックの固定長で取るため
// （評価はワーカースレッドで走るので、ヒープ確保を持ち込みたくない）。
// 超えた木は読み込み時に警告する。
inline constexpr int kMaxWeightedSelectorChildren = 16;

// 1つのノード定義。既定値は、読み込み側が代入を1つ忘れても不定値を読まないために書く。
struct BTNode
{
    BTNodeType type = BTNodeType::Action;

    ActionID actionOrConditionId = 0;

    // Decorator のときだけ意味を持つ修飾の種類。
    BTDecoratorType decoratorType = BTDecoratorType::Inverter;

    // 数値パラメータ。意味はノードの種類が決める。
    //   Action    … その行動の引数（待機の秒数、近接の硬直など）
    //   Decorator … クールダウン秒数 / リトライ回数
    float param = 0.0f;

    // 親が WeightedSelector のときの「選ばれやすさ」。既定 1.0 で均等。
    //
    //   param と分けてあるのは、param が「そのノード自身の設定」だから。
    //   技の param は技の全体時間で既に埋まっており、そこへ重みを
    //   入れると技の長さが消える。weight は「親から見たときの比」で持ち主が違う。
    //
    //   重みを親に配列で持たせない理由：children と長さがずれうるため。
    //   子が自分で持てば、子を足しても消してもずれようがない。
    //
    //   0 以下にすると抽選の候補から外れる（読み込み時に警告する）。
    float weight = 1.0f;

    // 親（Selector）の下で、上の枝が Running でも割り込んで入ってよいか。
    //
    //   既定 false は「上の枝が終わるまで待つ」＝今までどおり。true を付けた枝だけが、
    //   先頭に並ぶ条件（見張り条件）を毎フレーム見てもらえる。
    //
    //   全部の枝を毎フレーム見直さないのは、しおり（runningNodes）が技の継続そのもの
    //   だから。Selector が Running の子を先に再開するおかげで、出している最中の技が
    //   次のフレームに別の技へ差し替わらずに済んでいる。割り込みはその例外を枝ごとに
    //   開ける仕組みで、既定は閉じている。
    bool interrupt = false;

    // 親（Selector）の下で自分が Running の間、自分の見張り条件を毎フレーム見直すか。
    //
    //   interrupt が「上の枝が下を押しのける」のに対し、こちらは「自分の条件が崩れたら
    //   自分から降りる」。予備動作の最中に間合いを外れたら技を取りやめる、といった用途。
    //   降りた後は Selector が先頭から選び直すので、その場に合った枝が自然に選ばれる。
    bool abortSelf = false;

    std::vector<BTNodeID> children;
};

// 全Entityで共有される不変データ（アセット）。
//
// 実行状態（残り時間・どの子まで進んだか）をここへ置いてはいけない。同じ木を使う
// 個体が1つの変数を奪い合い、片方が始めた待機がもう片方のぶんまで減る。評価は
// 並列で走るので、同じ float への同時書き込みにもなる。
// 状態は個体側（BehaviorTreeComponent）が、ノードIDをそのまま添字にした配列で持つ。
// BTAssetCache が const で配ることで、「書けない」ことを型で守っている。
struct BTAsset
{
    AssetID             id;
    std::vector<BTNode> nodes;
};

/// 割り込みの可否を判定できる形をしているか（＝枝の先頭に条件が並んでいるか）。
///
/// 割り込みたい枝が「今なら通るか」を見るには、行動を実行せずに調べられるものが
/// 先頭に無いといけない。行動は実行＝副作用（アニメのトリガー・要求の書き込み）なので、
/// 「試しに評価してみる」ができない。
///
/// 形の規則をここに1つだけ置き、読み込み時の検査（BehaviorTreeLoader）と実行時の
/// 評価（BehaviorTreeSystem::GuardsPass）の両方から引く。別々に書くと、片方だけ直したとき
/// 「エディタでは警告が出ないのに実行時は割り込まない」という追いにくい食い違いになる。
inline bool HasReadableGuard(const BTAsset &asset, BTNodeID nodeId)
{
    if (nodeId >= asset.nodes.size()) return false;

    const BTNode &node = asset.nodes[nodeId];
    switch (node.type)
    {
    case BTNodeType::Condition: return true;

    // 修飾と手順は「先頭の子」へ降りる。Decorator(Cooldown) → Sequence[条件, …] という
    // よくある形が、そのまま2段降りて条件に行き着く。
    case BTNodeType::Decorator:
    case BTNodeType::Sequence: return !node.children.empty() && HasReadableGuard(asset, node.children[0]);

    default: return false;
    }
}
