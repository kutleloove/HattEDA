#include "hatt/ui/PrintLayoutDialog.hpp"

#include <QCheckBox>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

using namespace hatt::ui;

namespace {

// A 50 × 40 mm board with a 4 mm square pad at (10, 10) on bottom copper, in editor coordinates.
SketchDocument sampleBoard() {
    SketchItem outline;
    outline.kind = SketchItem::Kind::Polyline;
    outline.variant = BoardOutlineVariant;
    outline.closed = true;
    outline.layer = BoardLayer::BoardEdge;
    outline.points = {{0, 0}, {50, 0}, {50, 40}, {0, 40}};
    SketchItem pad;
    pad.kind = SketchItem::Kind::Pad;
    pad.points = {{10, 10}};
    pad.layer = BoardLayer::BottomCopper;
    pad.pad = {1, PadShape::Rect, 4.0, 4.0, 0.0, layerBit(BoardLayer::BottomCopper)};
    return {outline, pad};
}

int copperBit() { return 1 << static_cast<int>(CamLayerKind::BottomCopper); }

} // namespace

class PrintLayoutTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void paperAndOrientation();
    void artworkUsesTheBoardOutline();
    void fitFillsThePageInTheBetterOrientation();
    void sideBySidePlacesOneBoxPerLayer();
    void paintedPageHasCopperWhereThePadIs();
    void dialogFitsAndSavesPdf();

private:
    QTemporaryDir settingsDir_;
};

void PrintLayoutTests::initTestCase() {
    QVERIFY(settingsDir_.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("HattEDA-Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("hatt-print-layout-tests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
}

void PrintLayoutTests::paperAndOrientation() {
    QCOMPARE(paperSize(PaperPreset::A4), QSizeF(210, 297));
    QCOMPARE(paperSize(PaperPreset::A5), QSizeF(148, 210));
    PrintSettings settings;
    settings.paper = paperSize(PaperPreset::A3);
    QCOMPARE(pageSize(settings), QSizeF(297, 420));
    settings.landscape = true;
    QCOMPARE(pageSize(settings), QSizeF(420, 297));
}

void PrintLayoutTests::artworkUsesTheBoardOutline() {
    const CamOutput output = buildCamOutput(sampleBoard());
    const QRectF bounds = artworkBounds(output, copperBit());
    QCOMPARE(bounds.size(), QSizeF(50, 40));
    QCOMPARE(bounds.left(), 0.0);
    QCOMPARE(bounds.top(), -40.0); // CAM Y points up
}

void PrintLayoutTests::fitFillsThePageInTheBetterOrientation() {
    PrintSettings settings; // A4 portrait, 10 mm margin, 5 mm spacing
    const QSizeF board(50, 40);
    // Upright: 3 across (3·50 + 2·5 = 160 ≤ 190), 6 down (6·40 + 5·5 = 265 ≤ 277) = 18.
    // Turned:  4 across (4·40 + 3·5 = 175), 5 down (5·50 + 4·5 = 270) = 20.
    const PrintFit fit = fitCopies(settings, board, true);
    QCOMPARE(fit.columns * fit.rows, 20);
    QVERIFY(fit.rotate);
    const PrintFit upright = fitCopies(settings, board, false);
    QCOMPARE(upright.columns, 3);
    QCOMPARE(upright.rows, 6);

    settings.columns = fit.columns;
    settings.rows = fit.rows;
    settings.rotate = true;
    int dropped = -1;
    const auto tiles = layoutTiles(settings, board, &dropped);
    QCOMPARE(tiles.size(), 20);
    QCOMPARE(dropped, 0);
    for (const auto& tile : tiles) QVERIFY(QRectF(10, 10, 190, 277).contains(tile.page));

    settings.columns = 9; // too many across: the extra boxes are reported, not squeezed in
    QCOMPARE(layoutTiles(settings, board, &dropped).size(), 20);
    QCOMPARE(dropped, 5 * 5);
}

void PrintLayoutTests::sideBySidePlacesOneBoxPerLayer() {
    PrintSettings settings;
    settings.layers = copperBit() | (1 << static_cast<int>(CamLayerKind::TopCopper)) |
                      (1 << static_cast<int>(CamLayerKind::Outline));
    settings.grouping = PrintGrouping::SideBySide;
    settings.columns = 1;
    settings.rows = 2;
    const auto tiles = layoutTiles(settings, QSizeF(50, 40));
    QCOMPARE(tiles.size(), 4);
    QCOMPARE(tiles[0].layer, CamLayerKind::TopCopper);
    QCOMPARE(tiles[1].layer, CamLayerKind::BottomCopper);
    QVERIFY(tiles[1].layers & (1 << static_cast<int>(CamLayerKind::Outline)));
    QCOMPARE(tiles[1].page.left(), tiles[0].page.right() + settings.spacing);
    const PrintFit fit = fitCopies(settings, QSizeF(50, 40), false);
    QCOMPARE(fit.columns, 1); // 2·50 + 5 = 105 fits once in 190 mm
}

void PrintLayoutTests::paintedPageHasCopperWhereThePadIs() {
    const CamOutput output = buildCamOutput(sampleBoard());
    PrintSettings settings;
    settings.layers = copperBit();
    settings.margin = 0;
    settings.drills = false;
    const double dotsPerMm = 4.0;
    auto render = [&](const PrintSettings& page) {
        QImage image((pageSize(page) * dotsPerMm).toSize(), QImage::Format_RGB32);
        QPainter painter(&image);
        paintPrintPage(painter, output, page, dotsPerMm, false);
        return image;
    };
    // The pad is 10 mm from the left and top of the board (editor Y down = page Y down).
    QImage page = render(settings);
    QCOMPARE(page.pixelColor(40, 40), QColor(Qt::black));
    QCOMPARE(page.pixelColor(120, 120), QColor(Qt::white));

    settings.mirror = true; // now 10 mm from the right edge of the 50 mm board
    page = render(settings);
    QCOMPARE(page.pixelColor(160, 40), QColor(Qt::black));
    QCOMPARE(page.pixelColor(40, 40), QColor(Qt::white));

    settings.mirror = false;
    settings.colours = PrintColours::Negative;
    page = render(settings);
    QCOMPARE(page.pixelColor(40, 40), QColor(Qt::white));
    QCOMPARE(page.pixelColor(120, 120), QColor(Qt::black));
    QCOMPARE(page.pixelColor(400, 400), QColor(Qt::white)); // paper outside the board stays white
}

void PrintLayoutTests::dialogFitsAndSavesPdf() {
    QSettings().clear();
    PrintLayoutDialog dialog(buildCamOutput(sampleBoard()), QStringLiteral("board"));
    auto* bottom = dialog.findChild<QCheckBox*>(QStringLiteral("PrintLayer.%1").arg(static_cast<int>(CamLayerKind::BottomCopper)));
    QVERIFY(bottom && bottom->isChecked());
    dialog.fitToPage();
    QCOMPARE(dialog.settings().columns * dialog.settings().rows, 20);
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("PrintSummary"))->text().contains(QStringLiteral("20")));

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("board.pdf"));
    QCOMPARE(dialog.savePdf(path), QString());
    QFile pdf(path);
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    QVERIFY(pdf.read(5) == "%PDF-");
    QVERIFY(pdf.size() > 1000);
}

QTEST_MAIN(PrintLayoutTests)
#include "PrintLayoutTests.moc"
