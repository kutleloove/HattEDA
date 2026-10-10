# ADR-0020: Katalog BJT ve MOSFET modellerini DC akışına bağlama

- Durum: Kabul edildi
- Tarih: 2026-10-10
- İlgili: #22, #62, ADR-0017, ADR-0019

## Karar

UI adaptörü katalog pin adlarını BJT için C/B/E, MOSFET için D/G/S sırasına
çevirir. 2N2222 E/B/C, 2N7000 S/G/D ve IRLZ44N G/D/S sıraları korunur.
İlk NPN sembolünün sayısal pinleri mevcut çizimiyle uyumlu olarak B/C/E'dir;
adaptör {2,1,3} sırasını kullanır. Kalıcı kimlik, pin numarası ve PCB eşlemesi değişmez.
Üç pinli proje sembollerinde roller belirsizse simülasyon açık hata verir;
sadece pin sayısına bakarak transistör uçları tahmin edilmez.

BJT genel varsayılanları Is=1e-14 A, BetaF=100, BetaR=1'dir. MOSFET
Vto=+2 V (NMOS), -2 V (PMOS), K=0.001 A/V² kullanır. Bunlar eğitim amaçlı
Ebers-Moll ve level-1 modelleridir; parça numarasına özel datasheet modeli,
termal sınır veya gerçek parça güvenli çalışma alanı iddiası taşımaz.
Parça değer etiketi model parametrelerini değiştirmez.

UI bağlantı kontrolü BJT'nin üç terminalini iletken kabul eder. MOSFET'te
yalnız D/S kanalını bağlar; ideal gate başka bir eleman üzerinden DC referansa
ulaşmalıdır. Bir isim terminali tek başına gate'i referansa bağlamaz.
Akımlar ilk model terminaline giren yönde raporlanır: anot, kolektör veya drain.

Çözücü Qt'den bağımsızdır. UI sadece değişmez snapshot üretir; asenkron çözüm,
iptal ve değişiklik sonrası eski sonuç yönetimi mevcut akıştadır. Proje dosya
şeması veya undo sözleşmesi değişmez. Op-amp/lojik #74 ayrı çözücü çalışmasıdır.

## Doğrulama

Dokuz katalog transistörü gerçek sembol pinlerinden bağımsız gerilim kaynaklarıyla
sınanır; NPN/PNP işaretleri ve kolektör akımı Ebers-Moll referansıyla,
NMOS/PMOS drain akımı kare-kanun referansıyla karşılaştırılır. İdeal gate akımı
sıfırdır. Dört MOSFET'te sürülmeyen gate hatası orijinal paket pin numarasıyla
sınanır. Proje kayıt/açma model uçlarını korur. Diyot ve önceki DC akış
regresyonları da aynı test paketinde kalır.
