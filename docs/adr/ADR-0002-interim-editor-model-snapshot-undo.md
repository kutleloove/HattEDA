# ADR 0002 UI kabuğunda geçici editör modeli ve snapshot tabanlı undo

## Durum

Bootstrap için kabul edildi. HATT-003 ve HATT-004 tamamlandığında yerini yeni bir ADR alacaktır.

## Bağlam

Kabuk mimarisini (araç modları, snap, hizalama, workspace başına undo) gerçek kullanıcı etkileşimiyle doğrulamak için çizim yapılabilen bir editör gerekiyordu. Oysa sabit noktalı birimler ve geometri primitifleri (HATT-003) ile command/transaction servisleri (HATT-004) henüz yok. Bu bileşenler olmadan kalıcı bir domain modeli yazmak, sonradan geri dönülmesi zor kararları erken dondurmak anlamına gelirdi.

## Karar

`hatt-ui-shell` içinde, `hatt::ui` namespace'inde bilinçli olarak basit ve geçici bir editör modeli kullanılır:

- `SketchModel.hpp`: `SketchItem` (Symbol, Wire, Line, Polyline, Rectangle, Circle, Arc, Text), `SketchDocument = QVector<SketchItem>`, kayan noktalı milimetre `QPointF` koordinatları, indeks tabanlı seçim. Sembol ve footprint kütüphanesi (`symbolLibrary()`, `schematic.*` ve `board.*` kimlikleri) koda gömülüdür.
- `DesignCanvas`: özel çizilen `QWidget`; araç durum makinesi, snap (`SnapSettings`), hizalama/dağıtma, ölçüm ve zoom/pan. Her canvas kendi `QUndoStack`'ine sahiptir; `MainWindow` bu yığınları tek bir `QUndoGroup` altında toplar ve etkin workspace'in yığınını etkinleştirir.
- Undo snapshot tabanlıdır: her düzenleme `DesignCanvas::pushEdit` ile, değişiklik öncesi ve sonrası tüm `SketchDocument` ile seçimi saklayan bir `DocumentEditCommand` olarak yığına eklenir. `undo()`/`redo()` yalnızca `DesignCanvas::restore` çağırır.

Bu modelin sınırları:

- Dosyaya yazılmaz, okunmaz; `.hatt` formatının bir parçası değildir. Kaydetme komutu devre dışıdır.
- Eklentilere açılmaz; Plugin API ve `ContributionRegistry` bu tiplere bağımlı olamaz.
- Üzerine net/bağlantı, DRC/ERC, simülasyon veya CAM gibi domain özellikleri kurulmaz.
- Bu tipler domain kütüphanelerine taşınmaz; domain kütüphaneleri `hatt-ui-shell`'e bağımlı olmaz.

## Sonuçlar

Olumlu: Araç modeli, snap davranışı, hizalama çubuğu ve workspace başına undo `hatt-design-canvas-tests` ve `hatt-ui-shell-tests` ile şimdiden test edilebilir. Undo, her komut için ters işlem yazmadan doğru çalışır.

Olumsuz: Her düzenleme tüm belgenin iki kopyasını saklar; bellek ve süre belge boyutuyla doğrusal büyür, büyük tasarımlar için uygun değildir. Kayan nokta koordinatları tekrarlanan taşıma/döndürme sonrası yuvarlama hatası biriktirebilir. İndeks tabanlı seçim kalıcı nesne kimliği sağlamaz. Editör mantığı şu an bir UI sınıfının içindedir.

## Geçiş planı

HATT-003 (sabit noktalı birimler, geometri primitifleri) geldiğinde:

- `SymbolShape`, `SymbolDefinition` ve `SketchItem` içindeki `QPointF`/`QLineF`/`QRectF` alanları domain geometri tipleriyle değiştirilir. `itemSegments`, `itemAnchors`, `itemBounds`, `arcSamples`, `distanceToSegment`, `translateItem`, `rotateItemQuarterTurn` gibi serbest fonksiyonlar Qt UI bağımlılığı olmayan geometri kütüphanesine taşınır.
- `DesignCanvas` yalnızca ekran dönüşümünde (`worldToScreen`/`screenToWorld`) kayan noktaya çevirir; snap ve açı kısıtı sabit noktalı değerler üzerinde hesaplanır.
- Mevcut `hatt-design-canvas-tests` davranış testleri (grid snap, pin'de biten kablo, ortogonal kısıt, hizalama) regresyon güvencesi olarak korunur.

HATT-002 ve HATT-004 (kimlikler, command/transaction servisleri) geldiğinde:

- `SketchDocument` ve indeks tabanlı seçim, kalıcı nesne kimlikleri taşıyan domain belge modeliyle değiştirilir; seçim kimlik listesi olur.
- `DocumentEditCommand` ve `pushEdit` kaldırılır. Canvas araçları domain komutları üretir (yerleştir, taşı, döndür, sil, hizala); bir sürükleme tek transaction olarak işlenir. Undo/redo, tam belge kopyası yerine command/transaction servisinin fark (delta) kayıtlarıyla yapılır.
- `QUndoStack`/`QUndoGroup`, transaction servisinin geçmişine bağlanan ince bir adaptör olarak kalabilir; böylece `hatteda.action.undo`/`redo`, workspace başına undo ve "undo aktif aracı değiştirmez" davranışı korunur.
- `DesignCanvas` belgeyi doğrudan değiştirmez; okumayı snapshot/DTO, yazmayı command servisi üzerinden yapar. Bu, eklentilerin aynı yolları kullanmasının ön koşuludur.
- Sembol kütüphanesi koddan çıkarılıp versiyonlu bir kütüphane formatına taşınır; bu ayrı bir ADR gerektirir.

Geçiş tamamlandığında `SketchModel.hpp` ve `DocumentEditCommand` silinir ve bu ADR "Yerini ADR-XXXX aldı" olarak işaretlenir.
