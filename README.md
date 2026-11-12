# Fork-per-client System Information Service

以 C、POSIX TCP sockets 與 `fork()` 實作的 Linux/WSL client/server 課程專案。  
伺服器每接受一條連線就建立一個子程序，父程序回收結束的子程序；共用網路、日誌與參數工具放在 `libnetutil.so`，用戶端和伺服器都動態連結它。

## 建置與執行

需求：Linux 或 WSL Ubuntu、GCC、GNU Make；封包擷取另需 `tcpdump` 與 sudo 權限。

```sh
make
./build/server --verbose 127.0.0.1 5050
./build/client --verbose 127.0.0.1 5050 INFO
./build/client 127.0.0.1 5050 PING
./build/client 127.0.0.1 5050 'HOLD 3'
```

支援命令：`INFO` 回傳 hostname、OS、核心版本、CPU 架構與線上 CPU 數；`PING` 回傳存活訊息；`HOLD 1..30` 暫停指定秒數，用來觀察同時存在的子程序。

## 兩層 Debug 控制

編譯期控制 `DEBUG_BUILD`：`make clean && make DEBUG=1` 會編入 DEBUG 日誌，`make clean && make DEBUG=0` 則移除 DEBUG 日誌程式碼。  
執行期用 server/client 的 `--verbose` 開啟日誌；若編譯時 `DEBUG=0`，執行期參數不能重新啟用已移除的 DEBUG 訊息。INFO/WARN/ERROR 日誌不受此開關影響。

## 健壯性與對照模式

預設伺服器具備 newline framing、255-byte 請求上限、嚴格命令驗證、5 秒 socket I/O timeout、完整處理部分 send/EINTR、SIGPIPE 防護、SIGCHLD 回收，以及 fork/socket/bind 等錯誤處理。  
`--legacy-demo` 會刻意停用 framing、timeout 與嚴格驗證，只供本機課程示範，勿用於正式服務。

```sh
make test
```

測試會產生三種穩健性對照和 100 客戶端併發執行證據，存於 `artifacts/`。預設 100 個 `HOLD 3` 請求需在 15 秒內完成；逐一處理約需 300 秒。  
可用 `CLIENT_COUNT=20 make test` 指定其他併發數。

### 手動執行三種對照案例

若要逐個觀察正常模式和 Legacy 模式的差異，開三個 WSL 終端。先在終端 1、2 分別啟動兩個 server：

```sh
# 終端 1：正常模式
./build/server --verbose 127.0.0.1 25050

# 終端 2：停用部分健壯性檢查的教學模式
./build/server --legacy-demo --verbose 127.0.0.1 25051
```

在終端 3 執行以下案例。每一組先連正常模式 `25050`，再連 Legacy 模式 `25051`。

#### 案例一：缺少 newline

`--raw` 讓 client 不在 `PING` 後附加換行，並保持連線等待回覆。

```sh
./build/client --raw 127.0.0.1 25050 PING
./build/client --raw 127.0.0.1 25051 PING
```

預期正常模式約 5 秒後回覆 `ERR request timed out`；Legacy 模式把一次 `recv()` 當成完整命令，回覆 `OK PONG`。

#### 案例二：超長請求

送出 `PING`、300 個 `x` 和 newline。

```sh
python3 - <<'PY'
import socket

for port in (25050, 25051):
    with socket.create_connection(("127.0.0.1", port), timeout=8) as connection:
        connection.sendall(b"PING" + b"x" * 300 + b"\n")
        print(f"port {port}: {connection.recv(4096).decode().strip()}")
PY
```

預期正常模式回覆 `ERR request too long`；Legacy 模式只讀緩衝區前段，回覆 `OK PONG`。

#### 案例三：命令尾隨垃圾

```sh
./build/client 127.0.0.1 25050 "INFO garbage"
./build/client 127.0.0.1 25051 "INFO garbage"
```

預期正常模式回覆 `ERR invalid request`；Legacy 模式以前綴比對命中 `INFO`，回傳系統資訊。  
測試完成後，在終端 1、2 各按一次 `Ctrl+C` 停止 server。也可以直接執行 `make test` 自動重跑這些案例並保存結果。

## 封包擷取

```sh
sudo apt update && sudo apt install tcpdump
make capture
```

腳本在 loopback 介面擷取 TCP 指定 port 的封包，產生 `artifacts/client-server.pcap`，並以 tcpdump 解碼輸出至 `artifacts/packet-analysis.txt`。  
協定內容是明文，可在封包中看到 `INFO` 與回覆；正式部署應加入 TLS。

## 專案內容

- `src/server.c`：TCP listener、fork-per-client、命令處理與 system information。
- `src/client.c`：TCP client、命令送出、逾時與回覆處理。
- `src/netutil.c`, `include/netutil.h`：兩端共用的動態函式庫 API。
- `Makefile`：建置、測試、示範、抓包目標；執行檔內含 `$ORIGIN` rpath，可從 `build/` 載入 `.so`。
- `report/REPORT.md`：可提交的系統設計與實驗報告。
- `report/VIDEO_SCRIPT.md`：2 分鐘影片口條與操作順序。

以上命令請在 WSL Ubuntu 的專案根目錄執行。
