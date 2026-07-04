# agri-canopy-poe

M5Stack PoECAM-W (ESP32-WROVER + W5500 + OV2640) を使った温室
キャノピー撮影ノード。定刻に画像を撮って WebDAV サーバに HTTP PUT で
送るだけの割り切り構成。共通基盤は
[`agri-node-poe-core`](https://github.com/yasunorioi/agri-node-poe-core)、
撮影サイクルは NTP で拾った UTC + `/config` で入力した緯度経度から NOAA
太陽位置式で計算した太陽高度で gate する。

> **v0.3.0 で ADS1110/PVSS-03 の日射計パスを撤去**。現地の ADC ノイズで
> 使い物にならなくなったため、`InRadiation` 送信と UECS-CCM 出力も同時に
> 削除。時刻ベースの daylight gate に一本化してある。

## ハードウェア

- **MCU**: M5Stack PoECAM-W (ESP32-WROVER-B, 8MB PSRAM, 16MB flash)
- **Ethernet (PoE)**: 内蔵 Wiznet W5500 (SPI)
- **Camera**: OV2640 (DVP、SCCB)
- **書き込み**: M5 Writer (USB-C) → programming header

⚠ PoECAM の GPIO 27 は camera XCLK なので、ATOM 系の WS2812 status LED
(agri::Led) は使えない。状態は Serial ログのみ。

## 主な機能

- **DHCP / PoE プラグアンドプレイ** (W5500 SPI SCK=23 MISO=38 MOSI=13 CS=4)
- **agriha スキーマ MQTT publisher**
  - `<prefix>/sensor/CamShot`  (URL string, 直近 upload の閲覧 URL)
- **カメラ撮影 → WebDAV アップロード**
  - デフォルト 30 分間隔 (`cam_interval_s`)
  - 解像度 VGA / SVGA / XGA / HD / SXGA (既定) / UXGA から選択
  - JPEG quality 10 (best) 〜 30 (small)
  - 太陽高度が `sun_elev_min_deg` (既定 5°) 以下の間はスキップ
    (`cam_daylight_only`)
  - PUT URL: `<wd_url>/<hostname>/<YYYY>/<MMDDHHMMSS>.jpg` (JST)
  - MQTT `CamShot` には auth-free の 閲覧 URL を publish
    (`<wd_url>` から末尾 `/upload` を落とした base + tail)
- **Web UI** 3 ページ + JSON API — カメラ設定 / 緯度経度 / WebDAV 認証を編集
- **ArduinoOTA** over Ethernet (hostname `agri-canopy-XX`)
- **GitHub Release self-update** — Dashboard から1クリック更新

## MQTT トピック

`<prefix>` は agriha のハウス分割 (例 `agriha/2`)。全トピック retained。

| トピック | 型 / unit | 説明 |
|---|---|---|
| `<prefix>/sensor/CamShot`     | string `url`  | 直近成功した WebDAV upload の閲覧 URL |

`CamShot` の value は URL 文字列 (JSON では `"value":"http://..."`)。
ArSprout dashboard は `<img src>` 直貼りで表示できる想定。

## WebDAV サーバ側

受け側の nginx 設定と install script は
[`canopy-webdav-server`](https://github.com/yasunorioi/canopy-webdav-server) 参照
(今のところ private / 未公開、`~/canopy-webdav-server/` にローカル保管)。

概要:

```
PUT  http://<server>:<port>/upload/<node>/<YYYY>/<MMDDHHMMSS>.jpg   (Basic auth)
GET  http://<server>:<port>/<node>/<YYYY>/<MMDDHHMMSS>.jpg          (auth 無し)
```

nginx-extras の `ngx_http_dav_module` + `create_full_put_path on` で
中間ディレクトリを自動作成。

## Daylight gate

`sun.h` に NOAA 太陽位置式 (equation of time + declination) を実装。
`time(nullptr)` で UTC epoch → `gmtime_r` → day-of-year + hour_utc →
`gamma → eqtime → decl → hour angle → cos(zenith) → elevation`。
毎分 1 回計算して `g_sun_elev_deg` に保持し、撮影ゲート・Dashboard・
JSON status で共有する。

SNTP 未同期時 (`time(nullptr) < 1700000000`) は NaN を返す。この場合
gate は「通過」扱いにして、boot 直後の撮影を止めないようにしてある
(実運用で1発目が試写になる)。

## 永続化設定 (NVS)

`Preferences` ネームスペース `canopy-cfg`。`/config` から編集:

- **共通** (`agri::CommonConfig`): Node ID / hostname / MQTT host, port, user,
  pass, topic prefix, interval / CCM enable, interval, room, region, priority, ntype
  (CCM は今の firmware では publish しないので inert)
- **Camera**: `cam_en` / `cam_int_s` / `cam_res` / `cam_jq` / `cam_dl_only`
- **Location**: `lat` / `lon` / `sun_elev` (deg)
- **WebDAV**: `wd_url` / `wd_user` / `wd_pass`

既定は MQTT host 空、wd_url 空、camera ON @ 30 min SXGA Q12、
daylight-only ON @ 太陽高度 5° 閾値、lat=35.0 / lon=135.0 (要更新)。

## ビルド・書き込み

```
pio run -e poecam-canopy -t upload                                   # 初回 USB
pio run -e poecam-canopy -t upload --upload-port agri-canopy-01.local  # OTA
```

書き込み後は `http://agri-canopy-01.local/` で UI にアクセス。
Update ボタンから GitHub Release 経由で更新可。

## PoECAM ピンアウト (参考)

`~/poecam-bringup/` で検証済みの、確定ピン一覧:

**W5500** (SPI): SCK=23, MISO=38, MOSI=13, CS=4, RST=-1, INT=-1

**OV2640** (DVP + SCCB):
SIOD=14, SIOC=12, XCLK=27, PCLK=21, VSYNC=22, HREF=26,
D0=32, D1=35, D2=34, D3=5, D4=39, D5=18, D6=36, D7=19,
RESET=15, PWDN=-1
