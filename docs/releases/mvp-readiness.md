# İlk MVP yayın hazırlığı

Bu liste 2026-10-10 tarihinde `main` (fab9c07), açık issue'lar ve PR #74 incelenerek
hazırlandı. `0.1.0-alpha.1` geliştirici önizlemesi 2026-10-01 tarihinde GitHub üzerinde yayımlandı. Bu çalışma ilk MVP kabulünü tamamlamayı ve sonraki paketi hazırlamayı hedefler. Önizlemenin yayımlanmış olması yayın kabulünün tamamlandığı
anlamına gelmez. İlk hedef Windows x64 üzerinde çevrimdışı çalışan bir geliştirme
önizlemesidir; aşağıdaki kontroller yapılmadan yayın hazır kabul edilmez.

## Mevcut çalışan kapsam

- Mergen/Kayra editörleri, snapshot undo/redo, proje kaydı ve kurtarma.
- Netlist, pin-pad eşleme, PCB aktarımı, ratsnest, ERC/DRC ve net sınıfları.
- İki bakır katmanı, via, zone; Gerber/Excellon, BOM ve yerleşim CSV çıktıları.
- DC çalışma noktası ve iptal/eski sonuç yönetimi; core'da diyot, LED, BJT, MOSFET.
- Veri odaklı proje kütüphanesi ve chip footprint ailesi (#61 ve #75 birleşti).

`tests/unit/MvpFlowTests.cpp` şema → ERC → PCB → rota → DRC → CAM → yeniden
kayıt/açma zincirini sınar. Bu test gerçek masaüstü kullanımının veya fabrikasyon
çıktısının bağımsız incelemesinin yerini tutmaz.

## Yayın için gerekli PR sırası

| İş | Somut eksik ve PR kapsamı | Kabul / kanıt | Bağımlılık |
| --- | --- | --- | --- |
| Yayın kapsamı ve belge doğruluğu | README'deki eski “kayıt/ERC/DRC yok” iddialarını düzelt; bu listeyi ekle | Belgeler kodla tutarlı; kapsam ve kalanlar ayrı | Bu PR |
| Koordinat girişi ([PR #77](https://github.com/kutleloove/HattEDA/pull/77), #58) | Özellikler Y değerini durum çubuğuyla aynı Y-yukarı kuralında göster/gir | mil ve mm, pozitif/negatif Y, kabul/iptal, tek undo, redo, kayıt açma | ADR-0015 güncellemesi |
| Paket smoke kontrolü ([PR #78](https://github.com/kutleloove/HattEDA/pull/78)) | CI smoke işlemi hata halinde temizlenmeli; test için eklenen offscreen DLL dağıtımda kalmamalı | Minimal PATH ile çalışan kurulu Release; hata ve temizleme testleri | ADR-0016 |
| Simülasyon UI bağlantısı (#22/#62) | `SketchCircuit.cpp` yalnız lineer `DcKind` üretiyor; katalog model/parametre/pin rollerini nonlinear DTO'ya bağla | LED, zener, BJT, MOSFET devreleri UI adapter üzerinden beklenen sonuç; geçersiz model açık hata; iptal ve eski sonuç testleri | #74'ün değerlendirilmesi |
| Çözücü tamamlaması | Var olan #74 op-amp/lojik/warm-start PR'ını incele; ikinci PR açma | main üzerinde tam test; besleme ve durumlu devre sınırları ADR'de | Açık PR #74 |
| Gerçek footprint aileleri (#63) | Chip ailesi tamam; kalan THT/SOT/SOIC/DIP ailelerini küçük PR'lara böl | Boyut/pad/pin-1 testleri, kaynak metadata, CAM pad/silk doğrulaması ve aile görüntüleri | #61 tamam |
| Gerçek masaüstü kabulü (#2) | Kısayol, tel/arc/seçim, tema/snap, dosya ve kurtarma akışları | Temiz kullanıcı ayarlarıyla Windows kontrol listesi, ekran görüntüleri ve hata kayıtları | Yukarıdaki düzeltmeler |
| Yayın paketi | Kaynak commit/tag, Windows arşivi ve SHA256; kullanılan Qt/MinGW ve varsa router/JRE notice/kaynakları; temiz makine kontrolü | İndirilen arşivden açılış, örnek proje kayıt/açma ve üretim çıktısı; paket içerik listesi | Başarılı Release ve masaüstü kabulü |

PR açılmış olması kabul ölçütünün geçtiği anlamına gelmez. Uygulama PR'ları kendi
build/test sonuçlarını taşımalı; eksik masaüstü kanıtı ve dış araç testi açıkça
belirtilmelidir. PR'lar bu çalışma sırasında otomatik birleştirilmez ve sürüm yayınlanmaz.

## Uygulama ilerlemesi

- [PR #77](https://github.com/kutleloove/HattEDA/pull/77): koordinat düzeltmesi uygulandı;
  WERROR Debug ve 22/22 test, ayrıca CI başarılı.
- [PR #78](https://github.com/kutleloove/HattEDA/pull/78): paket testinin süreç/DLL/ortam
  temizliği uygulandı. Windows hata penceresini önleyen takip düzeltmesi tam CI'dan geçti;
  geliştirici Qt eklenti yollarını yalıtan ek kontrol de yerelde doğrulandı.
- [PR #79](https://github.com/kutleloove/HattEDA/pull/79): genel diyot/LED/Zener modelleri
  masaüstü simülasyonuna bağlandı; NC pinler, kompakt netler, kayıt/açma, akımlar ve eski
  sonuçlar sınandı. WERROR Debug ve 22/22 test başarılı. Datasheet'e özgü modeller ve
  animasyon bu dilimde yoktur; model varsayılanları sonuç ekranında açıklanır.

- [PR #80](https://github.com/kutleloove/HattEDA/pull/80): dokuz katalog BJT/MOSFET parçası
  UI DC akışına bağlandı; paket pin sıraları, genel model parametreleri ve boşta gate
  kontrolü doğrulandı. WERROR Debug 22/22 ve tam CI başarılı.
- [PR #81](https://github.com/kutleloove/HattEDA/pull/81): kaynak commit/Qt/derleyici bilgisi,
  dosya manifesti ve SHA256 ile çekirdek Windows ZIP adayı üretiliyor. Tam CI başarılı.
- [PR #82](https://github.com/kutleloove/HattEDA/pull/82): katalog ground/junction/voltage-probe
  kimlikleri eski alias'larla aynı bağlantı, ERC ve POWER sınıfı davranışını kullanıyor.
  Yeniden kayıt/açma regresyonu geçti; CI takip kontrolü sürüyor.

#76–81 ortak `codex/mvp-candidate` dalında WERROR Debug ve 24/24 testten, ayrıca
Release deploy/smoke/paket CI'ından geçti. CI ZIP'i indirilip dış SHA256, tüm yük
dosyalarının hash/boyutları ve kaynak commit'i doğrulandı; indirilmiş EXE geliştirici
Qt/MinGW yolları olmadan tekrar çalıştı ve test eklentisi kaldırıldı.

Bu PR'lar henüz `main`'e birleşmedi. Op-amp/lojik için #74 ayrıca değerlendirilmelidir.
Gerçek pencerede masaüstü etkileşimleri, bağımsız CAM incelemesi ve seçilen gerçek tag
üzerindeki son kabul hâlâ açıktır. CI çekirdek paketi optional router içermez; router'lı
paket için kaynak/JRE notices ve gerçek router kabulü ayrı gereklidir.
## Kütüphane epiğinde kalan ürün işleri

#60'ın kabul ölçütleri ilk MVP'nin tam parça kütüphanesi hedefi için hâlâ açıktır:

1. #63'ün chip dışı aileleri ve öncelikli parça içeriği.
2. #64 sembol varyant seçimi, pin bağlantılarını koruyan tek undo ve simülasyon DTO'sundan
   animasyon. Önce LED ile bir uçtan uca dilim; sonra diğer göstergeler.
3. #65 kullanıcı seçimiyle KiCad import; parser, dönüşüm/rapor ve önizleme ayrı PR'lar.
4. #66 önizlemeli temel devre şablonları; her şablon ERC ve beklenen simülasyon sonucu ile
   doğrulanmalı. NE555/flip-flop gibi zamana bağlı şablonlar DC desteği varmış gibi sunulmamalı.

Bu maddeler tamamlanmadan #60 kapanmaz. Daha dar bir alpha yayınlanırsa, ertelenen
parçalar ve desteklenmeyen simülasyonlar sürüm notlarında açıkça listelenmelidir.

## Sonraki mimari işler

#9 kimlik/Result/logging, #10 fixed-point geometri, #11 command transaction ve
#12–#14 plugin API/loader mevcut snapshot tabanlı alpha akışından ayrı geçişlerdir.
Bunları ilk paket düzeltmelerine karıştırma. #7/#8/#21/#22'nin eski açıklamalarını
bugünkü kabul ölçütleriyle değerlendir; sırf açık olmaları özelliklerin tamamen eksik
olduğu anlamına gelmez. AC/transient, çoklu kart autorouting ve otomatik yerleşim
optimizasyonu sürüm notlarındaki sınırlar olarak takip edilir.

## Yayın kabul kaydı (henüz tamamlanmadı)

- [ ] WERROR Debug build ve tam `ctest --preset debug`.
- [ ] main + değerlendirilen simülasyon PR'ları üzerinde uçtan uca regresyonlar.
- [ ] Release install ve minimal PATH smoke kontrolü.
- [ ] #2 gerçek masaüstü kontrolü; Türkçe/İngilizce ve açık/koyu tema.
- [ ] Kayıt/kurtarma/eski proje geçişi ve bağımsız Gerber/drill incelemesi.
- [ ] Temiz Windows makinesinde indirilen paket ve varsa gerçek Freerouting denemesi.
- [ ] Sürüm notları, kaynak/tag, checksums ve paket notice/kaynakları doğrulandı.
