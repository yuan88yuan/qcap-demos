# SC6F0 Dante RX: ALSA / ASoC 音訊驅動重構與執行指南

本指南詳細說明如何將 `sc6f0-dante-rx` 中原先透過應用層讀寫 Sysfs 直接控制 IP Core (`axis_switch` 與 `dante_rx_to_aes3`) 的架構，重構為標準 Linux **ALSA / ASoC (Audio System on Chip)** 驅動架構。

---

## 1. 架構概述 (Architecture Overview)

### 1.1 重構前後對比

```
【重構前：應用層直接 Sysfs 操控】
+-------------------------------------------------------------+
| User Space Application (sc6f0-dante-rx)                      |
|  - write_sysfs_value("/sys/.../axis_switch/port_enable")    |
|  - write_sysfs_value("/sys/.../dante_rx_to_aes3/apply")     |
+------------------------------+------------------------------+
                               | (Direct Sysfs Write)
                               v
+-------------------------------------------------------------+
| Kernel Space IP Core Sysfs Drivers                          |
+-------------------------------------------------------------+

【重構後：標準 ALSA / ASoC 架構】
+-------------------------------------------------------------+
| User Space Application (sc6f0-dante-rx)                      |
|  - snd_ctl_elem_value_set_enumerated("Audio Route Switch")   |
|  - snd_pcm_hw_params() (標準 ALSA PCM API)                   |
+------------------------------+------------------------------+
                               | (Standard ALSA ioctl API)
                               v
+-------------------------------------------------------------+
| Kernel Space ASoC Subsystem                                 |
|  +------------------------+  +----------------------------+ |
|  | ASoC Machine Driver    |  | ASoC DAI / Codec Driver    | |
|  |  - Mixer Control       |  |  - hw_params() Callback    | |
|  |    (AXI Stream Switch) |  |    (Dante RX to AES3)     | |
|  +------------------------+  +----------------------------+ |
+-------------------------------------------------------------+
```

### 1.2 職責分工

1. **`axis_switch` (AXI Stream 音訊切換器)**：
   - **Kernel**：註冊為 ASoC Mixer Enum Control (`Audio Input Route Switch`)。
   - **User**：透過 `snd_ctl_*` API 設定路由 (`0: PS DDR / ALSA`, `1: Dante FPGA`)。
2. **`dante_rx_to_aes3` (Dante 到 AES3 格式轉換器)**：
   - **Kernel**：註冊為 ASoC DAI (Digital Audio Interface) 元件。在 `.hw_params` 回調中自動設定 `sample-width` 與 `sample-rate` 並處理 3 次 `apply` 重試機制。
   - **User**：透過標準 `snd_pcm_hw_params` 設定格式，無需關心硬體暫存器及重試邏輯。

---

## 2. 核心驅動層實作 (Kernel-Space Implementation)

### 2.1 Devicetree 節點配置 (`dts`)

在 Linux Device Tree 中定義 ASoC 音訊節點：

```dts
/ {
    amba_pl: amba_pl@0 {
        #address-cells = <2>;
        #size-cells = <2>;

        /* AXI Stream Switch 節點 */
        axis_switch_0: axis_switch@b00a0000 {
            compatible = "xlnx,axis-switch-audio-1.0";
            reg = <0x0 0xb00a0000 0x0 0x1000>;
            status = "okay";
        };

        /* Dante RX to AES3 節點 */
        dante_rx_to_aes3_0: dante_rx_to_aes3@b0040000 {
            compatible = "xlnx,dante-rx-to-aes3-1.0";
            reg = <0x0 0xb0040000 0x0 0x1000>;
            status = "okay";
        };
    };

    /* ASoC Sound Card 定義 */
    sound_dante {
        compatible = "simple-audio-card";
        simple-audio-card,name = "xlnxhdmi1";
        simple-audio-card,format = "i2s";
        
        simple-audio-card,cpu {
            sound-dai = <&audio_formatter_0>;
        };
        
        simple-audio-card,codec {
            sound-dai = <&dante_rx_to_aes3_0>;
        };
    };
};
```

---

### 2.2 ASoC Mixer Control 實作 (`axis_switch`)

在核心驅動中將 `axis_switch` 實作成 ALSA Mixer 枚舉控制項：

```c
#include <sound/soc.h>
#include <sound/tlv.h>

/* AXI Stream Switch 枚舉選項 */
static const char * const audio_route_texts[] = {
    "PS DDR (Audio Formatter)",
    "Custom FPGA (Dante RX)"
};

static SOC_ENUM_SINGLE_DECL(audio_route_enum,
    0x0, /* reg offset */
    0,   /* shift */
    audio_route_texts);

static int audio_route_get(struct snd_kcontrol *kcontrol,
                           struct snd_ctl_elem_value *ucontrol)
{
    struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
    /* 讀取硬體暫存器 port_enable 狀態 */
    u32 val = readl(component->regmap_base + 0x04);
    ucontrol->value.enumerated.item[0] = (val == 0x1) ? 1 : 0;
    return 0;
}

static int audio_route_put(struct snd_kcontrol *kcontrol,
                           struct snd_ctl_elem_value *ucontrol)
{
    struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
    u32 sel = ucontrol->value.enumerated.item[0];
    
    /* 原子更新 AXI Stream Switch 暫存器 */
    writel(0, component->regmap_base + 0x00); /* reg_update_enable = 0 */
    writel(sel == 1 ? 0x1 : 0x0, component->regmap_base + 0x04); /* port_enable */
    writel(1, component->regmap_base + 0x00); /* reg_update_enable = 1 */

    pr_info("ASoC: Audio route switched to %s\n", audio_route_texts[sel]);
    return 1;
}

static const struct snd_kcontrol_new axis_switch_controls[] = {
    SOC_ENUM_EXT("Audio Input Route Switch", audio_route_enum,
                 audio_route_get, audio_route_put),
};
```

---

### 2.3 ASoC DAI `hw_params` 實作 (`dante_rx_to_aes3`)

將 `dante_rx_to_aes3` 寫寫與 Retry 邏輯移至 DAI `.hw_params` 回調：

```c
static int dante_rx_to_aes3_hw_params(struct snd_pcm_substream *substream,
                                       struct snd_pcm_hw_params *params,
                                       struct snd_soc_dai *dai)
{
    struct device *dev = dai->dev;
    void __iomem *base = dev_get_drvdata(dev);
    
    int sample_width = params_width(params);
    int sample_rate = params_rate(params);
    int retry, max_retries = 3;
    bool apply_ok = false;

    /* 寫入採樣率與位元深度暫存器 */
    writel(sample_width, base + 0x10); /* sample_width reg */
    writel(sample_rate,  base + 0x14); /* sample_rate reg */

    /* 處理 Apply 重試機制 */
    for (retry = 0; retry <= max_retries; ++retry) {
        if (retry > 0) {
            dev_warn(dev, "Retrying dante_rx_to_aes3 apply (attempt %d/%d)...\n", retry, max_retries);
            msleep(500);
        }
        
        writel(1, base + 0x00); /* apply reg */
        if (readl(base + 0x04) & 0x01) { /* 讀取 ack / status 暫存器 */
            apply_ok = true;
            break;
        }
    }

    if (!apply_ok) {
        dev_err(dev, "Failed to configure dante_rx_to_aes3 (%d Hz / %d bit) after retries\n",
                sample_rate, sample_width);
        return -EIO;
    }

    dev_info(dev, "dante_rx_to_aes3 configured: %d Hz / %d bit\n", sample_rate, sample_width);
    return 0;
}

static const struct snd_soc_dai_ops dante_rx_to_aes3_dai_ops = {
    .hw_params = dante_rx_to_aes3_hw_params,
};
```

---

## 3. 應用層程式碼重構 (User-Space Application Refactoring)

應用層 `sc6f0-dante-rx.cpp` 可以完全移除 Sysfs 文件開啟與 `write_sysfs_value` 邏輯。

### 3.1 C++ 應用層調用 ALSA Control

```cpp
#include <alsa/asoundlib.h>

// 音訊切換枚舉
enum AudioMode {
    AUDIO_MODE_STREAM = 0,       // Dante FPGA (port_enable = 0,1)
    AUDIO_MODE_TEST_PATTERN = 1  // PS DDR / ALSA (port_enable = 0,0)
};

static bool set_audio_route_via_alsa(const char* card_name, AudioMode mode) {
    snd_ctl_t *handle;
    int err;
    
    if ((err = snd_ctl_open(&handle, card_name, 0)) < 0) {
        LOGE("snd_ctl_open(%s) failed: %s", card_name, snd_strerror(err));
        return false;
    }

    snd_ctl_elem_id_t *id;
    snd_ctl_elem_value_t *control;
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_value_alloca(&control);

    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, "Audio Input Route Switch");

    snd_ctl_elem_value_set_id(control, id);
    
    // 0: PS DDR / ALSA, 1: Custom FPGA (Dante)
    int item_index = (mode == AUDIO_MODE_STREAM) ? 1 : 0;
    snd_ctl_elem_value_set_enumerated(control, 0, item_index);

    if ((err = snd_ctl_elem_write(handle, control)) < 0) {
        LOGE("Failed to set ALSA Mixer control 'Audio Input Route Switch': %s", snd_strerror(err));
        snd_ctl_close(handle);
        return false;
    }

    LOGI("Successfully configured audio route via ALSA mixer control: %s",
         mode == AUDIO_MODE_STREAM ? "Custom FPGA (Dante)" : "PS DDR (Audio Formatter)");
    
    snd_ctl_close(handle);
    return true;
}
```

---

### 3.2 簡化後的 `set_audio_info`

原本的 `configure_dante_rx_to_aes3` 已被 ALSA 核心接管，簡化後的 C++ 邏輯如下：

```cpp
bool set_audio_info(const struct audio_info* info) {
    static const char* alsaCard = "hw:CARD=xlnxhdmi1";
    static const char* alsaPcmDevice = "default:CARD=xlnxhdmi1";

    if (!info) return false;

    // 1. 透過 ALSA Mixer 切換路由 (代替原 configure_audio_switch)
    if (!set_audio_route_via_alsa(alsaCard, mAudioMode)) {
        return false;
    }

    if (mAudioMode == AUDIO_MODE_TEST_PATTERN) {
        return start_test_pattern_playback();
    }

    // 2. 設定 ALSA PCM 參數 (Kernel ASoC 會自動引發 dante_rx_to_aes3_hw_params 回調)
    stop_alsa();

    if (snd_pcm_open(&mAlsaPcm, alsaPcmDevice, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        LOGE("snd_pcm_open failed");
        return false;
    }

    snd_pcm_hw_params_t* hwParams;
    snd_pcm_hw_params_alloca(&hwParams);
    
    snd_pcm_hw_params_any(mAlsaPcm, hwParams);
    snd_pcm_hw_params_set_access(mAlsaPcm, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(mAlsaPcm, hwParams, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate(mAlsaPcm, hwParams, info->sample_rate, 0);
    snd_pcm_hw_params_set_channels(mAlsaPcm, hwParams, info->channel_count);

    // 呼叫 snd_pcm_hw_params 時，Kernel 會自動調用 dante_rx_to_aes3 DAI hw_params
    if (snd_pcm_hw_params(mAlsaPcm, hwParams) < 0) {
        LOGE("Failed to apply ALSA hw_params");
        stop_alsa();
        return false;
    }

    snd_pcm_prepare(mAlsaPcm);
    snd_pcm_start(mAlsaPcm);

    LOGI("Audio pipeline started seamlessly via ALSA subsystem.");
    return true;
}
```

---

## 4. 編譯與執行驗證步驟 (Build & Execution Verification)

### 4.1 專案編譯環境 (Project Build)

根據本專案規範，使用 Docker 容器與 CMake 進行編譯：

```bash
# 1. 進入專案根目錄
cd /home/zzlee/docker/qcap-demos

# 2. 在 Docker 容器內建置專案 (以 ubuntu2004_x64 平台為例)
./scripts/docker-run.sh qcap-build:all3rdparty-ubuntu2004_x64-vaapi-20260720 \
  env BUILD_SYSTEM=cmake ./build.sh ubuntu2004_x64 sc6f0-dante-rx
```

編譯產出之執行檔位置：`_objs/ubuntu2004_x64/bin/sc6f0-dante-rx`

---

### 4.2 系統與驅動驗證指令 (Kernel & ALSA Verification)

在目標硬體設備上執行以下指令，驗證 ASoC 驅動與 ALSA Mixer Control 是否正確載入：

#### 步驟 1：檢視 ALSA 聲卡與 Control 列表

```bash
# 檢查音效卡列表
aplay -l

# 列出 xlnxhdmi1 聲卡的所有 Mixer 控制項
amixer -c xlnxhdmi1 controls
```

**預期輸出**包含新註冊的 Control：
```text
numid=1,iface=MIXER,name='Audio Input Route Switch'
```

#### 步驟 2：手動測試 ALSA Mixer 切換

```bash
# 查詢當前音訊路由
amixer -c xlnxhdmi1 cget name='Audio Input Route Switch'

# 手動切換至 Custom FPGA (Dante RX)
amixer -c xlnxhdmi1 cset name='Audio Input Route Switch' 1

# 手動切換至 PS DDR (Audio Formatter)
amixer -c xlnxhdmi1 cset name='Audio Input Route Switch' 0
```

#### 步驟 3：執行測試應用程式

```bash
# 執行編譯後的測試程式
./_objs/ubuntu2004_x64/bin/sc6f0-dante-rx
```

---

## 5. 常見問題排除 (Troubleshooting)

| 異常現象 | 可能原因 | 排除步驟 |
| :--- | :--- | :--- |
| `snd_ctl_open()` 失敗 | 聲卡名稱未與 DTS 對齊或 ALSA 驅動未載入 | 執行 `cat /proc/asound/cards` 確認聲卡名稱，或檢查 `dmesg \| grep snd` |
| `snd_pcm_hw_params()` 返回 `-EIO` | Dante RX IP Core 重試 3 次後仍然未回應 | 檢查 FPGA Clock 供應及 Devicetree 中 `reg` 暫存器實體位址配置 |
| Mixer Switch 選項切換無效 | `axis_switch` reg_update_enable 時序未鎖定 | 檢查驅動中 `writel(0)` -> `writel(sel)` -> `writel(1)` 暫存器寫入邏輯 |

---
*文件產生時間: 2026-08-13*
