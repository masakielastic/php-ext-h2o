# PHP拡張 `h2o` 開発計画（`sample.c` ベース）

## 1. 目的
- `sample.c` の h2o + libuv + OpenSSL 構成を、PHP拡張（PECL/PIE 形式）として移植する。
- 最小限の HTTP サーバー機能を提供する。
- PIE でインストール可能なパッケージ構成にする（`composer.json` の `build-path` は `ext`）。

## 2. スコープ（最小実装）
- PHP 関数:
  - `h2o_server_run(array $options = []): void`
- 実装する機能:
  - TCP listen（デフォルト `0.0.0.0:8080`）
  - HTTP リクエスト受信（method/path/headers/body-size を取得）
  - 固定レスポンス返却（`200 OK`, `text/plain`）
- オプション（最小）:
  - `host`, `port`, `response_body`
  - `tls_cert`, `tls_key`（指定時のみ TLS 有効）
- 初期ターゲット:
  - CLI SAPI での利用（ブロッキング実行）

## 3. 非スコープ（初期版では実施しない）
- マルチワーカー管理
- PHP コールバックによる動的レスポンス
- 高度なルーティング
- Windows 向け最適化
- ZTS サポート（NTS 優先）

## 4. `sample.c` からの移植マップ
- `dump_req`:
  - C内部のデバッグログ関数として `ext/h2o_server.c` に移植
- `on_req`:
  - 拡張内部ハンドラ `php_h2o_on_req` として移植
  - 固定レスポンス + `Content-Type` 設定
- `setup_ssl`:
  - `tls_cert`/`tls_key` 指定時に実行
  - ALPN/NPN 設定は利用可能なら有効化
- `on_uv_connection`:
  - 受け入れ処理をそのまま分離移植
- `main`:
  - `h2o_server_run()` に置換

## 5. ディレクトリ設計（`ext` ビルド）
- `ext/config.m4`
- `ext/config.w32`（雛形のみ）
- `ext/php_h2o.h`
- `ext/h2o.c`（PHP関数エントリ、MINIT/MINFO）
- `ext/h2o_server.c`（h2o/libuv/OpenSSL 連携）
- `ext/h2o_server.h`
- `ext/tests/*.phpt`

## 6. 実装フェーズ
1. フェーズ1: ビルド骨格
- `config.m4` で `h2o`, `uv`, `ssl`, `crypto` を検出
- `phpize && ./configure && make` が通る最小拡張を作成

2. フェーズ2: サーバー起動
- `h2o_server_run()` から libuv loop を起動
- host/port オプションを反映

3. フェーズ3: リクエスト処理
- `sample.c` 相当の `on_req` を移植
- 固定レスポンスを返却

4. フェーズ4: TLS（任意）
- cert/key が指定された場合のみ SSL 初期化
- HTTP/2 ALPN を有効化

5. フェーズ5: テストと安定化
- PHPT: 起動、応答コード、レスポンス本文、TLS（任意）
- エラー時のメモリ解放と終了処理を確認

## 7. テスト計画
- 単体（PHPT）:
  - 関数存在確認
  - 不正オプション時の例外/警告
- 結合:
  - `curl http://127.0.0.1:8080/` で 200 応答
  - TLS 有効時に `curl -k https://127.0.0.1:8443/` 応答
- 回帰:
  - 連続リクエスト時のクラッシュ・リーク確認

## 8. PIE 対応方針
- ルート `composer.json`:
  - `type: php-ext`
  - `php-ext.extension-name: h2o`
  - `php-ext.build-path: ext`
- インストール例（ローカル開発）:
  - `pie install`

## 9. 受け入れ条件（MVP）
- `pie install` でビルドが開始できる
- 拡張をロード後、`h2o_server_run()` が利用できる
- HTTP リクエストに対して `200 OK` を返す
- `ext/tests` の主要 PHPT が通る

## 10. 想定リスクと対策
- 依存ライブラリのパス差異:
  - `configure-options` で prefix 指定可能にする
- イベントループの終了制御:
  - SIGINT で停止できるハンドラを実装
- ABI/バージョン差異:
  - `h2o` API差分は `#if` ガードで吸収
