# Devre akışı: sağ tık, netlist, PCB rehberi ve DC

## Sağ tık

Aktif çizim/yerleştirme/taşıma sırasında sağ tık taslağı iptal eder ve seçim aracına geçer. Yerleştirilmiş nesneler korunur. Sonraki tek sağ tık menüyü açar. Boştayken aynı nesne/yol üzerindeki hızlı çift sağ tık yalnız o nesneyi siler; Ctrl+Z geri getirir. Tek sağ tık menüsü, çift tıkla çakışmamak için sistem çift tık süresi kadar bekler. Yol bitirmek için Enter veya çift sol tık kullanılır.

Özellikler menüsünden etiket/metin, konum, desteklenen dönüş, bileşen değeri, footprint ve pin→pad eşlemesi düzenlenir. Seçili sembollerde pin numaraları görünür. Eşleme `1,2` gibi, şema pin sırasına göre PCB pad numaralarını listeler; her pad bir kez kullanılmalıdır. Konum Y değeri editörün aşağı-pozitif koordinatıdır (#16).

## Çalışan örnek

1. Yeni proje açın; boş Mergen şemasında **Circuit → Load DC divider example** seçin.
2. **Show netlist** pinlerin net üyeliğini gösterir. Metinler düzenleme ve undo sonrası güncellenir.
3. **Update PCB from schematic** örnekteki üç bileşeni Kayra'ya taşır. Kesikli çizgiler eksik bağlantılardır; rota çekildikçe ve bileşenler taşındıkça yeniden hesaplanır. Tekrar aktarım yerleşimi çoğaltmaz; PCB undo/redo desteklenir.
4. **Run DC operating point** sonucu ayrı sekmede açar: V1=5 V, R1=R2=1k için orta düğüm 2.5 V ve direnç akımları 2.5 mA olur. Kaynak akımı pin 1→pin 2 yönünde -2.5 mA'dır.

Kendi devrenizde bileşen değerlerini ve footprint atamalarını özelliklerden yapın. Genel sembollere otomatik footprint/model tahmini yapılmaz. Aynı isimli port/güç etiketleri aynı neti paylaşır; ground adı `0` ayrılmıştır. Salt tel kesişimi junction olmadan şemada bağlanmaz; PCB tek bakır katman olduğu için kesişen yollar birleşir. Şemada bağlantı dolu bir nokta ile gösterilir: bir telin ucu başka bir tele değdiğinde veya üç ya da daha fazla kol birleştiğinde nokta çıkar, noktasız kesişim bağlı değildir. Tel çizerken başka bir telin üstüne tıklayıp devam ederseniz tel orada bölünür ve bağlanır; tıklamadan üstünden geçerseniz kesişim olarak kalır. Çizim sırasında oluşacak noktalar önizleme renginde görünür.

## Sınırlar

- Proje `.hatt` dosyasına kaydedilir (Ctrl+S, Farklı kaydet Ctrl+Shift+S; ADR-0004). Kaydedilmemiş değişiklik varken pencere başlığında `*` görünür ve kapatma/yeni/aç işlemleri kaydetmeyi sorar. Otomatik kayıt ve çökme kurtarma henüz yoktur.
- Netlist uygulama içinde hesaplanır; harici netlist import/export ve genel SPICE formatı yoktur.
- Bağlantı rehberi otomatik router veya tam ERC/DRC değildir. Pad merkezi ve tel geometrisi kullanılır; bakır alanı/clearance/via/multilayer kontrolleri yoktur.
- Footprint/pin eşlemesi değişen veya şemadan silinen bileşenlerin mevcut PCB bağlantıları sessizce dönüştürülmez; kullanıcı incelemesi gerekir.
- Simülasyon yalnız direnç ve bağımsız DC gerilim kaynağıyla çalışma noktasıdır; AC/transient, diyot/transistör/opamp modelleri desteklenmez. Desteklenmeyen eleman hata verir.
- Değerler `1k`, `4.7k`, `5`, `1meg`, `1e-3` biçiminde girilir. `M` milli, `MEG` mega; `V`/`ohm` eki eklenmez.
- Şema değiştiğinde sonuçlar geçersiz işaretlenir. Simülasyon iptal edilebilir; maksimum 256 bilinmeyen desteklenir.

Uygulama: `SketchCircuit` adapter, Qt'siz `hatt-electrical`, host tarafından oluşturulan `CircuitWorkflow`, `DesignCanvas` snapshot undo. Karar: ADR-0003. Sonraki işler #21 ve #22 altında izlenir.
