# FKEngine

| ブランチ | Debug | Development | Release |
| --- | --- | --- | --- |
| main | [![Debug build](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-debug.yml/badge.svg?branch=main&event=push)](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-debug.yml?query=branch%3Amain) | [![Development build](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-development.yml/badge.svg?branch=main&event=push)](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-development.yml?query=branch%3Amain) | [![Release build](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-release.yml/badge.svg?branch=main&event=push)](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-release.yml?query=branch%3Amain) |

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

## GitHub Actionsの自動ビルド

コミットをpushすると、GitHubの新しいWindows環境でDebug・Development・Releaseをそれぞれビルドします。プルリクエストでも同じ確認を実行します。これらの設定を含むmainから作業ブランチを作成するか、既存のGE3・CG3へmainをマージしてから使ってください。

1. ファイルを編集し、必要なソース・モデル・画像などをコミットしてpushします。
2. GitHubの[Actions](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions)で結果を確認します。
3. 成功するとREADME先頭のバッジが`passing`、失敗すると`failing`になります。バッジをクリックすると該当構成の実行履歴を開けます。

各実行では、プロジェクト参照ファイルの存在、競合マーカーの残存、C++のビルド、4本のHLSLのコンパイル、実行フォルダへの素材のコピーを確認します。ローカルの生成済みファイルは使いません。必要なファイルを`.gitignore`で除外したままpushすると、この確認で検出できます。実機での描画確認にはPC側の`--smoke-test`を使ってください。

成功した実行のページ下部のArtifactsから、構成別の実行ファイル・素材一式とビルドログを取得できます。保存期間は7日です。

共通処理は`.github/workflows/windows-build.yml`と`Tools/Build-CI.ps1`にあり、構成ごとのワークフローから呼び出します。実行環境は`windows-2025-vs2026`（Visual Studio 2026 / v145）です。

ローカルでもPowerShell 7から同じ確認ができます。

```powershell
./Tools/Build-CI.ps1 -Configuration Debug -OutputRoot C:\FKEngine_Build\CI-Local
```

バッジの表示方法は[GitHub公式ドキュメント](https://docs.github.com/en/actions/how-tos/monitor-workflows/add-a-status-badge)に従っています。初回の実行が完了するまでは、バッジに結果が表示されないことがあります。
