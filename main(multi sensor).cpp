#include <BLEDevice.h>
#include <Esp.h>
#include <WiFi.h>
#include <HTTPClient.h>

void connectWifi();
bool connectSensor(BLEAddress address);
void sendToBackend(double temp, double humi, double batt, BLERemoteCharacteristic *current);
void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic_THB, uint8_t *pData, size_t length, bool isNotify);
double dewPointC(double celsius, double humidity);
void disconnectSensor(BLERemoteCharacteristic *bleChara);
std::string replaceStr(std::string srcStr, std::string oldStr, std::string newStr);

// 无线网路SSID,密码配置
// your wifi configuration
const char *ssid = "***SSID***";
const char *password = "***password***";
// 传感器的mac地址 在米家-传感器-右上角-关于 可以查看
// your sensor mac
static std::string addresses[] = {"mac address1", "mac address2"};
// 控制读取延时 单位 毫秒
static unsigned int loopTime = 5 * 60 * 1000;
// static unsigned int loopTime = 20 * 1000;
static BLEClient *pClient;
static HTTPClient *httpClient;
static BLERemoteCharacteristic *pRemoteCharacteristic_THB;
class CustomCallback : public BLEClientCallbacks
{
	void onConnect(BLEClient *pclient)
	{
		Serial.println("BLE Connected");
	}
	void onDisconnect(BLEClient *pclient)
	{
		Serial.println("BLE Disconnected");
	}
};
static bool isReceiveNotification = false;

//BLE 相关uuid
static BLEUUID serviceUUID("ebe0ccb0-7a0a-4b0c-8a1a-6ff2997da3a6");
static BLEUUID charUUID("ebe0ccc1-7a0a-4b0c-8a1a-6ff2997da3a6");
static BLEUUID charUUID_SetNotify("00002902-0000-1000-8000-00805f9b34fb");

float temp;
float humi;
float bat;

void setup()
{
	Serial.begin(115200);
	Serial.println("booting MiHTSensor reader");
	BLEDevice::init("ESP32");
	connectWifi();
	httpClient = new HTTPClient();

	//初始化BLE客户端
	pClient = BLEDevice::createClient();
	pClient->setClientCallbacks(new CustomCallback());
}

void loop()
{
	//对于每一个address
	for (std::string address : addresses)
	{
		int retryCount = 0;
		bool success = true;
		Serial.printf("\nconnecting to %s\n", address.c_str());
		BLEAddress bleAddress(address);
		//尝试三次连接
		while (!connectSensor(bleAddress))
		{
			retryCount++;
			if (retryCount >= 3)
			{
				success = false;
				Serial.println("---all 3 times failed---");
				break;
			}
			delay(5000);
		}
		//判断连接状态
		if (success)
		{
			Serial.println("sensor connected, waiting for notification");
			//"阻塞"与超时
			retryCount = 0;
			while (!isReceiveNotification)
			{
				retryCount++;
				//十五秒未响应
				if (retryCount >= 15)
				{
					Serial.println("there is no reply from sensor, disconnecting");
					if (pRemoteCharacteristic_THB != NULL)
						disconnectSensor(pRemoteCharacteristic_THB);
					break;
				}
				delay(1000);
			}
		}
		//每个传感器之间缓一缓
		delay(3000);
	}

	delay(loopTime);
}

//连接wifi
void connectWifi()
{
	Serial.println();
	Serial.print("wifi connecting");
	WiFi.begin(ssid, password);
	int i = 0;
	while (WiFi.status() != WL_CONNECTED)
	{
		i++;
		delay(500);
		Serial.print(".");
		if (i > 20)
		{
			Serial.println("fail to cannect, restarting...");
			ESP.restart();
		}
	}
	Serial.print("\n");
	Serial.println("---wifi connected---");
}

bool connectSensor(BLEAddress address)
{
	pClient->connect(address);
	String currentAddress = pClient->getPeerAddress().toString().c_str();
	if (pClient->isConnected() == true)
	{
		Serial.print("connected to ");
		Serial.print(currentAddress);
		Serial.printf("\nsignal strength= %d \n", pClient->getRssi());

		BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
		if (pRemoteService == nullptr)
		{
			Serial.print("connection broken: fail to get service");
			pClient->disconnect();
			return false;
		}
		pRemoteCharacteristic_THB = pRemoteService->getCharacteristic(charUUID);
		if (pRemoteCharacteristic_THB == nullptr)
		{
			Serial.print("connection broken: fail to get characteristic THB");
			pClient->disconnect();
			return false;
		}
		else
		{
			isReceiveNotification = false;
			pRemoteCharacteristic_THB->registerForNotify(notifyCallback);
			return true;
		}
	}
	else
	{
		Serial.println("fail to connect");
		return false;
	}
}

//收到notification之后的回调
void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic_THB, uint8_t *pData, size_t length, bool isNotify)
{
	Serial.println("data received");
	temp = (pData[0] | (pData[1] << 8)) * 0.01;
	humi = pData[2];
	bat = (pData[3] | (pData[4] << 8)) * 0.001;
	Serial.printf("TEMP %.2f C - HUMI %.2f %% - BATT= %.3f V - DEWP= %.2f C \n", temp, humi, bat, dewPointC(temp, humi));
	Serial.printf("free heap: %d \n", esp_get_free_heap_size());
	sendToBackend(temp, humi, bat, pBLERemoteCharacteristic_THB);
}

//发送到后端
void sendToBackend(double temp, double humi, double batt, BLERemoteCharacteristic *bleChara)
{
	if (WiFi.status() != WL_CONNECTED)
	{
		Serial.println("wifi disconnected restarting...");
		if (pClient->isConnected() == true)
			pClient->disconnect();
		ESP.restart();
	}
	else
	{
		Serial.println("connecting to backend...");
		char url[150];
		std::string mac = replaceStr(pClient->getPeerAddress().toString(),":","-");
		sprintf(url, "http://192.168.50.188:8672/api/upload?temp=%.2f&humi=%.2f&batt=%.2f&address=%s", temp, humi, batt, mac.c_str());
		Serial.println(url);
		httpClient->begin(url);
		httpClient->setTimeout(5000);
		int statusCode = httpClient->GET();
		if (statusCode < 0)
		{
			Serial.println("an error occurred:");
			Serial.println(httpClient->errorToString(statusCode));
			Serial.println("---finished---");
		}
		else
		{
			Serial.printf("response code:\n %d\nresponse text:\n", statusCode);
			Serial.println(httpClient->getString());
			Serial.println("---finished---");
		}
		//关掉notification 免得异步断开连接时收到通知引起的错误
		disconnectSensor(bleChara);
		isReceiveNotification = true;
	}
}

void disconnectSensor(BLERemoteCharacteristic *bleChara)
{
	uint8_t val[] = {0x00, 0x00};
	bleChara->getDescriptor((uint16_t)0x2902)->writeValue(val, 2, true);
	pClient->disconnect();
	//等三秒让他处理断开
	delay(3000);
}

//温湿度换算露点
double dewPointC(double celsius, double humidity)
{
	//double celsius = readTempC();
	//double humidity = readFloatHumidity();

	// (1) Saturation Vapor Pressure = ESGG(T)
	double RATIO = 373.15 / (273.15 + celsius);
	double RHS = -7.90298 * (RATIO - 1);
	RHS += 5.02808 * log10(RATIO);
	RHS += -1.3816e-7 * (pow(10, (11.344 * (1 - 1 / RATIO))) - 1);
	RHS += 8.1328e-3 * (pow(10, (-3.49149 * (RATIO - 1))) - 1);
	RHS += log10(1013.246);
	double VP = pow(10, RHS - 3) * humidity; // factor -3 is to adjust units - Vapor Pressure SVP * humidity
											 // (2) DEWPOINT = F(Vapor Pressure)
	double T = log(VP / 0.61078);			 // temp var
	return (241.88 * T) / (17.558 - T);
}

std::string replaceStr(std::string srcStr, std::string oldStr, std::string newStr)
{
    std::string::size_type pos = 0;
    while((pos = srcStr.find(oldStr)) != std::string::npos)   //替换所有指定子串
    {
        srcStr.replace(pos, oldStr.length(), newStr);
    }
    return srcStr;
}
