TROY LoRa APRS DigiRepeater (ESP32)

TR0Y-5 LoRa Digirepeater by TA3OER


Bu proje, bir ESP32 mikrodenetleyici ve bir LoRa modülü (SX127x serisi) kullanarak bir APRS (Automatic Packet Reporting System) digipeater ve APRS-IS (Internet Service) gateway'i oluşturur. Cihaz, LoRa üzerinden alınan APRS paketlerini Internet'teki APRS-IS ağına iletebilir ve/veya APRS-IS'ten gelen bazı paketleri LoRa ağına aktarabilir. Aynı zamanda, paketin yolu üzerindeki "WIDE1-1" gibi ifadeleri kendi çağrı işaretine (TR0Y-5*) çevirerek APRS paketlerini digipeat etme yeteneğine sahiptir.

Cihazın yönetimi ve yapılandırılması, yerleşik bir web sunucusu üzerinden kolayca yapılabilir.

Özellikler

- LoRa APRS DigiRepeater:
	- 433.775MHz frekansında çalışacak şekilde yapılandırılmıştır (yapılandırılabilir).

	- APRS paketlerini alır ve "WIDE1-1" path'ine sahipse digipeat eder.

	- Tekrarlayan (digipeat edilmiş) paketleri, CALLSIGN* formatında kendi çağrı işaretiyle işaretler.

	- Kendi gönderdiği veya daha önce digipeat ettiği paketleri tekrarlamaz (loop önleme).

	- Çarpışmaları azaltmak için rastgele gecikme ile TX yapar.


- APRS-IS Gateway:
	- LoRa üzerinden alınan APRS paketlerini APRS-IS sunucularına iletir.

	- (Opsiyonel) APRS-IS'ten gelen paketleri LoRa ağına yayınlayabilir (şu an yorum satırı olarak kapatılmıştır, kanal yoğunluğu nedeniyle dikkatli kullanılmalıdır).

	- Kullanıcı adı, şifre, sunucu ve filtre ile yapılandırılabilir.

	- APRS-IS bağlantı durumunu izler ve otomatik olarak yeniden bağlanmayı dener.


- Web Yönetim Arayüzü:
	- Ana Sayfa (/): Cihaz durumu, çalışma süresi, LoRa RX/TX/DIGI istatistikleri, APRS-IS bağlantı bilgileri ve son alınan/digipeat edilen paketlerin geçmişi görüntülenir.

	- Yapılandırma Sayfası (/config):
		- WiFi (STA modu) SSID ve şifre.

		- APRS-IS sunucu adresi, portu, kullanıcı adı, şifresi ve filtre ayarları.

		- LoRa frekansı, Spreading Factor (SF), Bant Genişliği (BW), Coding Rate (CR), TX Gücü (Tx Power).

		- Digipeater'ın Çağrı İşareti (Call Sign), Durum Mesajı (Status Message) ve Yorum Mesajı (Comment Message).


	- Yönetim Butonları: Durum (Status) ve Yorum (Comment) paketi gönderme, LoRa modülünü yeniden başlatma ve cihazı yeniden başlatma düğmeleri.

	- Otomatik Yenileme: Ana sayfa, istatistikleri güncel tutmak için her 30 saniyede bir otomatik olarak yenilenir.


- Kalıcı Ayarlar (Preferences): Tüm yapılandırma ayarları, ESP32'nin NVS belleğine kaydedilir ve cihaz yeniden başlatıldığında otomatik olarak yüklenir.

- WiFi Erişim Noktası (AP) Modu:
	- Cihaz açıldığında otomatik olarak AP modunda başlar (SSID: TROY-DigiRepeater, Şifre: 12345678).

	- BOOT butonuna 3 saniye basılı tutularak manuel olarak da AP modu başlatılabilir.

	- AP modu, otomatik olarak 5 dakika sonra kapanır (yapılandırılabilir).


- Görsel Geri Bildirim: Yerleşik LED (GPIO2), cihazın durumu (AP modu, paket gönderme) hakkında görsel geri bildirim sağlar.

- Periyodik Mesajlar: Belirli dakikalarda otomatik olarak APRS Durum ve Yorum paketleri gönderir.

- APRS-IS Yapılandırması
Web arayüzünden:

Sunucu: rotate.aprs2.net
Port: 14580
Kullanıcı adı: Çağrı işaretiniz
Şifre: APRS-IS şifreniz
Filtre: r/lat/lon/radius (örn: r/40.1479/26.4324/50)

Kullanım
Web Arayüzü

Ana sayfa: İstatistikler ve son paketler
Yapılandırma: Tüm ayarları değiştirme
Durum Gönder: Manuel status beacon
Yorum Gönder: Manuel comment beacon
LoRa Restart: LoRa modülünü yeniden başlat
Sistem Reboot: Cihazı yeniden başlat

Periyodik İletimler

Status Beacon: Her saatin 30. ve 59. dakikasında
Comment Beacon: 5, 25 ve 45. dakikalarda

LED Göstergeleri

Yavaş yanıp sönme: AP modu aktif
Tek yanıp sönme: Paket digipeat edildi
İki yanıp sönme: Status/Comment gönderildi
Üç yanıp sönme: Sistem başlatıldı

Gerekli Donanım

- ESP32 Geliştirme Kartı: (Örn. ESP32 DevKitC, NodeMCU-32S, Wemos D1 R32 vb.)

- LoRa Modülü: SX1276, SX178 (433MHz için Ra-02) veya benzeri.

- Anten (433MHz LoRa için).

- USB-C veya Micro-USB kablosu (ESP32 kartınıza bağlı olarak).

Bağlantı Şeması (Örnek - Pinler Kodda Tanımlıdır)


LoRa modülü ile ESP32 arasındaki pin bağlantıları aşağıdaki gibi olmalıdır (kodunuzdaki tanımlamalara göre):


LoRa Modülü Pini	ESP32 GPIO Pini
MISO	GPIO19
MOSI	GPIO27
SCK	GPIO5
NSS (CS)	GPIO18
RESET (RST)	GPIO14
DIO0	GPIO26
GND	GND
VCC	3.3V
LED Pini: Dahili LED (veya harici bir LED) için GPIO2 kullanılmaktadır.

BOOT Butonu Pini: AP modunu manuel tetiklemek için GPIO0 kullanılmaktadır (ESP32 kartlarındaki BOOT/FLASH butonu).


Seri Monitör:

	- Kodu yükledikten sonra, VS Code'un altındaki durum çubuğunda bulunan "Monitor" (Prize benzer simge) butonuna tıklayarak Seri Monitörü açın.

	- monitor_speed = 115200 ayarı platformio.ini dosyasında doğru olduğu için, monitör de otomatik olarak 115200 baud hızında açılacaktır.


Kullanım ve Yapılandırma


Cihazı ilk kez çalıştırdığınızda veya fabrika ayarlarına döndürüldüğünde:


1. AP Moduna Bağlanma: Cihaz otomatik olarak bir WiFi Erişim Noktası (AP) oluşturacaktır.
	- WiFi Ağ Adı (SSID): TROY-DigiRepeater

	- Şifre: 12345678

	- Bilgisayarınız veya telefonunuzla bu ağa bağlanın.

	- Alternatif olarak, cihazın BOOT (GPIO0) butonuna 3 saniye basılı tutarak AP modunu manuel olarak başlatabilirsiniz.


2. Web Arayüzüne Erişme:
	- Bir web tarayıcısı açın ve adres çubuğuna http://192.168.4.1 yazın.

	- Cihazın ana sayfasını (/) göreceksiniz.


3. Yapılandırma:
	- Ana sayfadaki "Yapılandırma" butonuna tıklayın veya http://192.168.4.1/config adresine gidin.

	- Bu sayfada WiFi STA (İnternet) bağlantı bilgilerinizi, APRS-IS ayarlarınızı, LoRa parametrelerinizi ve digipeater'ın çağrı işaretini, durum ve yorum mesajlarını girebilirsiniz.

	- Tüm ayarları girdikten sonra "Kaydet ve Yeniden Başlat" butonuna tıklayın. Ayarlar kaydedilecek ve cihaz yeni ayarlarla yeniden başlayacaktır.


4. İnternet (STA) Modunda Çalışma:
	- Eğer bir WiFi ağına bağlanmak için STA ayarlarını yaptıysanız, cihaz yeniden başladıktan sonra belirtilen WiFi ağına bağlanmaya çalışacaktır.

	- Bağlantı başarılı olursa, APRS-IS gateway işlevi aktif hale gelecektir.

	- Cihaza artık yerel ağınızdaki IP adresinden erişmeniz gerekecektir (bu IP adresi seri monitörde veya ana sayfadaki WiFi STA bölümünde görünecektir).


Sorun Giderme

- Seri Çıktıda Bozuk Karakterler: PlatformIO Seri Monitörünüzün platformio.ini dosyasında monitor_speed = 115200 ayarının doğru olduğundan emin olun.

- LoRa Başlatılamadı Hatası: LoRa modülü bağlantılarını kontrol edin (özellikle RST ve DIO0 pinleri). SPI.begin() içindeki pin tanımlamalarının ve LoRa.setPins() içindeki pin tanımlamalarının doğru olduğundan emin olun.

- APRS-IS Bağlantı Sorunları: WiFi STA bağlantınızın çalıştığından emin olun (APRS-IS Gateway için İnternet gereklidir). APRS-IS sunucu adresini, portunu ve login bilgilerinizi (callSign ve APRS-IS password) doğru girdiğinizden emin olun. Firewall ayarlarınızı kontrol edin.

- Web Arayüzüne Erişilemiyor (AP modunda): AP modunun aktif olduğundan ve 5 dakikalık sürenin dolmadığından emin olun. Cihazın BOOT butonuna basılı tutarak AP modunu yeniden başlatmayı deneyin.
