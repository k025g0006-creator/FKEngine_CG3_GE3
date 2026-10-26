# 旧ファイルの保管場所

整理前のコピーと設定を、内容を削除せずに保管しています。現在のソリューションのビルドには含まれません。

| フォルダ | 元の場所・内容 |
| --- | --- |
| `LegacyEngine` | ルート直下の`FKEngine`にあった旧コピー |
| `LegacyFKAlpha` | ルート直下の`FKAlpha`に残っていた生成ファイル |
| `Submission_LE2C_22` | `LE2C_22_フジイ_ケンノスケ`の提出用コピー |
| `ImGuiExamples_1.89.4` | 未使用の`imgui-1.89.4`サンプル一式 |
| `DirectXTexProjects` | 未使用のプラットフォーム別プロジェクト設定 |
| `NuGetCache` | 使用しなくなったNuGetパッケージのローカルキャッシュ |
| `LegacySettings` | 旧packages.configと誤記のあった空の辞書 |

旧`FKAlpha`はGit上でサブモジュール参照（`05e9dd1c31fc3ccf2868e755abf436af51fd3938`）として登録されていましたが、ローカルには`.git`やソースのチェックアウトがなく、ビルド出力だけが残っていました。壊れたプロジェクト参照は使用中の`externals/DirectXTex`への参照に置き換えています。

ルートにあった`x64`、DirectXTexの`Bin`と生成シェーダーは、プロジェクト外の`../FKEngine_Build/Legacy`へ、過去のログとダンプは`../FKEngine_Build/Logs`・`../FKEngine_Build/Dumps`へ移しています。
