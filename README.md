# 系統簡介

# 各項硬體
### ESP8266 監測站端
 - ESP8266-12F（微控器）
 - PMS7003（空氣品質感測器）
 - 18650（可充電電池）
 - 18650充電保護版（防止過充電、過放電）
 - USB to TTL（燒錄微控器用）
 - 5V升壓電路（穩定電池電壓）
 - MAX481（將RS-232訊號轉成RS-485）
### 伺服器 資料端 
 - Raspberry pi（樹莓派）

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
GPIO5/GPIO4 -> RS485(boud=115200)（分別接到MAX481的DI/RI）  
GPIO2 -> MAX481的~(RE)/DE  
GPIO0 -> 燒錄用（保持Low）  
GPIO4 -> Mode Pin  
ADC -> 偵測電池電量（最大值只有1V，所以要分壓！）  
REST -> GPIO16（喚醒ESP8266用的）  
GPIO12 -> 綠色LED  
GPIO13 -> 紅色LED  

### 伺服器 資料端 
