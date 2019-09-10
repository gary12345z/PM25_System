# 系統簡介

# 各項硬體
### ESP8266 監測站端
 - ESP8266-12F（微控器）
 - PMS7003（空氣品質感測器）
 - 18650（可充電電池）
 - 18650充電保護版（防止過充電、過放電）
 - USB to TTL（燒錄微控器用）
 - 5V升壓電路（穩定電池電壓）
 - MAX485（將RS-232訊號轉成RS-485）
### 伺服器 資料端 
 - Raspberry pi（樹莓派）  
   - MQTT Broker(mosquitto)  
   - Python2.7  
   - MySQL  
   - Apache HTTP Server  

# 功能說明
### ESP8266 監測站端
開機時，根據Mode腳位判斷模式：High（設定模式）、Low（正常模式）。  
在設定模式中，ESP8266會成為AP模式，此模式可設定該監測站的系統參數（此時指示燈只有紅燈）。  
1. 連接上AP，SSID為ESP_XX（XX表示其測站的編號）。
2. 使用HTTP協定的GET方式傳送資料，例如URL為 http://192.168.4.1/?SSID=WIFISSID&PWD=WIFIPWD&IP=127_0_0_1&PORT=1234&ID=1 ，則表示WiFi連線的SSID和Password為WIFISSID/WIFIPWD（預設是只可以連線有加密的WiFi），資料端的IP/PORT為127.0.0.1/1234（目前無法輸入透過DNS去連線，且請將其中"."替換成"_"），ID為該測站的編號（不同測站請勿重複）。
3. 設定成功後，將系統參數存入EEPROM，並回傳"Battery Power: XX% OK"文字，其中XX為電池電量（此時指示燈為紅+綠燈）。   
 
在正常模式中，ESP8266會成為STA模式，此模式為正常工作的模式（此時指示燈只有綠燈）。  
1. 系統會自EEPROM中讀取系統參數。
2. 此時會有三件事同時運作（其實不是真正同時運作，只是分時多工）：收發PMS7003感測器的資料、收發伺服器的資料、Ｗatchdog。 
- 傳送開機指令給感測器，等候10秒後再開始接收PMS7003感測器的資料（剛感測器剛啟動時，資料不正確），接收時順便檢查校驗碼，如果正確，傳送關機指令給感測器，如果錯誤，繼續接收下一筆資料。 
- 連線上伺服器，並先檢查是否有封包要透過RS485傳送，如果有則傳送，沒有則等至上一個工作取到資料，並傳出資料。接著花5秒讀取RS485的回應，並回傳，最後在傳送完成命令的封包，並按照指示時間長度睡眠。  
- 如果開機超過45秒，不管如何，直接傳送關機指令給感測器、強制ESP8266進入深層睡眠1分鐘（1分鐘後重新開機）。 

### 伺服器 資料端  
依功能分成三支程式分別為Subscriber.py、DailyExport.py、Output.py  
 - Subscriber.py（開機後執行）  
 負責和監測站溝通以及儲存資料
 1. 連線至MQTT的Broker，並訂閱空氣品質的主題，並等待監測站發布  
 2. 取得監測站發布的資料後，根據當前時間計算出應當睡眠的時間（每10分鐘1筆資料），並透過MQTT發布此時間給特定監測站（在此之前，監測站已先訂閱特定主題）  
 3. 檢查此時段是否已經有資料存在於資料庫中，如果沒有則將資料存入資料庫  
 4. 等待下一筆資料  
 
 - DailyExport.py（每天0:00執行）  
 1. 從資料庫匯出過於老舊的資料，使用CSV的形式儲存，並且依照月份分成不同檔案  
 2. 檢查是否有監測站電源不足，如果有則寄信通知管理者
 
 - Output.py（每隔10分鐘執行）  
 1. 從資料庫匯出欲顯示於網頁上的資料，亦使用CSV的形式儲存
 2. 從政府的開放平台上（http://opendata2.epa.gov.tw/AQI.json ）取得中央氣象局的測站資料，並存入資料庫（日後做資料分析）

# 補充說明
### ESP8266 監測站端
 - 使用Arduino IDE 燒錄
 於“額外的開發板管理員網址”中輸入：http://arduino.esp8266.com/stable/package_esp8266com_index.json  
 並安裝esp8266 by ESP8266 Community  
 詳細內容請自行google： Arduino ide esp8266  
 
 - Arduino 工具選項設定值（確認可燒錄）  
![image](https://github.com/gary12345z/PM25_System/blob/master/Picture/%E6%88%AA%E5%9C%96%202019-09-08%20%E4%B8%8B%E5%8D%882.19.39.png)

 - 燒錄硬體設置  
在ESP8266開機（或重開機）的當下，GPIO15(MTDO)串連一個電阻GND、GPIO2為High、GPIO0為Low時，為燒錄模式  
此時可透過USB to TTL，連接ESP8266的TXD/RXD,將程式燒錄至的Flash當中  
結束燒錄後請在ESP8266開機（或重開機）的當下，將GPIO0改設成為High即可正常運作

- ESP8266 PinMap  
TXD/RXD -> PMS7003(boud=9600)  
GPIO5/GPIO4 -> RS485(boud=115200)（分別接到MAX485的DI/RI）  
GPIO2 -> MAX485的~(RE)/DE  
GPIO0 -> 燒錄用（保持Low）  
GPIO4 -> Mode Pin  
ADC -> 偵測電池電量（最大值只有1V，所以要分壓！）  
REST -> GPIO16（喚醒ESP8266用的）  
GPIO12 -> 綠色LED  
GPIO13 -> 紅色LED  

### 伺服器 資料端 
 - MQTT  
 伺服器和監測站溝通一律使用MQTT協定，目前主題有DATA（空氣品質的資料/JSON）/SET（睡眠時間/ASCII）/SEND（欲透過RS485發送的指令）/RECEIVE（透過RS485接收的指令）  
 
