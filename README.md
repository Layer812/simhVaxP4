# simhVaxP4 — MicroVAX II / 4.3BSD on Tab5

X68KTab5向けに作ったプラットフォームで、MicroVAX IIと4.3BSD Quasijarusも動かしてみました。

「悪魔本」を片手に、カフェでRogueを楽しみたい方にもどうぞ。

> **公開準備中 / Preparing the first release:** 初回導入用ファームウェアは実機確認中です。現在、完成した初回書込み用ファームウェアはまだ配布していません。以下は公開時の導入手順です。

## はじめてのセットアップ

M5Stack Tab5、キーボード、FAT32形式のmicroSDカード（4GB以上推奨）を用意します。**Windows・macOS・Linuxのいずれでも、PC側でコマンドやスクリプトを実行する必要はありません。**

1. **ファームウェアを書き込む。** 検証完了後、[Releases](https://github.com/Layer812/simhVaxP4/releases)に初回書込み用ファームウェアを掲載します。
2. **BSDのディスクを入手する。** [narukeh / 4.3BSD-Quasijarus](https://github.com/narukeh/4.3BSD-Quasijarus) の [`quasdisk.dsk.xz`](https://github.com/narukeh/4.3BSD-Quasijarus/blob/main/quasdisk.dsk.xz) をダウンロードして展開します。展開後の `quasdisk.dsk` を `QUASDISK.DSK` という名前でSDカードの一番上（ルート）にコピーしてください。**`.xz`のままコピーしないでください。**
3. **設定ファイルをコピーする。** このリポジトリの [`sdcard/`](sdcard/) にある4つのテキストファイルを、フォルダごとではなくSDカードのルートにコピーします。[Code → Download ZIP](https://github.com/Layer812/simhVaxP4/archive/refs/heads/main.zip)からまとめて取得しても構いません。
4. **Tab5を起動する。** 初回はファームウェアが元ディスクから書込み用 `QUASWORK.DSK` を作成し、BSDに初期設定を行う想定です。2回目以降は作業用ディスクを使い、既存のデータを保持します。

### SDカードの中身

```text
SD card/
├── QUASDISK.DSK        ← 外部公開元からダウンロードし、展開したディスク
├── VAXMEDIA.TXT        ← ディスクの場所
├── BSD_FIRSTBOOT.TXT   ← 初回のBSDネットワーク設定
├── BSD_MULTIUSER.FLAG  ← マルチユーザー起動の指定
└── MACRO.TXT           ← 任意の端末設定コマンド
```

`QUASWORK.DSK`は初回起動時にのみ作成する設計です。既存の`QUASWORK.DSK`は上書きしません。SDカードには元ディスクと作業用ディスクの両方を置ける空き容量が必要です。

### 操作

Wi-Fiは必須ではありません。Tab5のWi-FiボタンでSSIDを選択し、パスワードを入力します。オフラインでもBSDのローカルコンソールを利用できます。MACROボタンはSDカードの`MACRO.TXT`をゲストコンソールへ入力するもので、ネットワーク接続のために毎回押す必要はありません。

BSDにログインしたら`vi`や`rogue`をお楽しみください。全画面表示向けに80桁の端末設定を使用します。

### 配布について

4.3BSDのディスクイメージは本リポジトリで複製配布せず、[元の公開先](https://github.com/narukeh/4.3BSD-Quasijarus)から取得する方式です。BSDおよび収録ソフトウェアの著作権・ライセンスは、それぞれの権利者に帰属します。

---

## English

This is another use of the platform originally developed for X68KTab5: a MicroVAX II running 4.3BSD Quasijarus on M5Stack Tab5.

Perhaps for enjoying a little Rogue at a café.

> **First release in preparation:** The complete first-install firmware is still being verified on hardware. It is not available as a public release yet. The steps below describe the intended first-time setup.

### First-time setup

You need an M5Stack Tab5, a keyboard, and a FAT32 microSD card (4 GB or larger recommended). **No PC-side scripts or command-line tools are required** on Windows, macOS, or Linux.

1. **Flash the firmware.** Once verified, the first-install image will be published under [Releases](https://github.com/Layer812/simhVaxP4/releases).
2. **Download the BSD disk.** Obtain [`quasdisk.dsk.xz`](https://github.com/narukeh/4.3BSD-Quasijarus/blob/main/quasdisk.dsk.xz) from [narukeh / 4.3BSD-Quasijarus](https://github.com/narukeh/4.3BSD-Quasijarus). Extract it with your usual archive application. Copy the extracted `quasdisk.dsk` into the microSD root as `QUASDISK.DSK` (not the compressed `.xz` file).
3. **Copy configuration files.** Copy the **four text files** from [`sdcard/`](sdcard/) into the microSD root, not the folder itself. You can also get them through [Code → Download ZIP](https://github.com/Layer812/simhVaxP4/archive/refs/heads/main.zip).
4. **Boot Tab5.** On the first boot, the firmware is designed to create a writable `QUASWORK.DSK` and configure BSD automatically. Subsequent boots reuse the writable disk without replacing existing data.

Wi-Fi is optional. Choose an SSID and enter its password using the Tab5 Wi-Fi button. The MACRO button sends the user-editable `MACRO.TXT` into the guest console. It is not needed for the automatic BSD network startup. The local console should remain usable without Wi-Fi.

The BSD disk image is **not redistributed** here. Please download it from its [original hosting repository](https://github.com/narukeh/4.3BSD-Quasijarus). All third-party copyrights and license terms remain with their respective holders.

**Status:** Firmware source preparation is complete for review, but a full ESP-IDF build and first-install hardware verification are still required before the public firmware release.
