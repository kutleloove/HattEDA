# Devre akışı: sağ tık, netlist, PCB rehberi ve DC

## Sağ tık

Aktif çizim/yerleştirme/taşıma sırasında sağ tık taslağı iptal eder ve seçim aracına geçer. Yerleştirilmiş nesneler korunur. Sonraki tek sağ tık menüyü açar. Boştayken aynı nesne/yol üzerindeki hızlı çift sağ tık yalnız o nesneyi siler; Ctrl+Z geri getirir. Tek sağ tık menüsü, çift tıkla çakışmamak için sistem çift tık süresi kadar bekler. Yol bitirmek için Enter veya çift sol tık kullanılır.

Özellikler menüsünden etiket/metin, konum, desteklenen dönüş, bileşen değeri, footprint ve pin→pad eşlemesi düzenlenir. Seçili sembollerde pin numaraları görünür. Eşleme `1,2` gibi, şema pin sırasına göre PCB pad numaralarını listeler; her pad bir kez kullanılmalıdır. Konum Y değeri editörün aşağı-pozitif koordinatıdır (#16).

## Component modu (Proteus ISIS/ARES akışı, #27)

- **Şema:** Component modu (A) projenin eleman listesini gösterir; yeni proje boş başlar. **Eleman seç...** (`hatteda.devices.pick`, Design menüsünde de var) kütüphane penceresini (`PickDevicesDialog`) açar; ada veya önekle aranır, çoklu seçilip **Projeye ekle** ile listeye eklenir. **Kaldır** seçili elemanı listeden çıkarır; şemada o elemandan parça varsa önce parçaların silinmesi istenir. Liste `.hatt` dosyasında `library.devices` olarak saklanır (ADR-0006). Kütüphanede olmayan ama şemada kullanılan bir eleman (ör. silmeyi geri alınca) listeye kendiliğinden döner.
- Yeni yerleştirilen parçaya varsayılan değer (direnç `1k`, kaynak `5`, kondansatör `100n`...) ve pin sayısı uyan kılıf (`r0603`, `header-1x2`, `c0805`, `sot23`, `soic8`) atanır; pin→pad eşlemesi 1'e 1'dir. Özelliklerden değiştirilebilir.
- **PCB:** Kayra'da Component modu yalnızca şemaya yerleştirilmiş ve kartta henüz olmayan parçaları tasarımcı sırasıyla (R1, R2, V1) listeler. Tıklanan yere yerleştirilen kılıf şema parçasına bağlanır (`sourceId`, etiket, değer, eşleme) ve listeden düşer; geri alınınca tekrar görünür. Kılıfı atanmamış parçalar listelenmez, ipucu alanında bildirilir.
- **PCB'den hariç tut:** Özellikler penceresindeki bu seçenek parçayı PCB listesinden, aktarımdan ve bağlantı rehberinden çıkarır; parça netlistte ve simülasyonda kalır.
- **Netlist to PCB** (Alt+A, `hatteda.action.update-pcb`) bağlı kılıfların etiket/değerini yeniler ve kartta olmayan parçaları otomatik yerleştiriciyle (`autoPlaceParts`) ekler: kart dış çizgisi varsa içine, yoksa tasarımın sağına, mevcut nesnelerle çakışmadan satır satır ve 1.27 mm ızgaraya.
- **Auto placer...** (`hatteda.action.auto-place`, PCB component modunda `hatteda.parts.auto-place` düğmesi) `AutoPlacerDialog` açar: yerleşim ızgarası (`AutoPlacerGrid`) ve elemanlar arası boşluk (`AutoPlacerSpacing`) PCB biriminde girilir, `pcb/autoPlacer/grid|spacing` ayarlarında hatırlanır; tüm bekleyen parçalar tek undo adımıyla yerleşir.
- **Export netlist...** (`hatteda.action.export-netlist`) `.net` metin dosyası yazar: `*PARTS` (referans, eleman, değer, kılıf) ve `*NETS` (`net: R1.1 V1.2`) bölümleri. Portlar/raylar net adı verir ama parça olarak listelenmez.

## Eleman ve kılıf oluşturma (Make Device / Make Package, #29)

- **Yeni eleman...** (`hatteda.devices.new`, Design › New device...) `DeviceEditorDialog` açar. Ad, etiket öneki (1-8 harf), varsayılan değer, pin sayısı ve isteğe bağlı pin adları girilir. Şema sembolü kendiliğinden üretilir: 2 pine kadar küçük kutu, fazlası solda yukarıdan aşağı, sağda aşağıdan yukarı pinli IC kutusu. Oluşan eleman projenin eleman listesine eklenir.
- **Datasheet bilgileri** isteğe bağlıdır: üretici, parça no, bağlantı, kılıf tipi, delikli/SMD, pin aralığı, sıra aralığı, gövde, bacak ölçüleri ve pin başına en fazla akım. 0 değeri "bilinmiyor" demektir.
- **Kılıf** listesi pin sayısı aynı olan hazır ve proje kılıflarını gösterir. **Elemandan kılıf oluştur...** `FootprintEditorDialog` açar. Pad sayısı elemanın pin sayısına kilitlidir. Sağdaki `DeviceInfoBox` elemanın bilgilerini gösterir:
  - Datasheet geometrisi varsa pin aralığı, sıra aralığı ve pad ölçüleri ondan üretilir (`suggestFootprint`).
  - Pin akımı girilmişse IPC-2221'e göre (1 oz, 10 °C) en az bakır genişliği gösterilir. Pin aralığı izin verdiği ölçüde padler genişletilir ve yolların da en az bu genişlikte çizilmesi önerilir.
  - Hiç bilgi yoksa yalnız pin sayısı bilinir. Kılıf tam o sayıda padle, genel 2.54 mm delikli bir başlangıçla açılır.
- **Pin → pad** tablosu (`DevicePinMap`) kılıf seçilince dolar. Her pin için pad numarası seçilir, her pad bir kez kullanılmalıdır. Yeni yerleştirilen parçalar bu eşlemeyi alır.
- **Yeni kılıf...** (Design menüsü) aynı kılıf penceresini elemansız açar: iki uçlu, tek sıra, çift sıra (DIP/SOIC) veya dört kenar (QFP) dizilim; canlı önizleme; çakışan padler ve padden büyük delik reddedilir.
- Kanvasta çizilen pad ve serigrafiden kılıf yapma (Make Package, #28) aynı `FootprintDefinition` yapısının açık geometri (`pads` + `pins` + `shapes`) biçimini kullanır.
- Hepsi `.hatt` dosyasında `library.customDevices` ve `library.customFootprints` olarak saklanır (ADR-0007). Kütüphane belgelerden önce okunur. Geçersiz bir satır dosyanın açılmasını adıyla belirtilen bir hatayla durdurur.

## Kayra katmanları, pad/via ve Make Package (#28, #30)

- **Modlar:** Kılıf (K) hazır ve proje kılıflarını şemadan bağımsız yerleştirir. Via (I) ve Pad (O) modları stil listesi gösterir. Track (W) modunda T8–T100 track stilleri listelenir. **Yeni stil...** ile kendi track, via veya pad stilinizi oluşturursunuz. Stiller uygulama ayarıdır; yerleştirilen öğe ölçüsünü kopyalar.
- **Aktif katman** sol alttaki `ActiveLayer` kutusundan seçilir. Yol, zone ve SMD pad aktif bakıra, 2B çizimler aktif katmana (bakır seçiliyse o yüzün serigrafisine) gider. Alt katman aktifken kılıf alt yüze aynalı yerleşir. Space üst/alt bakırı değiştirir, Page Up / Page Down seçer. Yol çizerken katman değiştirmek son köşeye via koyar ve yola yeni katmanda devam eder; Backspace son katman değişikliğini geri alır. Gizli katmanlar çizilmez ve seçilemez.
- **Bağlantı:** Yollar yalnız aynı bakır katmanında birleşir; farklı katmanlar via veya delikli pad üzerinden bağlanır. SMD pad yalnız kendi katmanındaki yola bağlanır.
- **Make Package** (Design › Make package..., kart sağ tık menüsü): Pad modu ile padleri yerleştirin, dış çizgiyi Üst serigrafiye çizin, hepsini seçin. Ad ve orijin (pad 1 veya padlerin merkezi) girilir; kılıf `library.customFootprints` içine açık geometriyle kaydedilir, Kılıf modunda ve eleman penceresinin kılıf listesinde görünür. Varsayılan olarak seçim yeni kılıfla değiştirilir. Numaraları 1..n olmayan padler yeniden numaralanır; yalnız alt yüzde çizilmiş kılıf üstten görünüşüyle saklanır. Yol, yazı ve diğer katmanlar yok sayılır.
- **Decompose** (Design › Decompose) seçili kılıfları aynı yer ve ölçüde düzenlenebilir Pad öğelerine ve serigrafi çizgilerine ayırır; tek undo adımıdır. Parçalanan padler şema parçasına bağlı kalmaz.

## Simülasyon başlat/durdur (#22)

Komut çubuğundaki oynat düğmesi veya **Circuit → Start simulation** (F12) canlı simülasyonu başlatır. Probe modundaki **Voltage probe** bir tele veya pine konduğunda gerilimi şemada etiket olarak görünür. Simülasyon çalışırken değer değiştirmek, parça eklemek veya taşımak devreyi yeniden çözer. Durdur düğmesi (Shift+F12) etiketleri kaldırır. Hata olursa sonuç sekmesi açılır ve simülasyon durur. DC modelinde kondansatör açık devre, bobin kısa devredir.

## Gerber ve delik çıktısı (CAM)

**File → Export Gerber and drill files...** (`hatteda.action.export-fabrication`) Kayra belgesinden
dokuz Gerber X2 katmanı (üst/alt bakır, serigrafi, lehim maskesi, pasta ve kart kenarı) ile bir
Excellon PTH delik dosyası üretir. Pad, via, yol, bakır alan, kılıf serigrafisi ve kart kenarı aynı
`itemPads`, katman ve geometri yardımcılarından okunur. Editörün aşağı-pozitif Y koordinatı CAM'de
yukarı-pozitif olacak şekilde çevrilir. Sonuçlar atomik olarak seçilen klasöre yazılır ve
`hatteda.tool.gerber-viewer` sekmesi açılır: soldaki `CamLayerList` ile katmanlar ve delikler
açılıp kapatılır, **Önizleme** (`CamPreview`) dosyalara yazılan aynı primitifleri katman renkleriyle
çizer (tekerlek yakınlaştırır, sürükleme kaydırır, sağ tık sığdırır), `GerberFileList`'ten seçilen
dosyanın ham metni **Dosya metni** sekmesinde görünür. Yeni bir dışa aktarma önceki sekmenin yerini alır.

Dışa aktarmadan önce DRC çalışır. **Hata** varsa `FabricationChecksDialog` sorar: **Yine de dışa aktar**,
**Raporu aç** (tasarım denetimi sekmesini açar, dosya yazmaz) veya **İptal**. Uyarılar soru çıkarmaz,
sayıları durum satırında bildirilir.

**Bakır alan dolgusu (ADR-0009):** Bir bakır alanın (copper zone) özelliklerinden **Net** seçilir
(`ItemZoneNet`; şemadaki netler listelenir, ad elle de yazılabilir). Netli alan kendi katmanında
dökülür: alan poligonu, kart dış çizgisi varsa kenar boşluğu kadar içeride kalır ve başka netlerin
bakırından tasarım kurallarındaki clearance kadar oyulur. Aynı netin padleri dolguya termal bağlantıyla
(0.3 mm boşluk halkası ve dört 0.4 mm kol), yolları ve via'ları doğrudan bağlanır; 0.25 mm'den ince
dolgu şeritleri ve netin hiçbir bakırına değmeyen dolgu adacıkları silinir (netin kartta bakırı yoksa alan boş kalır). Dolgu kanvasta katman renginde çizilir; belge, şema veya kurallar
değişince yeniden hesaplanır. Gerber'de dolgu katmanın en başına yazılır, oyuklar LPC (clear) bölge
olarak çıkar. Netsiz alan dökülmez ve Gerber'e **yazılmaz**, çünkü katı bakır kapsadığı bütün netleri
kısa devre ederdi; Gerber sekmesinin üstünde `FabricationZonesNotice` uyarısı ve durum satırında sayısı
görünür (`CamOptions::includeZones` yalnız bilinçli kullanım içindir).

Kılıf etiketleri (R1, U1) kılıfın serigrafi katmanına 1 mm yüksekliğinde tek çizgili fontla
(`StrokeFont.hpp`) yazılır; alt yüzdekiler aynalanır. Yerleşim kılıfın dönüşünü izler
(`designatorPlacement`): 0° ve 180°'de kılıfın üstünde yatay, 90° ve 270°'de kılıfın solunda aşağıdan
yukarı okunacak şekilde dikey. Kanvastaki etiket de aynı yerde çizilir. Kartta yerleştirilen metinler kendi
katmanına aynı fontla çıkar; kart kenarındaki metin atlanır ve sayısı bildirilir. Via'lar tented kabul
edilir ve tüm delikler kaplamalıdır.

### Montaj dosyaları: BOM ve pick and place

- **File → Export bill of materials...** (`hatteda.action.export-bom`, `ManufacturingExport.hpp`):
  - Şemadaki parçaları eleman, değer ve kılıf aynıysa tek satırda toplar.
  - Satırlar ilk etikete göre sıralanır; etiketler de kendi içinde doğal sırada yazılır (R2, R10).
  - PCB'den hariç tutulan parçalar listeye girmez.
  - Proje elemanlarının üretici ve parça numarası da yazılır.
- **File → Export pick and place...** (`hatteda.action.export-pick-place`):
  - Karttaki her kılıf için şunları yazar: etiket, değer, kılıf, pad sınırlarının merkezi (mm, Y yukarı), üstten bakışla saat yönünün tersine dönüş ve yüz (Top/Bottom).
- İki dosya da UTF-8 CSV'dir (RFC 4180 tırnaklama, CRLF). Montaj servisleri sütunları adıyla okuduğu için başlıklar İngilizcedir.

## Tasarım denetimi: ERC ve DRC (#31)

Komut çubuğundaki denetim düğmesi veya **Design › Run design checks** (`hatteda.action.run-checks`) şemaya ERC, karta DRC uygular. Sonuçlar **Design checks** sekmesinde (`hatteda.tool.design-checks`) listelenir: önce hatalar, sonra uyarılar. Satıra tıklayınca ilgili çalışma alanına geçilir, sorunlu nesneler seçilir ve görünüm oraya ortalanır. **Run again** listeyi yeniler. Kurallar ve kimlikleri ADR-0008'dedir.

- **ERC (şema):**
  - Hata:
    - eksik veya tekrarlanan etiket
    - adı olmayan port
    - birbirine bağlanmış farklı net adları
    - kılıfı olmayan ya da pin-pad eşlemesi bozuk parça (PCB'den hariç tutulanlar hariç)
  - Uyarı:
    - bağlanmamış pin
    - hiçbir parçaya bağlanmayan port, ground veya prob
    - tüm pinleri aynı nette olan parça
    - boşta kalan tel ucu
    - değeri boş parça
- **DRC (kart):** Aynı bakır katmanında birbirine değen yollar, padler ve vialar tek iletken sayılır.
  - Hata:
    - farklı şema netlerini birleştiren bakır (kısa devre); bir bakır alan üzerinden olursa `drc.zone-short`
    - clearance altında kalan farklı iletkenler
    - en küçük yol genişliğinin, deliğin veya halka genişliğinin altı
    - kart dış çizgisinin dışındaki ya da kenara çok yakın bakır
  - Uyarı:
    - üst üste binen kılıflar
    - tamamlanmamış netler
    - karta yerleştirilmemiş parçalar
    - kapalı kart dış çizgisinin olmaması
    - neti olmayan bakır alan: dökülmez, boşluksuz dolu bakır sayılır ve Gerber'e yazılmaz (`drc.zone-unfilled`)
    - neti olan ama altında o netin bakırı olmadığı için hiç bakır dökmeyen alan (`drc.zone-empty`)
  - Neti olan bakır alan dökümüyle (clearance, termal bağlantılar, adacık temizliği) denetlenir. Dökümün değdiği bakır birleşir, örneğin GND dökümü GND netini tamamlar. Başka nete değerse `drc.zone-short`, clearance altında kalırsa `drc.clearance` verir.
- **Design › Design rules...** (`DesignRulesDialog`) şu değerleri mm cinsinden düzenler: bakır aralığı (0.2), en küçük yol (0.15), en küçük delik (0.3), en küçük halka (0.13), kart kenarı mesafesi (0.3). Kurallar `.hatt` dosyasında `rules` nesnesi olarak saklanır; değiştirmek projeyi kaydedilmemiş yapar.

## Çalışan örnek

1. Yeni proje açın; boş Mergen şemasında **Circuit → Load DC divider example** seçin.
2. **Show netlist** pinlerin net üyeliğini gösterir. Metinler düzenleme ve undo sonrası güncellenir.
3. **Update PCB from schematic** örnekteki üç bileşeni Kayra'ya taşır. Kesikli çizgiler eksik bağlantılardır; rota çekildikçe ve bileşenler taşındıkça yeniden hesaplanır. Tekrar aktarım yerleşimi çoğaltmaz; PCB undo/redo desteklenir.
4. **Run DC operating point** sonucu ayrı sekmede açar: V1=5 V, R1=R2=1k için orta düğüm 2.5 V ve direnç akımları 2.5 mA olur. Kaynak akımı pin 1→pin 2 yönünde -2.5 mA'dır.

Kendi devrenizde varsayılan değer ve footprint atamalarını gerekirse özelliklerden değiştirin. Aynı isimli port/güç etiketleri aynı neti paylaşır; ground adı `0` ayrılmıştır. Salt tel kesişimi junction olmadan şemada bağlanmaz; PCB'de aynı bakır katmanında kesişen yollar birleşir, farklı katmanlardakiler yalnız via veya delikli pad üzerinden bağlanır. Şemada bağlantı dolu bir nokta ile gösterilir: bir telin ucu başka bir tele değdiğinde veya üç ya da daha fazla kol birleştiğinde nokta çıkar, noktasız kesişim bağlı değildir. Tel çizerken başka bir telin üstüne tıklayıp devam ederseniz tel orada bölünür ve bağlanır; tıklamadan üstünden geçerseniz kesişim olarak kalır. Çizim sırasında oluşacak noktalar önizleme renginde görünür.

## Sınırlar

- Proje `.hatt` dosyasına kaydedilir (Ctrl+S, Farklı kaydet Ctrl+Shift+S; ADR-0004). Kaydedilmemiş değişiklik varken pencere başlığında `*` görünür ve kapatma/yeni/aç işlemleri kaydetmeyi sorar. Otomatik kayıt, kurtarma, yedek ve kilit dosyaları ADR-0005'te anlatılır.
- Netlist uygulama içinde hesaplanır ve `.net` olarak dışa aktarılabilir; netlist import ve genel SPICE formatı yoktur.
- Bağlantı rehberi otomatik router değildir; airwire'lar pad merkezleri arasında çizilir. Clearance, genişlik, delik, kart kenarı ve katman kontrolleri DRC'dedir (ADR-0008). Neti olmayan bakır alanlar dökülmez ve DRC'de dolu bakır sayılır. Net sınıfı kuralları ve istisna listesi yoktur.
- Footprint/pin eşlemesi değişen veya şemadan silinen bileşenlerin mevcut PCB bağlantıları sessizce dönüştürülmez; kullanıcı incelemesi gerekir.
- Simülasyon direnç, bağımsız DC gerilim kaynağı, kondansatör (açık) ve bobin (kısa) ile DC çalışma noktasıdır; AC/transient, diyot/transistör/opamp modelleri desteklenmez. Desteklenmeyen eleman hata verir. Akım probu henüz değer göstermez.
- Değerler `1k`, `4.7k`, `5`, `1meg`, `1e-3` biçiminde girilir. `M` milli, `MEG` mega; `V`/`ohm` eki eklenmez.
- Şema değiştiğinde sonuçlar geçersiz işaretlenir. Simülasyon iptal edilebilir; maksimum 256 bilinmeyen desteklenir.

Uygulama: `SketchCircuit` adapter, Qt'siz `hatt-electrical`, host tarafından oluşturulan `CircuitWorkflow`, `DesignCanvas` snapshot undo. Karar: ADR-0003. Sonraki işler #21 ve #22 altında izlenir.
