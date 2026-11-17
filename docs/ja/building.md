# ソースからのビルド

## Linuxホストの準備

x86-64 LinuxでBash、GNU Make、Node.js/npm、rootless Podmanを使用します。
Node.jsの対象バージョンは各`package.json`の`engines`に従ってください。
不足するホスト用ツールはディストリビューションのパッケージ管理でインストールします。ホスト用MinGWは不要です。

リポジトリーのルートで実行します。

```sh
git submodule update --init --recursive
npm ci
./prereq.sh
npm run build
npm test
```

`agents/deps/cardio`はcardio 1.3.0を固定したサブモジュールです。既存のチェックアウトを更新した場合も初期化してください。

`prereq.sh`は初回にWindowsツールチェーンのイメージを作り、以後は準備済みのイメージを再利用します。
初回はコンテナーレジストリー、Debian snapshot、NSIS公式ソースの取得先へのネットワーク接続が必要です。
通常のビルドと配布処理はネットワークを無効にして実行し、イメージを自動取得・再構築しません。
イメージがない場合は`prereq.sh`の実行を案内します。

Windowsエージェントの出力は`agents/dist/agent-amd64.exe`と`agents/dist/agent-i686.exe`です。
`make -C agents -j4`でも同じ固定イメージを使います。
`make -C agents clean`はビルド生成物を削除し、イメージは残します。

## 配布物の生成

```sh
npm run pack
```

`artifacts/`へSDKパッケージ、Windows用ZIP、NSISインストーラーを生成します。
Windows配布物だけが必要なら`npm run pack --workspace agents`を実行してください。
ZIPの名前は`agent-rover-agent-win32-{version}.zip`、インストーラーは`agent-rover-agent-win32-{version}-setup.exe`です。
バージョンは`screw-up format`から取得し、同じnativeビルドを両方へ収録します。
インストーラーのビルド警告は失敗として扱い、NSIS生成に失敗した場合は新しいZIPも公開しません。

## ツールチェーンの固定

[Containerfile](../../agents/toolchain/Containerfile)はDebianベースイメージをdigestで固定し、パッケージには日付を固定した[Debian snapshot](https://snapshot.debian.org/#Usage)を使います。
クロスツールチェーンはGCC 12.2.0のWin32スレッドモデル、MinGW-w64 10.0.0/MSVCRT、binutils 2.40です。
両アーキテクチャで同じ定義を使うため、ホストのコンパイラー更新は影響しません。
NSISは[公式v3.13ソース](https://github.com/NSIS-Dev/nsis/tree/v313)のSHA-256を検証してビルドします。

Containerfileの内容のハッシュがローカルイメージ名を決めます。
定義を変更した場合は`./prereq.sh`を再実行してください。Makeは影響するオブジェクトを再構築します。
古いイメージは明示的に削除するまで残ります。現在のイメージ名は次のコマンドで確認できます。

```sh
./agents/scripts/windows-toolchain.sh --image
```

Make、npm、バージョン生成はホストで実行します。
コンパイラー、リソースコンパイラー、リンカー、NSIS、PE検査ツールは使い捨てのコンテナーで実行します。
コンテナーはホストユーザーのファイル所有権を維持し、チェックアウトを同じ絶対パスへマウントします。
nativeのテストドライバーも製品と同じツールチェーンでビルドします。

独自の`OBJ_ROOT`、`GENERATED_DIR`、出力先はチェックアウト内へ置いてください。
外部ディレクトリを使う場合は先に作成し、`AGENT_ROVER_BUILD_DIR`へ絶対パスを指定すると追加でマウントできます。

```sh
mkdir -p /tmp/agent-rover-build
AGENT_ROVER_BUILD_DIR=/tmp/agent-rover-build make -C agents -j4 \
  OBJ_ROOT=/tmp/agent-rover-build/obj \
  GENERATED_DIR=/tmp/agent-rover-build/generated \
  TARGET_ROOT=/tmp/agent-rover-build/dist
```

`AMD64_CXX`、`I686_CXX`、`AMD64_RC`、`I686_RC`を上書きすると既定のツールチェーンを使わなくなるため、互換性を別途確認してください。
イメージの保存とrootlessのボリューム動作は[Podman build](https://docs.podman.io/en/v4.9.3/markdown/podman-build.1.html)と[Podman run](https://docs.podman.io/en/v4.9.3/markdown/podman-run.1.html)を参照してください。

## 検証

全体テストはルートの`npm test`で実行します。
nativeビルドテストは配布エージェントのPE importを調べ、想定するシステムDLL以外への依存と、既知のXP以降の必須API importを検出します。
配布テストは実際のpackコマンドを実行し、ZIPとインストーラーの生成、起動、更新失敗時の保持、アンインストールを確認します。
Wineとunzipが必要です。

Windows実機テストでは対話デスクトップで起動した制御用エージェントの接続先を`AGENT_ROVER_WIN11_HOST`、トークンを`AGENT_ROVER_WIN11_TOKEN`へ指定します。
制御用エージェントは0.10.0で検証しています。指定がなければ実機テストはスキップします。
テスト対象は別の配置先へ転送します。テストで使うポートをファイアウォールで許可するか、Windowsから到達できるLinux側IPを`AGENT_ROVER_WIN11_REVERSE_HOST`へ指定してください。
後者はWindows PowerShellと.NETの非同期ストリーム転送を使うWin11向けの検証方法です。

インストーラーの実機テストは標準ユーザーで実行します。
既定のインストール先、プログラムメニュー、自動起動登録、アンインストール登録が未使用であることを確認してから開始します。
既存のインストールを試験用に上書きしません。実際の再ログオンは専用ユーザーまたは仮想マシンで別に確認してください。

対象OSはi686がWindows XP SP2以降、amd64がWindows XP Professional x64 Edition以降です。
PE検査やWin11での成功はXP実行確認の代わりになりません。
XP上で最終インストーラー、エージェントの起動・認証・基本操作を確認する必要があります。
過去の不具合では[Vista以降のGetThreadId](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadid)が必須importになりました。
[GCC 13でWin32スレッド実装が変更された](https://gcc.gnu.org/gcc-13/changes.html)ため、ツール更新時も静的検査と実機確認を続けてください。
