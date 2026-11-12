# 2 分鐘展示影片口條

**0:00–0:15｜專案與建置**

「這是用 C 實作的 Linux TCP 系統資訊服務。共享網路和 log 函式放在 `libnetutil.so`，client 和 server 都動態連結。現在用 Makefile 編譯。」畫面執行 `make clean && make DEBUG=1`。

**0:15–0:35｜基本功能**

「伺服器每接受一個 client 就 fork 一個子程序，INFO 回傳主機與作業系統資訊，PING 用於確認服務存活。」分割終端執行 `./build/server --verbose 127.0.0.1 5050` 與 `./build/client 127.0.0.1 5050 INFO`。

**0:35–0:55｜雙層 Debug**

「編譯期 `DEBUG=1` 決定是否包含 DEBUG 程式碼，執行期 `--verbose` 決定是否輸出。關掉其中任一層都不會顯示 DEBUG 訊息。」指出 server log 中的 fork PID，再說明 `make clean && make DEBUG=0` 可關閉編譯期日誌。

**0:55–1:20｜併發證明**

「測試同時啟動一百個 client，每個連線要求 HOLD 三秒。這次一百個都成功，總時間約 3.24 秒；若逐一執行約需 300 秒。server log 能看到不同 worker PID。」執行 `make test`，展示 `artifacts/concurrency-proof.txt`。

**1:20–1:45｜健壯性差異**

「測試也比較 robust 和 legacy demo：未完成的命令會 timeout、超長訊息會被拒絕、尾隨垃圾不會被當成有效命令；legacy 模式則錯誤接受這些輸入。」展示 `artifacts/robustness-examples.txt`。

**1:45–2:00｜封包與收尾**

「tcpdump 擷取 loopback 封包後，可以看到 TCP handshake 與 INFO/OK 明文 payload。這是課堂示範協定，正式服務應再加 TLS。」展示 pcap/Wireshark 與報告的系統設計摘要。