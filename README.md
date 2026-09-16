# HattEDA

[![CI](https://github.com/kutleloove/HattEDA/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/kutleloove/HattEDA/actions/workflows/ci.yml)

HattEDA; elektronik şema ve simülasyon, PCB/CAD, CAM ve üretim otomasyonu için geliştirilen Qt tabanlı bir masaüstü uygulamasıdır. Ürün, başlangıçtan itibaren eklenti katkılarına açık olacak; ancak proje verisi ve domain modeli kararlı servis sınırlarının arkasında kalacaktır.

Bu depo şu anda ilk çalışan geliştirme dilimini içerir:

- **Mergen:** Schematic & Simulation çalışma alanı
- **Kayra:** PCB, CAD & CAM çalışma alanı
- **Tool Workspace Host:** Gerber Viewer, simülasyon raporları ve gelecekteki plugin araçları için açılıp kapanabilen sekmeler
- Qt Widgets tabanlı `QMainWindow`, dock panelleri ve durum çubuğu
- C++20, Qt 6.11.2, CMake ve Ninja ile tekrarlanabilir Windows derlemesi

Ana mimari sözleşme [`docs/HattEDA_Teknik_Gereksinimler_ve_Mimari_Spesifikasyon_v0.2.docx`](docs/HattEDA_Teknik_Gereksinimler_ve_Mimari_Spesifikasyon_v0.2.docx) belgesidir. Kod içinde Mergen ve Kayra pazarlama adları domain sınıf adlarına dönüştürülmez; teknik adlar `SchematicWorkspace`, `BoardWorkspace` ve `ToolWorkspaceHost` olarak kalır.

## Gereksinimler

Bu makinede doğrulanan araç zinciri:

- Qt 6.11.2 MinGW 64-bit: `C:\Qt\6.11.2\mingw_64`
- MinGW 13.1: `C:\Qt\Tools\mingw1310_64`
- CMake: `C:\Qt\Tools\CMake_64\bin\cmake.exe`
- Ninja: `C:\Qt\Tools\Ninja\ninja.exe`

Qt farklı bir dizine kurulmuşsa `CMakePresets.json` içindeki yolları yerel kurulumla eşleştirin. Qt Creator kullanırken kök `CMakeLists.txt` dosyasını açıp Qt 6.11.2 MinGW kitini seçebilirsiniz.

## Derleme ve çalıştırma

PowerShell'de depo kökünden:

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --preset windows-mingw-debug
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset debug
$env:PATH = "C:\Qt\6.11.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
& '.\build\windows-mingw-debug\apps\hatteda-desktop\hatteda.exe'
```

Testler:

```powershell
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --preset debug
```

Release derlemesi:

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --preset windows-mingw-release
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset release
```

QtTest çıktısı CTest tarafından yakalanmadığı için her QtTest programı sonuçlarını `build/<preset>/test-logs/<test>.txt` dosyasına da yazar.

## CI

GitHub Actions (`.github/workflows/ci.yml`) her `main`, `feature/**` ve `work/**` push'unda, `main`'e açılan pull request'lerde ve elle tetiklendiğinde `windows-latest` üzerinde çalışır:

- `jurplel/install-qt-action` ile Qt 6.11.2 MinGW 64-bit, MinGW 13.1 ve Ninja kurulur (yalnızca CI aracıdır, uygulama bağımlılığı değildir) ve önbelleğe alınır.
- `ci-mingw-debug` ile uyarılar hata sayılarak (`HATTEDA_WERROR=ON`) Debug derlenir, `ctest --preset ci-debug` (`QT_QPA_PLATFORM=offscreen`) çalışır; başarısızlıkta `test-logs` artifact olarak yüklenir.
- `ci-mingw-release` ile Release derlemesi (`BUILD_TESTING=OFF`) doğrulanır.

CI preset'leri araç zincirini mutlak yollar yerine ortam değişkenlerinden alır. Yerelde denemek için:

```powershell
$env:QT_ROOT_DIR = 'C:\Qt\6.11.2\mingw_64'; $env:HATTEDA_MINGW_DIR = 'C:\Qt\Tools\mingw1310_64'
$env:PATH = "C:\Qt\Tools\Ninja;$env:PATH"
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --preset ci-mingw-debug -DHATTEDA_WERROR=ON
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset ci-debug
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --preset ci-debug
```

Karar kaydı: [ADR-0005](docs/adr/ADR-0005-continuous-integration.md).

## Depo yapısı

```text
apps/hatteda-desktop/  Uygulama composition root ve main
libs/ui-shell/         MainWindow ve çalışma alanı kabuğu
libs/electrical/       UI'dan bağımsız netlist ve sınırlı DC çözücü
tests/unit/            Hızlı Qt tabanlı birim/UI sözleşme testleri
docs/architecture/     Uygulanabilir mimari notları
docs/adr/              Numaralı mimari karar kayıtları
docs/                   HATT-SPEC-0001 ana gereksinim belgesi
```

## Mimari kurallar

- UI olayları domain iş mantığı taşımaz.
- Pluginler doğrudan `QMainWindow` üzerinde menü, toolbar veya dock oluşturmaz; katkılar ileride `ContributionRegistry` üzerinden kaydedilir.
- Pluginlere mutable domain pointer verilmez. Okuma snapshot/DTO, yazma command/transaction servisleri üzerinden yapılır.
- `.hatt`, `.hattc` ve `.hattplug` formatları birbirinden ayrıdır ve sürümlüdür.
- AI ve bulut uygulamanın çalışması için zorunlu değildir.
- Yeni bağımlılık, kalıcı format alanı veya katmanlar arası bağımlılık ADR ve test olmadan eklenmez.

## Yakın yol haritası

1. `HATT-002`: kimlikler, `Result/Error` ve logging temeli
2. `HATT-003`: fixed-point birimler ve geometri primitive'leri
3. `HATT-004`: command/undo transaction altyapısı
4. `HATT-032`: `.hattplug` manifest şeması ve uyumluluk preflight
5. `HATT-033`: Plugin API ve `ContributionRegistry`
6. `HATT-034`: güvenilir native Qt plugin loader ve örnek plugin

Geçici şema/PCB editörü, sağ tık özellikleri, netlist, PCB aktarımı/bağlantı rehberi ve direnç/DC kaynakları için çalışma noktası simülasyonu uygulanmıştır. Kalıcı proje dosyası, tam ERC/DRC, AC/transient simülasyon ve gerçek plugin yükleme henüz yoktur. [Kullanım ve sınırlar](docs/architecture/circuit-workflow.md).

## Katkı

Hata bildirimleri, öneriler ve pull request'ler memnuniyetle karşılanır. Açık işler [Issues](https://github.com/kutleloove/HattEDA/issues) sekmesinde takip edilir. Büyük değişikliklerden önce ilgili issue üzerinde tartışma açılması ve yukarıdaki mimari kurallara uyulması beklenir.

## Üçüncü taraf araçlar

HattEDA, isteğe bağlı otomatik yol yönlendirme (autorouting) için [Freerouting](https://www.freerouting.org)'i ayrı, harici bir işlem (`java -jar`, Specctra DSN/SES dosya alışverişi) olarak çalıştırabilir. Freerouting GPL-3.0 lisanslıdır ve kaynağı bu depoya gömülmez/link edilmez — bkz. [ADR-0014](docs/adr/ADR-0014-external-autorouter-boundary.md) ve [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Lisans

HattEDA, [GNU General Public License v3.0](LICENSE) altında lisanslanmıştır. Copyright (C) 2026 Murat Çuka.
