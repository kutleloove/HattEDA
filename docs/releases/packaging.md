# Windows yayın adayını hazırlama

CI, WERROR Debug testlerinden ve kurulu Release smoke kontrolünden sonra çekirdek
Windows paketini ZIP ve `.sha256` olarak üretir. ZIP içindeki `release-manifest.json`
kaynak commit, sürüm, Qt/derleyici sürümü ve her yük dosyasının SHA256/byte boyutunu
kaydeder. Manifest kendisini listelemez; manifest dahil ZIP bütünü dış checksum ile
doğrulanır. GitHub PR CI'sında kaynak commit sentetik merge commit'i olabilir;
yayın için aynı adım seçilen gerçek tag/commit üzerinde yeniden çalıştırılmalıdır.

Yerelde önce Release build/install ve `scripts/Test-DeployedRelease.ps1` çalıştırılır.
Ardından PowerShell'de, sürümü gerçek aday adına göre değiştirerek:

```powershell
$sourceCommit = git rev-parse HEAD
./scripts/New-ReleasePackage.ps1 `
    -DeploymentDirectory build/windows-mingw-release/dist `
    -OutputDirectory build/windows-mingw-release/packages `
    -Version 0.1.0-candidate -SourceCommit $sourceCommit `
    -QtVersion 6.11.2 -CompilerVersion 'MinGW GCC 13.1.0'
```

Paketleyici mevcut çıktıları üzerine yazmaz. Yeni aday için temiz bir install
dizini ve yeni çıktı dizini kullanılır. Çıktı install dizininin içinde olamaz.
Eksik EXE/Qt/platform/lisans dosyası, test offscreen eklentisi veya dosya sistemi
bağlantısı varsa paketleme durur. Gizli dosyalar da arşive ve manifeste girer.

Bu adım çekirdek paket içindir ve optional router dahil etmez. `autorouter/`
bulunursa doğrulanmamış bir router dağıtımı üretmek yerine hata verir. Router'lı
yayın için mevcut THIRD_PARTY_LICENSES.md sözleşmesi kapsamında eşleşen Freerouting
kaynak arşivi, lisansı ve JRE legal/NOTICE/release dosyaları ayrıca doğrulanmalıdır.
CI aday dosyası bir GitHub release veya temiz Windows makine kabulü değildir.

İndirdikten sonra ZIP SHA256 değeri `.sha256` içindeki değerle karşılaştırılır.
ZIP açılıp gerçek pencerede Türkçe/İngilizce ve açık/koyu tema kontrolü, kayıt/açma,
şema→PCB→CAM akışı ve bağımsız Gerber/Excellon incelemesi tamamlanır. Yayın notları
desteklenmeyen simülasyonları ve geçici footprint ailelerini açıkça listelemelidir.
Paket kaynak commit'i onaylanan tag'e bağlanmadan yayın kabulü tamamlanmış sayılmaz.

Plain PowerShell `ReleasePackageTests.ps1` CTest'e kayıtlıdır. Gerçek ZIP içeriği,
gizli dosya, her yük hash'i, arşiv checksum'u, eksik dosya/offscreen/router reddi ve
üzerine yazmama davranışı doğrulanır. Testin dummy DLL'leri sadece paketleme fixture'ıdır;
çalışabilir Release uygulamasının smoke testi ayrı ve önceki adımdır.
