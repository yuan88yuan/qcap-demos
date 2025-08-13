# qcap-demos

這是一個 C++ 專案，旨在展示和測試 `qcap` 影音擷取函式庫的功能。

此專案包含主要的範例應用程式 (`qdemo`)、一個工具程式庫 (`zzlab`)，以及一個基於 `make` 的複雜建置系統，使其具有高度的跨平台相容性。

## 目錄結構

```
.
├── src/
│   ├── qdemo/         # 主要的範例應用程式
│   ├── tests/         # 測試程式
│   └── zzlab/         # 共用的工具函式庫 (日誌、時脈、工具等)
├── mkfiles/           # Make 的設定檔，支援多種平台與函式庫
├── _objs/             # 編譯後產生的目標檔案與執行檔
└── ...
```

## 建置與執行

此專案使用 `make` 進行編譯。您需要根據 `mkfiles` 目錄中定義的目標平台來進行建置。

### 1. 編譯

選擇一個您要使用的目標平台，並執行 `make` 指令。例如，若要為 `l4t-r36-2` (NVIDIA Jetson L4T R36.2) 平台編譯：

```bash
# 將 l4t-r36-2 替換成您的目標平台, QCAP_HOME替換為QCAP函式庫路徑
QCAP_HOME=/your-qcap-root/l4t-r36-2 make -f l4t-r36-2.mk -j4
```

編譯成功後，所有產生的檔案 (包括執行檔) 將會被放置在 `_objs/<your-target-platform>/` 目錄下。

### 2. 執行

執行檔位於對應平台的 `_objs` 子目錄中。

```bash
# 執行 qdemo 範例程式
./_objs/l4t-r36-2/qdemo
```

## 主要技術與依賴函式庫

此專案整合了多種影音處理與系統相關的函式庫，使其能在不同硬體和作業系統上運作。主要的技術與依賴包括：

*   **核心**: C++
*   **建置系統**: GNU Make
*   **影音處理**:
    *   FFmpeg
    *   GStreamer
    *   NVIDIA CUDA / NPP / NvJpeg
    *   VAAPI
    *   X264
*   **支援平台**:
    *   Linux (Ubuntu, CentOS, Debian, Kylin)
    *   嵌入式平台 (NVIDIA L4T, HiSilicon, Rockchip, Novatek)
*   **其他函式庫**:
    *   SDL
    *   ALSA
    *   OpenSSL
    *   Boost
    *   fmt
