# ADR-0021: Gömülü temel proje şablonları

- Durum: Kabul edildi
- Tarih: 2026-10-10
- İlgili: #66, ADR-0004/0017/0019

## Karar

İlk eğitim dilimi DC destekli gerilim bölücü ve akım sınırlamalı LED devresidir.
Şablonlar UI kütüphanesinin Qt kaynaklarında mevcut `.hatt` biçimiyle saklanır.
ProjectTemplates bu dosyaları normal proje okuyucusuyla doğrular ve her yüklemede
öğe kimliklerini yeniden oluşturur. Gömülü dosya yazılmaz; oluşturulan proje
kullanıcının seçtiği ad/konuma normal proje koruma/kayıt akışıyla yazılır.
Kaynak commit'i aynı olsa bile iki yeni proje aynı belge kimliklerini paylaşmaz.
Bu dilimde PCB boş olduğundan kaynak kimliği bağlantısı yeniden eşlenmez;
PCB içeren şablonlar ileride aynı kimlik haritasıyla iki belgeyi eşlemelidir.

Başlık, açıklama ve şema üzerindeki öğretici not Türkçeye çevrilir. Önizlemeler
mevcut DesignCanvas çiziminden açılış anındaki paletle üretilir; farklı bir
şema çizicisi veya sabit temalı raster varlığı eklenmez. Önizleme bir resimdir,
belgeyi değiştirmez. Boş proje varsayılanı mevcut kullanıcı akışını korur.

Yerleştirilmiş parçalar katalogdaki gerçek kimlikler ve chip footprint seçenekleri
ile gelir. İlk dilim animasyon veya fiziksel LED özelliklerine özel model iddia etmez.
LED genel modeliyle 3.3 V/100 ohm devresinde VOUT yaklaşık 1.9214 V'tur; divider
5 V ve iki 1k dirençle 2.5 V'tur. Notlar fiziksel üretim hesabı yerine bu genel
modeli açıklar. Her ikisi de VOUT probu ile çalışma noktası sonucunu gösterir.

NE555, edge-triggered flip-flop, regülatör ve diğer desteklenmeyen/zamana bağlı
şablonlar simüle edilebilir gibi sunulmaz. #66 bu ilk dilimle kapanmaz. Diğer
şablonlar ve animasyonlu varyantlar kendi destek/kabul kanıtlarıyla eklenir.

## Doğrulama

Açık ve koyu temada iki şablonun gerçek yeni-proje diyaloğundan seçilip dosyaya
kaydedilmesi; bağımsız kimlikler, temiz ilk undo durumu, kaynakların değişmezliği,
ERC ve beklenen prob gerilimleri sınanır. Boş proje akışının mevcut regresyonları
aynı test paketinde kalır. Proje dosyası biçimine yeni alan eklenmez.
