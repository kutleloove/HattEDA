# HattEDA

[![CI](https://github.com/kutleloove/HattEDA/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/kutleloove/HattEDA/actions/workflows/ci.yml)

HattEDA; elektronik ┼şema ve sim├╝lasyon, PCB/CAD, CAM ve ├╝retim otomasyonu i├ğin geli┼ştirilen Qt tabanl─▒ bir masa├╝st├╝ uygulamas─▒d─▒r. ├£r├╝n, ba┼şlang─▒├ğtan itibaren eklenti katk─▒lar─▒na a├ğ─▒k olacak; ancak proje verisi ve domain modeli kararl─▒ servis s─▒n─▒rlar─▒n─▒n arkas─▒nda kalacakt─▒r.

Bu depo ┼şu anda ilk ├ğal─▒┼şan geli┼ştirme dilimini i├ğerir:

- **Mergen:** Schematic & Simulation ├ğal─▒┼şma alan─▒
- **Kayra:** PCB, CAD & CAM ├ğal─▒┼şma alan─▒
- **Tool Workspace Host:** Gerber Viewer, sim├╝lasyon raporlar─▒ ve gelecekteki plugin ara├ğlar─▒ i├ğin a├ğ─▒l─▒p kapanabilen sekmeler
- Qt Widgets tabanl─▒ `QMainWindow`, dock panelleri ve durum ├ğubu─şu
- C++20, Qt 6.11.2, CMake ve Ninja ile tekrarlanabilir Windows derlemesi

Ana mimari s├Âzle┼şme [`docs/HattEDA_Teknik_Gereksinimler_ve_Mimari_Spesifikasyon_v0.2.docx`](docs/HattEDA_Teknik_Gereksinimler_ve_Mimari_Spesifikasyon_v0.2.docx) belgesidir. Kod i├ğinde Mergen ve Kayra pazarlama adlar─▒ domain s─▒n─▒f adlar─▒na d├Ân├╝┼şt├╝r├╝lmez; teknik adlar `SchematicWorkspace`, `BoardWorkspace` ve `ToolWorkspaceHost` olarak kal─▒r.

## Gereksinimler

Bu makinede do─şrulanan ara├ğ zinciri:

- Qt 6.11.2 MinGW 64-bit: `C:\Qt\6.11.2\mingw_64`
- MinGW 13.1: `C:\Qt\Tools\mingw1310_64`
- CMake: `C:\Qt\Tools\CMake_64\bin\cmake.exe`
- Ninja: `C:\Qt\Tools\Ninja\ninja.exe`

Qt farkl─▒ bir dizine kurulmu┼şsa `CMakePresets.json` i├ğindeki yollar─▒ yerel kurulumla e┼şle┼ştirin. Qt Creator kullan─▒rken k├Âk `CMakeLists.txt` dosyas─▒n─▒ a├ğ─▒p Qt 6.11.2 MinGW kitini se├ğebilirsiniz.

## Derleme ve ├ğal─▒┼şt─▒rma

PowerShell'de depo k├Âk├╝nden:

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

QtTest ├ğ─▒kt─▒s─▒ CTest taraf─▒ndan yakalanmad─▒─ş─▒ i├ğin her QtTest program─▒ sonu├ğlar─▒n─▒ `build/<preset>/test-logs/<test>.txt` dosyas─▒na da yazar.

## CI

GitHub Actions (`.github/workflows/ci.yml`) her `main`, `feature/**` ve `work/**` push'unda, `main`'e a├ğ─▒lan pull request'lerde ve elle tetiklendi─şinde `windows-latest` ├╝zerinde ├ğal─▒┼ş─▒r:

- `jurplel/install-qt-action` ile Qt 6.11.2 MinGW 64-bit, MinGW 13.1 ve Ninja kurulur (yaln─▒zca CI arac─▒d─▒r, uygulama ba─ş─▒ml─▒l─▒─ş─▒ de─şildir) ve ├Ânbelle─şe al─▒n─▒r.
- `ci-mingw-debug` ile uyar─▒lar hata say─▒larak (`HATTEDA_WERROR=ON`) Debug derlenir, `ctest --preset ci-debug` (`QT_QPA_PLATFORM=offscreen`) ├ğal─▒┼ş─▒r; ba┼şar─▒s─▒zl─▒kta `test-logs` artifact olarak y├╝klenir.
- `ci-mingw-release` ile Release derlemesi (`BUILD_TESTING=OFF`) do─şrulan─▒r.

CI preset'leri ara├ğ zincirini mutlak yollar yerine ortam de─şi┼şkenlerinden al─▒r. Yerelde denemek i├ğin:

```powershell
$env:QT_ROOT_DIR = 'C:\Qt\6.11.2\mingw_64'; $env:HATTEDA_MINGW_DIR = 'C:\Qt\Tools\mingw1310_64'
$env:PATH = "C:\Qt\Tools\Ninja;$env:PATH"
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --preset ci-mingw-debug -DHATTEDA_WERROR=ON
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset ci-debug
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --preset ci-debug
```

Karar kayd─▒: [ADR-0005](docs/adr/ADR-0005-continuous-integration.md).

## Depo yap─▒s─▒

```text
apps/hatteda-desktop/  Uygulama composition root ve main
libs/ui-shell/         MainWindow ve ├ğal─▒┼şma alan─▒ kabu─şu
libs/electrical/       UI'dan ba─ş─▒ms─▒z netlist ve s─▒n─▒rl─▒ DC ├ğ├Âz├╝c├╝
tests/unit/            H─▒zl─▒ Qt tabanl─▒ birim/UI s├Âzle┼şme testleri
docs/architecture/     Uygulanabilir mimari notlar─▒
docs/adr/              Numaral─▒ mimari karar kay─▒tlar─▒
docs/                   HATT-SPEC-0001 ana gereksinim belgesi
```

## Mimari kurallar

- UI olaylar─▒ domain i┼ş mant─▒─ş─▒ ta┼ş─▒maz.
- Pluginler do─şrudan `QMainWindow` ├╝zerinde men├╝, toolbar veya dock olu┼şturmaz; katk─▒lar ileride `ContributionRegistry` ├╝zerinden kaydedilir.
- Pluginlere mutable domain pointer verilmez. Okuma snapshot/DTO, yazma command/transaction servisleri ├╝zerinden yap─▒l─▒r.
- `.hatt`, `.hattc` ve `.hattplug` formatlar─▒ birbirinden ayr─▒d─▒r ve s├╝r├╝ml├╝d├╝r.
- AI ve bulut uygulaman─▒n ├ğal─▒┼şmas─▒ i├ğin zorunlu de─şildir.
- Yeni ba─ş─▒ml─▒l─▒k, kal─▒c─▒ format alan─▒ veya katmanlar aras─▒ ba─ş─▒ml─▒l─▒k ADR ve test olmadan eklenmez.

## Yak─▒n yol haritas─▒

1. `HATT-002`: kimlikler, `Result/Error` ve logging temeli
2. `HATT-003`: fixed-point birimler ve geometri primitive'leri
3. `HATT-004`: command/undo transaction altyap─▒s─▒
4. `HATT-032`: `.hattplug` manifest ┼şemas─▒ ve uyumluluk preflight
5. `HATT-033`: Plugin API ve `ContributionRegistry`
6. `HATT-034`: g├╝venilir native Qt plugin loader ve ├Ârnek plugin

Ge├ğici ┼şema/PCB edit├Âr├╝, sa─ş t─▒k ├Âzellikleri, netlist, PCB aktar─▒m─▒/ba─şlant─▒ rehberi ve diren├ğ/DC kaynaklar─▒ i├ğin ├ğal─▒┼şma noktas─▒ sim├╝lasyonu uygulanm─▒┼şt─▒r. Kal─▒c─▒ proje dosyas─▒, tam ERC/DRC, AC/transient sim├╝lasyon ve ger├ğek plugin y├╝kleme hen├╝z yoktur. [Kullan─▒m ve s─▒n─▒rlar](docs/architecture/circuit-workflow.md).

## Katk─▒

Hata bildirimleri, ├Âneriler ve pull request'ler memnuniyetle kar┼ş─▒lan─▒r. A├ğ─▒k i┼şler [Issues](https://github.com/kutleloove/HattEDA/issues) sekmesinde takip edilir. B├╝y├╝k de─şi┼şikliklerden ├Ânce ilgili issue ├╝zerinde tart─▒┼şma a├ğ─▒lmas─▒ ve yukar─▒daki mimari kurallara uyulmas─▒ beklenir.

## Lisans

HattEDA, [GNU General Public License v3.0](LICENSE) alt─▒nda lisanslanm─▒┼şt─▒r. Copyright (C) 2026 Murat ├çuka.
