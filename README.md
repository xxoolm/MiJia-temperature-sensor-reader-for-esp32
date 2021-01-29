# 运行在esp32上的米家蓝牙温湿度计2(LYWSD03MMC)度数读取器
# A simple MiJia temperature and humidity sensor reader for esp32. Model ID:LYWSD03MMC

> **修改并简化自 https://github.com/jaggil/ESP32_Xiaomi-Mijia-LYWSD03MMC**，感谢作者提供的uuid和esp32 ble使用方法。

> **simplify from https://github.com/jaggil/ESP32_Xiaomi-Mijia-LYWSD03MMC**,thanks so much to the original author.

- 功能包括
    1. 按照指定间隔时间读取温湿度计读数
    2. 通过访问url的方式上传至后端

- 需要进行的配置
    1. wifi账号与密码
    2. 温湿度计mac地址
    3. (可选) 读取的时间间隔
    4. (高玩) 直接重写sendToBackend函数达到自己所需的效果

- 编译环境
    - platform io (本人在vscode使用)
    - pio中安装Espressif 32平台
    - pio中使用esp32cam板型
    - 直接替代原main函数即可

~~若源码中出现单词拼写错误等纯属正常~~
