# agri-canopy-poe

M5Stack PoECAM-W (ESP32-WROVER + W5500 + OV2640) を使った温室
キャノピー撮影ノード。日射計 (PVSS-03) も同居で、日射量を agriha MQTT に、
撮影画像は WebDAV サーバに HTTP PUT で送る。共通基盤は
[`agri-node-poe-core`](https://github.com/yasunorioi/agri-node-poe-core)、
兄弟の [`agri-solar-poe`](https://github.com/yasunorioi/agri-solar-poe) の
ADS1110 読み出し + [`agri-env-poe`](https://github.com/yasunorioi/agri-env-poe)
の agriha 1値1トピック publisher を組み合わせた変種。

## ハードウェア

- **MCU**: M5Stack PoECAM-W (ESP32-WROVER-B, 8MB PSRAM, 16MB flash)
- **Ethernet (PoE)**: 内蔵 Wiznet W5500 (SPI)
- **Camera**: OV2640 (DVP、SCCB)
- **Grove I²C** (G25 SDA / G33 SCL) → M5 ADC Unit V1.1 (ADS1110 @ 0x48)
- **日射計**: PVSS-03 (or 0-1V 直線出力の互換品)
- **書き込み**: M5 Writer (USB-C) → programming header

⚠ PoECAM の GPIO 27 は camera XCLK なので、ATOM 系の WS2812 status LED
(agri::Led) は使えない。状態は Serial ログのみ。

## 主な機能

- **DHCP / PoE プラグアンドプレイ** (W5500 SPI SCK=23 MISO=38 MOSI=13 CS=4)
- **agriha スキーマ MQTT publisher**
  - `<prefix>/sensor/InRadiation`  (W/m², PVSS-03)
  - `<prefix>/sensor/CamShot`       (URL string, 直近 upload の閲覧 URL)
- **Optional UECS-CCM 出力** (`InRadiation` のみ、`ccm_enabled` ON で有効化)
- **カメラ撮影 → WebDAV アップロード**
  - デフォルト 30 分間隔 (`cam_interval_s`)
  - 解像度 VGA / SVGA / XGA / HD / SXGA (既定) / UXGA から選択
  - JPEG quality 10 (best) 〜 30 (small)
  - 日射閾値以下ではスキップ (`cam_daylight_only` + `cam_daylight_wm2`)
  - PUT URL: `<wd_url>/<hostname>/<YYYY>/<MMDDHHMMSS>.jpg` (JST)
  - MQTT `CamShot` には auth-free の 閲覧 URL を publish
    (`<wd_url>` から末尾 `/upload` を落とした base + tail)
- **Web UI** 3 ページ + JSON API — 日射校正 / カメラ設定 / WebDAV 認証を編集
- **ArduinoOTA** over Ethernet (hostname `agri-canopy-XX`)
- **GitHub Release self-update** — Dashboard から1クリック更新

## MQTT トピック

`<prefix>` は agriha のハウス分割 (例 `agriha/2`)。全トピック retained。

| トピック | 型 / unit | 説明 |
|---|---|---|
| `<prefix>/sensor/InRadiation` | float `W/m^2` | ADS1110 電圧 × `wm2_per_volt` |
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

## 永続化設定 (NVS)

`Preferences` ネームスペース `canopy-cfg`。`/config` から編集:

- **共通** (`agri::CommonConfig`): Node ID / hostname / MQTT host, port, user,
  pass, topic prefix, interval / CCM enable, interval, room, region, priority, ntype
- **Solar**: `wm2v` (W/m² per V、PVSS-03=1000)、`ccm_ord` (InRadiation.<ntype> order)
- **Camera**: `cam_en` / `cam_int_s` / `cam_res` / `cam_jq` /
  `cam_dl_only` / `cam_dl_wm2`
- **WebDAV**: `wd_url` / `wd_user` / `wd_pass`

既定は MQTT host 空、CCM OFF、wd_url 空、camera ON @ 30 min SXGA Q12、
daylight-only ON @ 20 W/m² 閾値。

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

**Grove I²C** (ADS1110): SDA=25, SCL=33
