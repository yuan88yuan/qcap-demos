# `test-zznvenc2.cpp` 作用及執行流程

`test-zznvenc2.cpp` 是一個用於 `qcap2` 函式庫的測試應用程式，主要用於驗證使用 `ZZNVCODEC2` 進行影像編碼的功能。

## 執行流程

1.  **初始化與設定**：
    *   程式啟動時，會初始化日誌模組和其他 `zzlab` 工具函式庫。
    *   `App0::Main()` 函式是應用程式的入口點，它會呼叫 `App0::Run()`。
    *   `App0::Run()` 函式會檢查 `TEST_CASE` 環境變數。如果設定為 `1`，則直接執行 `TestCase1`；否則，它會進入一個互動式迴圈，等待使用者輸入 (`q` 退出，`0`-`9` 執行對應的測試案例)。

2.  **`TestCase1` 的核心邏輯 (`DoWork`)**：
    *   **事件處理器設定**：啟動事件處理器機制。
    *   **啟動階段 (`OnStart`)**：
        *   **模擬影像來源 (`StartFakeVsrcQ`)**：
            *   建立一個模擬的影像來源，該來源會產生 `NV12` 格式 (1920x1080) 的視訊影格。
            *   這些影格是透過重複載入 `vsrc.jpg` 圖片到預先分配的 `qcap2_rcbuffer_t` 緩衝區中來生成的。
            *   設定一個 `qcap2_timer_t` 以 60 FPS 的頻率，將這些模擬影格從一個內部緩衝區佇列 (`pBufferQ`) 推送到一個視訊來源佇列 (`pVsrcQ`)。
        *   **啟動共享錄影 (`StartShareRecord`)**：
            *   設定視訊編碼器的屬性，包括：
                *   編碼器類型：`QCAP_ENCODER_TYPE_ZZNVCODEC2` (NVIDIA NVENC)。
                *   編碼格式：`QCAP_ENCODER_FORMAT_H264`。
                *   解析度：1920x1080。
                *   影格率：60.0 FPS。
                *   位元率：8000 kbps (目標) 和 12000 kbps (最大)。
            *   呼叫 `QCAP_START_SHARE_RECORD` 啟動編碼程序，並設定為僅編碼視訊 (`QCAP_RECORD_FLAG_ENCODE | QCAP_RECORD_FLAG_VIDEO_ONLY`)。
        *   **新增視訊來源佇列事件處理器**：將 `OnVsrcQ` 函式註冊為 `pVsrcQ` 的事件處理器。這表示每當 `pVsrcQ` 中有新的影格可用時，`OnVsrcQ` 就會被呼叫。

3.  **影格處理與編碼 (`OnVsrcQ`)**：
    *   當 `pVsrcQ` 中有新的模擬影格時，`OnVsrcQ` 函式會被觸發。
    *   它從 `pVsrcQ` 中取出一個 `qcap2_rcbuffer_t` (視訊影格)。
    *   從影格中提取其呈現時間戳 (PTS)。
    *   將 `qcap2_rcbuffer_t` 中的原始視訊資料轉換為可直接使用的緩衝區。
    *   呼叫 `QCAP_SET_VIDEO_SHARE_RECORD_UNCOMPRESSION_BUFFER()` 函式，將這些未壓縮的視訊影格資料送入 `ZZNVCODEC2` 編碼器進行編碼。

4.  **測試結束**：
    *   `TestCase1::DoWork()` 隨後呼叫 `wait_for_test_finish()`，使應用程式運行一段時間 (預設為 1,000,000 毫秒，或直到使用者在互動模式下按下按鍵)。
    *   當測試結束或應用程式退出時，所有透過 `free_stack_t` 管理的資源 (如計時器、佇列和共享錄影會話) 都會被正確釋放。

## 測試目的

此測試旨在驗證 `qcap2` 函式庫中 `ZZNVCODEC2` 視訊編碼器的功能。它透過模擬一個視訊來源，並以固定的影格率將影像資料送入編碼器，確保編碼器能夠正確地被設定、啟動並處理未壓縮的視訊影格。