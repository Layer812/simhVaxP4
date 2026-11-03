# simhVaxP4 — MicroVAX II / 4.3BSD on M5Stack Tab5

X68KTab5向けに作ったプラットフォームで、MicroVAX IIと4.3BSD Quasijarusも動かしてみました。

「悪魔本」を片手に、カフェでRogueを楽しみたい方にもどうぞ。

> **公開準備中 / First release in preparation:** VAX専用ファームウェアは現在準備中です。実機確認が済んだものを[Releases](https://github.com/Layer812/simhVaxP4/releases)で案内します。

## はじめてのセットアップ

M5Stack Tab5、キーボード、microSDカードを用意します。Windows・macOS・Linuxいずれも、PC側でスクリプトやコマンドを実行する必要はありません。

1. **VAX専用ファームウェアを書き込む。** 公開後は[Releases](https://github.com/Layer812/simhVaxP4/releases)から入手できます。
2. **4.3BSDのディスクをダウンロードする。** [narukeh / 4.3BSD-Quasijarus](https://github.com/narukeh/4.3BSD-Quasijarus) の [`quasdisk.dsk.xz`](https://github.com/narukeh/4.3BSD-Quasijarus/blob/main/quasdisk.dsk.xz) を保存し、通常の解凍ソフトで展開します。
3. **SDカードにコピーする。** 展開した `quasdisk.dsk` をSDカードのルートに置きます。`*.xz` のままでは使用できません。
4. **Tab5を起動する。** ディスクからBSDを起動します。

必要に応じて、[`sdcard/MACRO.TXT`](sdcard/MACRO.TXT) をSDカードのルートにコピーできます。これはMACROボタンでゲストのコンソールへ入力するコマンド例です。**配置しなくても起動できます。**

### SDカードの内容

```text
SD card/
├── quasdisk.dsk  ← 展開した4.3BSDディスク（必須）
└── MACRO.TXT     ← お好みで（任意）
```

初回起動時の作業用ディスク管理などはVAX専用ファームウェア側で扱う方針です。追加の初期化ファイルをSDカードへ置く必要はありません。**初回導入の実機確認が済むまでは正式なファームウェア配布は行いません。**

### 操作

Wi-Fiは任意です。Tab5のWi-FiボタンでSSIDを選んで接続します。接続しなくてもBSDのローカルコンソールは使用できる設計です。MACROボタンは `MACRO.TXT` があるときだけその内容を入力します。

BSDでは `vi` や `rogue` などをお楽しみください。

### 4.3BSDの配布について

ディスクイメージ自体は当リポジトリに再収録していません。入手は[元の公開先](https://github.com/narukeh/4.3BSD-Quasijarus)をご利用ください。BSDと収録ソフトウェアに関する権利はそれぞれの権利者に帰属します。

---

## English

I tried running a MicroVAX II and 4.3BSD Quasijarus on the platform originally developed for X68KTab5. Perhaps something to enjoy with Rogue and a coffee.

> **First release in preparation:** A dedicated VAX firmware build is being prepared. Verified firmware will be listed under [Releases](https://github.com/Layer812/simhVaxP4/releases).

### First-time setup

You need an M5Stack Tab5, a keyboard, and a microSD card. No PC-side command-line tools or scripts are required on Windows, macOS, or Linux.

1. **Flash the dedicated VAX firmware**, once the verified image is available under [Releases](https://github.com/Layer812/simhVaxP4/releases).
2. **Download the BSD disk.** Get [`quasdisk.dsk.xz`](https://github.com/narukeh/4.3BSD-Quasijarus/blob/main/quasdisk.dsk.xz) from [narukeh / 4.3BSD-Quasijarus](https://github.com/narukeh/4.3BSD-Quasijarus), and extract it with your usual archive application.
3. **Copy the extracted `quasdisk.dsk`** to the root of the microSD card. Do not copy only the compressed `.xz` archive.
4. **Insert the card and boot Tab5.**

Optionally, copy [`sdcard/MACRO.TXT`](sdcard/MACRO.TXT) to the card root. It provides example guest-console commands for the MACRO button and is not needed to boot.

```text
SD card/
├── quasdisk.dsk  # extracted BSD disk, required
└── MACRO.TXT     # optional
```

The dedicated VAX firmware is intended to manage the working disk and boot setup internally, without extra SD configuration files. **First-install hardware verification is still required before firmware release.**

Wi-Fi is optional and can be configured from the Tab5 Wi-Fi button. The local BSD console is designed to work offline. Try `vi` or `rogue` after boot.

The BSD disk image is not redistributed here; please obtain it from its [original hosting repository](https://github.com/narukeh/4.3BSD-Quasijarus). Third-party copyrights and license terms remain with their respective holders.
