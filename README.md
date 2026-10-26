# FKEngine

`FKAlpha.slnx`をVisual Studioで開いて使用します。構成は`Debug`・`Development`・`Release`、プラットフォームは`x64`です。現在のツールセットは`v145`です。

## フォルダ構成

| 場所 | 内容 |
| --- | --- |
| `Source/App` | アプリケーションの開始処理とシーン（main.cpp） |
| `Source/Engine/Core` | ウィンドウ、ログ、文字列処理 |
| `Source/Engine/Graphics` | DirectX基盤と描画リソース |
| `Source/Engine/Input` | 入力処理 |
| `Source/Engine/Audio` | 音声処理 |
| `Source/Engine/Math` | ベクトル・行列 |
| `Source/Engine/Debug` | デバッグカメラ、リソースリーク確認 |
| `Resources` | モデル、テクスチャ、音声などの素材 |
| `Resources/Shaders` | HLSLシェーダーと共通定義 |
| `externals` | 使用中の外部ライブラリ（DirectXTex・ImGui） |
| `Config` | 共通ビルド設定、ImGuiの配置設定、スペルチェック辞書 |
| `../FKEngine_Build` | プロジェクト外に置く実行ファイル、静的ライブラリ、中間ファイル、ログ |
| `Archive` | 旧コピー、提出用フォルダ、旧設定の保管場所 |

## ビルドと実行

- 共通出力先：`../FKEngine_Build/`（現在の配置では`C:\FKEngine_Build`）
- 実行ファイル：`../FKEngine_Build/Bin/x64/<構成>/FKAlpha.exe`
- 外部ライブラリ：`../FKEngine_Build/Lib/x64/<構成>/DirectXTex.lib`、Debugでは`ImGui.lib`も生成
- 中間ファイル：`../FKEngine_Build/Obj/x64/<構成>/<プロジェクト>/`
- DirectXTexの生成シェーダー：`../FKEngine_Build/Generated/DirectXTex/x64/<構成>/`
- 実行時ログ・クラッシュダンプ：実行ファイルの隣の`Logs/`・`Dumps/`

出力先は`Config/BuildSettings.props`の`FKEngineOutputRoot`で一括設定しています。プロジェクトをコピー・提出する際は、隣の`FKEngine_Build`を含める必要はありません。整理前の生成物と過去のログも同フォルダへ引き継いでいます。

Visual Studioでの作業ディレクトリはプロジェクトのルートです。ビルド後には、必要な素材・シェーダー・DXCのDLLが実行ファイルの隣にもコピーされます。実行時のImGui設定は実行ファイル側の`Config/imgui.ini`へ保存します。実行フォルダを別の場所へ渡すときは、`Resources`と`Config`、DLLを含めてコピーしてください。

DirectXTexとImGuiはソリューション内の静的ライブラリプロジェクトとしてビルドします。アプリケーションはプロジェクト参照でリンクし、DirectXTex用の旧NuGetパッケージは使用しません。ImGuiはアプリケーションのDebug構成で有効になります。

新しいC++ファイルは担当する`Source`配下に置いてプロジェクトへ登録し、ソリューションエクスプローラーでも同じフォルダのフィルターへ追加してください。

## GE3・CG3とmainの使い分け

mainを動作確認済みの共通版として使います。GE3のクラス化・入力処理・フォルダ整理と、CG3のパーティクル・地面・各種ライトを共存させています。日本語のImGuiでライトや減衰を調整できます。

1. 作業前にmainを最新にし、新しい作業はmainから分けたブランチで進めます。
2. 既存のGE3・CG3で続きを作る場合は、最初にmainをその作業ブランチへマージします。これにより整理後のファイル配置とクラス経由の呼び出しを引き継げます。
3. 変更をコミットしたらmainへマージし、競合は両方の機能が残るように解消します。
4. Debug・Development・Releaseをビルドし、実行時に使うHLSLも確認してからmainをpushします。

ソースは`Source/App/main.cpp`、シェーダーは`Resources/Shaders`が使用中のファイルです。`Archive`内の旧コピーを編集しても実行結果には反映されません。

起動と描画の確認には`FKAlpha.exe --smoke-test`も使えます。3フレーム描画後に通常の終了処理で終了し、実行ファイル横の`Logs`へ確認結果を残します。このモードでは確認中の音声再生を省きます。
