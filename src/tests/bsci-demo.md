### TestCase6 功能概述

`TestCase6` 的主要功能是模擬一個包含「即時影像顯示」與「非同步推論 (Inference)」的雙路影像處理流程。

它會建立兩個視窗：
1.  **即時視窗 (Live Window)**：較大的視窗 (1280x720)，用於顯示從模擬視訊來源經過第一次裁切後的即時影像。
2.  **推論結果視窗 (Inference Window)**：較小的視窗 (560x560)，用於顯示影像經過模擬推論流程處理後的結果。

整個測試案例展示了如何使用 `qcap2` 函式庫建立一個複雜的、事件驅動的非同步影像處理管線 (pipeline)，包含影像的讀取、裁切、色彩空間轉換、多執行緒處理以及最終的顯示。

### 執行流程詳解

程式的執行流程是事件驅動 (event-driven) 且非同步的，主要可以分為以下幾個階段：

1.  **初始化 (DoWork -> OnStart)**
    *   `DoWork()` 首先啟動事件處理器 (`StartEventHandlers`)，並在事件處理器的 context 中執行 `OnStart()` 函數來建構整個影像處理管線。
    *   `OnStart()` 會建立上述的兩個 X11 視窗 (`pWindow_live`, `pWindow_infer`)。
    *   它會建立一個獨立的事件處理器 (`pEventHandlers_infer`) 專門用來處理模擬的推論工作，以避免阻塞主流程。

2.  **模擬視訊來源 (StartFakeVsrcQ -> OnFakeVsrc)**
    *   程式並非從真實的攝影機讀取影像，而是透過 `StartFakeVsrcQ` 建立一個模擬的視訊來源。
    *   它會先從磁碟讀取一張圖片 (`vsrc.jpg`) 作為影像來源。
    *   接著啟動一個計時器 (`qcap2_timer_t`)，並由 `OnFakeVsrc` 回呼函式週期性地 (模擬 30 FPS) 將這張圖片的 buffer 推入一個佇列 (`pVsrcQ`)，並觸發 `pEvent_vsrc` 事件，模擬有新的影像幀傳入。

3.  **即時影像處理流程 (OnVsrc)**
    *   `pEvent_vsrc` 事件會觸發 `OnVsrc` 回呼函式。
    *   `OnVsrc` 從佇列中取出影像幀 (1920x1080, NV12 格式)。
    *   **影像裁切 (Crop)**：將影像透過 `pVsca_crop` (一個影像縮放/裁切器) 裁切成 1280x720 (I420 格式)。
    *   **即時顯示**：將裁切後的 1280x720 影像推送到 `pVsink_live`，顯示在「即時視窗」中。
    *   **分流至推論管線**：同時，它會**非同步地**將同一幀裁切後的影像推送到 `pVsca_infer`。這個元件會將影像轉換為 `GBRP` 格式並觸發 `pEvent_infer` 事件，將影像送入下一個處理階段。這個推送動作不會阻塞 `OnVsrc` 的執行。

4.  **模擬推論處理流程 (OnInfer)**
    *   `pEvent_infer` 事件會觸發 `OnInfer` 回呼函式 (此函式在獨立的 `pEventHandlers_infer` 中執行)。
    *   `OnInfer` 從 `pVsca_infer` 取出 `GBRP` 格式的影像。
    *   **模擬推論**：呼叫 `DoInfer` 函式，此函式內部使用 `pVsca_infer_crop` 將 1280x720 的影像再次裁切成 560x560，模擬在一個感興趣的區域 (Region of Interest) 進行推論。
    *   **儲存結果**：將推論結果 (560x560 的 `GBRP` 影像) 透過 `pVsca_infer_bgr` 轉換成 `BGR24` 格式，並存成 BMP 圖片檔案 (`testcase6-XX.bmp`) 到磁碟中。
    *   **顯示結果**：同時，將推論結果透過 `pVsca_infer_vsink` 轉換成 `I420` 格式，最後推送到 `pVsink_infer`，顯示在「推論結果視窗」中。

5.  **主迴圈 (wait_for_test_finish)**
    *   在 `DoWork()` 的最後，程式會進入一個等待迴圈，主要工作是處理兩個視窗的 UI 事件，直到測試結束。

總結來說，`TestCase6` 是一個從模擬來源讀取影像，然後將其分流成兩路：一路用於即時預覽，另一路送入獨立的執行緒進行模擬的推論與後處理，並將兩路的結果分別顯示在不同視窗的完整範例。