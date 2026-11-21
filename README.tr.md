# Osdrop

[![CI](https://github.com/akinalpfdn/osdrop/actions/workflows/ci.yml/badge.svg)](https://github.com/akinalpfdn/osdrop/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Osdrop, tek bir klavye ve fareyle aynı yerel ağdaki birden çok bilgisayarı kullanmanı sağlar.
İmleci ekranın kenarından geçirince komşu bilgisayarda devam eder, klavye de onunla gider. Bir
dosyayı kenardan sürükleyince bıraktığın yere kopyalanır. Bir bilgisayarda kopyalayıp başka birinde
yapıştırabilirsin.

macOS ile Windows arasında her yönde çalışır: Mac ve PC, iki Mac, iki PC, ve tek düzende en çok sekiz
bilgisayar.

English: [README.md](README.md)

## Ne yapar, ne yapmaz

- **Ortak klavye ve fare:** hangi bilgisayarın klavyesini ve faresini kullanıyorsan o, diğerlerini
  yönetir; her bilgisayar kendi monitörlerini ve masaüstünü korur.
- **Dosya ve pano:** sürüklenen dosya ve klasörler bırakıldığı yere kopyalanır; bir bilgisayarda
  kopyalanan metin, görüntü ve dosyalar başka birinde yapıştırılabilir.
- **Ekran paylaşımı değildir.** Hiçbir ekranın görüntüsü bir yere gönderilmez. Bir bilgisayarı
  diğerine ek ekran yapmak için [Osplay](https://github.com/akinalpfdn/osplay)'e bak.
- **Yalnız yerel ağ.** Hesap, bulut ya da aracı sunucu yok.

## Nasıl çalışır

Eşleştirilen bilgisayarlar bir grup oluşturur ve birbirleriyle şifreli bağlantılar kurar. İmleç bir
kenardan geçince, kullandığın bilgisayar kendi imlecini oynatmayı bırakır ve girdini komşuya
gönderir; komşu da onu kendi klavyesi ve faresi kullanılmış gibi uygular. Ayrıntılar:
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Gereksinimler

| | |
|---|---|
| **Mac** | macOS 14 Sonoma ya da sonrası |
| **Windows** | Windows 10 22H2 ya da Windows 11, x64 |
| **Ağ** | Tüm bilgisayarlar aynı yerel ağda, kablolu ya da Wi-Fi |

## Kurulum

### macOS

1. [`Osdrop.dmg`](https://github.com/akinalpfdn/osdrop/releases/latest/download/Osdrop.dmg)
   dosyasını indir, aç ve Osdrop'u Uygulamalar klasörüne sürükle.
2. Osdrop'u başlat; menü çubuğunda durur.

### Windows

1. [`Osdrop-Setup.exe`](https://github.com/akinalpfdn/osdrop/releases/latest/download/Osdrop-Setup.exe)
   dosyasını indir ve çalıştır.
2. Kurulum programı kod imzalı değil, bu yüzden SmartScreen "Windows bilgisayarınızı korudu"
   diyebilir: **Ek bilgi › Yine de çalıştır**'ı seç. Her sürüm, kurulum programının SHA-256
   değerini listeler.
3. Kurulum; uygulamayı, Osdrop Input hizmetini (imleci oynatır ve diğer bilgisayarlar için yazar)
   ve bir güvenlik duvarı kuralını ekler. Osdrop bildirim alanında durur.

## İlk çalıştırma

1. **Mac:** Osdrop sorduğunda **Erişilebilirlik** (klavye ve fareyi okuyup yönetmek için) ve
   **Yerel Ağ** (diğer bilgisayarları bulmak için) izinlerini ver.
2. Bir bilgisayarda Osdrop'un menüsünü (Mac) ya da panelini (Windows) aç ve diğer bilgisayarın
   yanındaki **Eşleştir**'i seç. İki ekranda da aynı altı haneli kod çıkar; ikisinde de onayla.
3. **Ayarlar › Yerleşim**'de bilgisayarların ekranlarını masandaki yerlerine sürükle.
4. İmleci diğer bilgisayara doğru kenardan geçir.

Başka bilgisayarlar da aynı şekilde katılır, en çok sekiz tane. Her bilgisayar **Ayarlar ›
Aygıtlar**'da diğerlerinin neler yapabileceğine kendisi karar verir: kendisini kontrol etmek, dosya
göndermek, panosunu paylaşmak.

### Klavye kısayolları

| İşlem | Mac | Windows |
|-------|-----|---------|
| İmleci bu bilgisayara geri getir | ⌃⌥⌘⎋ | Ctrl+Alt+Win+Esc |
| İmleci bulunduğu bilgisayarda tut (bırakmak için yeniden bas) | ⌃⌥⌘L | Ctrl+Alt+Win+K |

İkisi de **Ayarlar › Girdi**'den değiştirilebilir.

## Güvenlik

Bilgisayarlar bir kez, iki ekranda da 6 haneli kodu onaylayarak eşleşir; tüm trafik uçtan uca
şifrelidir. Her bilgisayar, eşleştiği bilgisayarlardan hangisinin kendisini kontrol edebileceğine,
dosya gönderebileceğine ve panosunu paylaşabileceğine kendisi karar verir. Bkz.
[`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md) ve [`SECURITY.md`](SECURITY.md).

## Sorun giderme

Bkz. [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md).

## Kaynaktan derleme

Bkz. [`docs/BUILDING.md`](docs/BUILDING.md).

## Katkı

Bkz. [`CONTRIBUTING.md`](CONTRIBUTING.md) ve [Davranış Kuralları](CODE_OF_CONDUCT.md).

## Lisans

[MIT](LICENSE) © 2026 Akinalp Fidan.

## Teşekkür

Osdrop; [Monocypher](https://monocypher.org), [Asio](https://think-async.com/Asio/),
[WPF-UI](https://github.com/lepoco/wpfui), [H.NotifyIcon](https://github.com/HavenDV/H.NotifyIcon)
ve [CommunityToolkit.Mvvm](https://github.com/CommunityToolkit/dotnet) üzerine kuruludur. Tam liste:
[`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md).
