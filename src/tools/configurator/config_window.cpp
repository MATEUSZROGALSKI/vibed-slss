#include "config_window.h"

#include "slc/content/discovery_context.h"
#include "slc/content/steam_local_provider.h"
#include "slc/platform/paths.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <unordered_set>
#include <vector>

namespace slc::configurator {
namespace {

QString toQs(const std::string& s) { return QString::fromStdString(s); }

QString toQs(const std::filesystem::path& p)
{
    return QString::fromStdString(p.generic_string());
}

QWidget* row(QWidget* inner, QAbstractButton* side)
{
    auto* wrap = new QWidget;
    auto* h = new QHBoxLayout(wrap);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(inner, 1);
    if (side != nullptr) {
        h->addWidget(side);
    }
    return wrap;
}

} // namespace

ConfigWindow::ConfigWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Steam Library Screensaver — Settings"));
    m_path = config::defaultConfigPath();

    auto* tabs = new QTabWidget;
    tabs->addTab(makeContentTab(), QStringLiteral("Content"));
    tabs->addTab(makeGamesTab(), QStringLiteral("Game Filter"));
    tabs->addTab(makeLayoutTab(), QStringLiteral("Layout"));
    tabs->addTab(makeAnimationTab(), QStringLiteral("Animation"));
    tabs->addTab(makeRenderingTab(), QStringLiteral("Rendering"));
    tabs->addTab(makeAdvancedTab(), QStringLiteral("Enrichment & Advanced"));

    auto* saveBtn = new QPushButton(QStringLiteral("Save"));
    auto* saveAsBtn = new QPushButton(QStringLiteral("Save As…"));
    auto* reloadBtn = new QPushButton(QStringLiteral("Reload"));
    connect(saveBtn, &QPushButton::clicked, this, &ConfigWindow::save);
    connect(saveAsBtn, &QPushButton::clicked, this, &ConfigWindow::saveAs);
    connect(reloadBtn, &QPushButton::clicked, this, &ConfigWindow::reload);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(reloadBtn);
    buttons->addStretch(1);
    buttons->addWidget(saveAsBtn);
    buttons->addWidget(saveBtn);

    auto* central = new QWidget;
    auto* v = new QVBoxLayout(central);
    v->addWidget(tabs, 1);
    v->addLayout(buttons);
    setCentralWidget(central);
    resize(560, 520);

    reload();
}

QWidget* ConfigWindow::makeContentTab()
{
    auto* w = new QWidget;
    auto* f = new QFormLayout(w);

    m_steamRoot = new QLineEdit;
    m_steamRoot->setPlaceholderText(QStringLiteral("auto-detect"));
    auto* browse = new QPushButton(QStringLiteral("Browse…"));
    connect(browse, &QPushButton::clicked, this, &ConfigWindow::browseSteamRoot);
    f->addRow(QStringLiteral("Steam root"), row(m_steamRoot, browse));

    auto* kinds = new QWidget;
    auto* kh = new QHBoxLayout(kinds);
    kh->setContentsMargins(0, 0, 0, 0);
    m_kindHero = new QCheckBox(QStringLiteral("hero"));
    m_kindGrid = new QCheckBox(QStringLiteral("grid"));
    m_kindHeader = new QCheckBox(QStringLiteral("header"));
    m_kindLogo = new QCheckBox(QStringLiteral("logo"));
    m_kindIcon = new QCheckBox(QStringLiteral("icon"));
    for (auto* cb : {m_kindHero, m_kindGrid, m_kindHeader, m_kindLogo, m_kindIcon}) {
        kh->addWidget(cb);
    }
    f->addRow(QStringLiteral("Artwork kinds"), kinds);

    m_installedOnly = new QCheckBox(QStringLiteral("installed games only"));
    f->addRow(QStringLiteral("Discovery"), m_installedOnly);

    m_minImageWidth = new QSpinBox;
    m_minImageWidth->setRange(0, 8192);
    m_minImageWidth->setSuffix(QStringLiteral(" px"));
    f->addRow(QStringLiteral("Min image width"), m_minImageWidth);
    f->addRow(QString(),
              new QLabel(QStringLiteral(
                  "Per-game exclusions live on the Game Filter tab.")));
    return w;
}

QWidget* ConfigWindow::makeGamesTab()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);

    m_gameSearch = new QLineEdit;
    m_gameSearch->setPlaceholderText(
        QStringLiteral("Filter by name or app id…"));
    m_gameSearch->setClearButtonEnabled(true);
    connect(m_gameSearch, &QLineEdit::textChanged, this,
            &ConfigWindow::filterGames);
    v->addWidget(m_gameSearch);

    auto* buttons = new QHBoxLayout;
    auto* includeBtn = new QPushButton(QStringLiteral("Include shown"));
    auto* excludeBtn = new QPushButton(QStringLiteral("Exclude shown"));
    includeBtn->setToolTip(
        QStringLiteral("Check every game matching the current filter"));
    excludeBtn->setToolTip(
        QStringLiteral("Uncheck every game matching the current filter"));
    connect(includeBtn, &QPushButton::clicked, this, [this] {
        setVisibleGamesChecked(true);
    });
    connect(excludeBtn, &QPushButton::clicked, this, [this] {
        setVisibleGamesChecked(false);
    });
    buttons->addWidget(includeBtn);
    buttons->addWidget(excludeBtn);
    buttons->addStretch(1);
    v->addLayout(buttons);

    m_gameList = new QListWidget;
    m_gameList->setUniformItemSizes(true);
    connect(m_gameList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem*) { updateGameCount(); });
    v->addWidget(m_gameList, 1);

    m_gameCount = new QLabel;
    v->addWidget(m_gameCount);
    return w;
}

QWidget* ConfigWindow::makeLayoutTab()
{
    auto* w = new QWidget;
    auto* f = new QFormLayout(w);

    m_layoutMode = new QComboBox;
    m_layoutMode->addItem(QStringLiteral("Focused (single artwork)"),
                          QStringLiteral("focused"));
    m_layoutMode->addItem(QStringLiteral("Collage (multi-image layouts)"),
                          QStringLiteral("collage"));
    m_layoutMode->addItem(QStringLiteral("Matrix (scrolling columns)"),
                          QStringLiteral("matrix"));
    m_layoutMode->addItem(QStringLiteral("Mix (all layouts)"),
                          QStringLiteral("mix"));
    f->addRow(QStringLiteral("Layout mode"), m_layoutMode);

    m_cellCount = new QSpinBox;
    m_cellCount->setRange(1, 64);
    f->addRow(QStringLiteral("Cell count"), m_cellCount);

    m_gutter = new QSpinBox;
    m_gutter->setRange(0, 64);
    m_gutter->setSuffix(QStringLiteral(" px"));
    f->addRow(QStringLiteral("Gutter"), m_gutter);

    m_seed = new QLineEdit;
    m_seed->setPlaceholderText(QStringLiteral("empty = random"));
    f->addRow(QStringLiteral("Layout seed"), m_seed);
    return w;
}

QWidget* ConfigWindow::makeAnimationTab()
{
    auto* w = new QWidget;
    auto* f = new QFormLayout(w);

    m_kenBurnsEnabled = new QCheckBox(QStringLiteral("enabled"));
    f->addRow(QStringLiteral("Ken Burns"), m_kenBurnsEnabled);

    m_kbDurationMs = new QSpinBox;
    m_kbDurationMs->setRange(500, 120000);
    m_kbDurationMs->setSuffix(QStringLiteral(" ms"));
    f->addRow(QStringLiteral("KB duration"), m_kbDurationMs);

    m_kbZoomFrom = new QDoubleSpinBox;
    m_kbZoomFrom->setRange(0.25, 4.0);
    m_kbZoomFrom->setSingleStep(0.05);
    f->addRow(QStringLiteral("KB zoom from"), m_kbZoomFrom);

    m_kbZoomTo = new QDoubleSpinBox;
    m_kbZoomTo->setRange(0.25, 4.0);
    m_kbZoomTo->setSingleStep(0.05);
    f->addRow(QStringLiteral("KB zoom to"), m_kbZoomTo);

    m_collageEnabled = new QCheckBox(QStringLiteral("enabled"));
    f->addRow(QStringLiteral("Collage anim"), m_collageEnabled);

    m_collageFadeInMs = new QSpinBox;
    m_collageFadeInMs->setRange(0, 10000);
    m_collageFadeInMs->setSuffix(QStringLiteral(" ms"));
    f->addRow(QStringLiteral("Fade-in"), m_collageFadeInMs);

    m_collageDriftPx = new QSpinBox;
    m_collageDriftPx->setRange(0, 400);
    m_collageDriftPx->setSuffix(QStringLiteral(" px"));
    f->addRow(QStringLiteral("Drift"), m_collageDriftPx);

    m_transitionMs = new QSpinBox;
    m_transitionMs->setRange(0, 20000);
    m_transitionMs->setSuffix(QStringLiteral(" ms"));
    f->addRow(QStringLiteral("Crossfade"), m_transitionMs);

    m_stillMs = new QSpinBox;
    m_stillMs->setRange(0, 60000);
    m_stillMs->setSuffix(QStringLiteral(" ms"));
    m_stillMs->setSingleStep(500);
    f->addRow(QStringLiteral("Still hold"), m_stillMs);

    m_showBadge = new QCheckBox(QStringLiteral("show game badge"));
    f->addRow(QStringLiteral("Badge"), m_showBadge);
    return w;
}

QWidget* ConfigWindow::makeRenderingTab()
{
    auto* w = new QWidget;
    auto* f = new QFormLayout(w);

    m_targetFps = new QSpinBox;
    m_targetFps->setRange(15, 240);
    m_targetFps->setSuffix(QStringLiteral(" fps"));
    f->addRow(QStringLiteral("Target FPS"), m_targetFps);

    m_vsync = new QCheckBox(QStringLiteral("enabled"));
    f->addRow(QStringLiteral("VSync"), m_vsync);

    m_blurBackground = new QCheckBox(QStringLiteral("enabled"));
    f->addRow(QStringLiteral("Blur background"), m_blurBackground);

    // User post-process shaders: any *.frag dropped in
    // <configDir>/shaders is selectable here as a full-frame effect.
    m_shader = new QComboBox;
    m_shader->addItem(QStringLiteral("(none)"), QString());
    const QString shaderDir = QString::fromStdWString(
        (slc::platform::configDir() / "shaders").wstring());
    const QDir dir(shaderDir);
    for (const auto& fi :
         dir.entryInfoList({QStringLiteral("*.frag")}, QDir::Files,
                           QDir::Name)) {
        m_shader->addItem(fi.fileName(), fi.fileName());
    }
    f->addRow(QStringLiteral("Shader"), m_shader);
    auto* hint = new QLabel(
        QStringLiteral("Drop .frag files in %1 — see example_vignette.frag")
            .arg(shaderDir));
    hint->setWordWrap(true);
    f->addRow(QString(), hint);
    return w;
}

QWidget* ConfigWindow::makeAdvancedTab()
{
    auto* w = new QWidget;
    auto* f = new QFormLayout(w);

    m_sgdbEnabled = new QCheckBox(QStringLiteral("enabled (opt-in, needs key)"));
    f->addRow(QStringLiteral("SteamGridDB"), m_sgdbEnabled);

    m_sgdbKeyEnvVar = new QLineEdit;
    f->addRow(QStringLiteral("API key env var"), m_sgdbKeyEnvVar);
    f->addRow(QString(),
              new QLabel(QStringLiteral(
                  "The API key itself is never stored — only the env var name.")));

    m_hltbEnabled =
        new QCheckBox(QStringLiteral("enabled (uses howlongtobeat.com)"));
    f->addRow(QStringLiteral("HowLongToBeat"), m_hltbEnabled);

    m_logLevel = new QComboBox;
    m_logLevel->setEditable(true);
    m_logLevel->addItems({QStringLiteral("trace"), QStringLiteral("debug"),
                          QStringLiteral("info"), QStringLiteral("warn"),
                          QStringLiteral("error")});
    f->addRow(QStringLiteral("Log level"), m_logLevel);

    m_cacheDir = new QLineEdit;
    m_cacheDir->setPlaceholderText(QStringLiteral("default"));
    auto* browse = new QPushButton(QStringLiteral("Browse…"));
    connect(browse, &QPushButton::clicked, this, &ConfigWindow::browseCacheDir);
    f->addRow(QStringLiteral("Cache dir"), row(m_cacheDir, browse));

    m_debugUi = new QCheckBox(QStringLiteral("enabled"));
    f->addRow(QStringLiteral("Debug overlay"), m_debugUi);
    return w;
}

void ConfigWindow::loadToUi(const config::AppConfig& cfg)
{
    m_steamRoot->setText(cfg.content.steamRoot ? toQs(*cfg.content.steamRoot)
                                              : QString());
    const auto& kinds = cfg.content.includeKinds;
    m_kindHero->setChecked(kinds.contains("hero"));
    m_kindGrid->setChecked(kinds.contains("grid"));
    m_kindHeader->setChecked(kinds.contains("header"));
    m_kindLogo->setChecked(kinds.contains("logo"));
    m_kindIcon->setChecked(kinds.contains("icon"));
    m_installedOnly->setChecked(cfg.content.installedOnly);
    m_minImageWidth->setValue(static_cast<int>(cfg.content.minImageWidth));
    populateGameList(cfg);

    const int modeIdx =
        m_layoutMode->findData(QString::fromStdString(cfg.layout.mode));
    m_layoutMode->setCurrentIndex(modeIdx >= 0 ? modeIdx : 3); // mix
    m_cellCount->setValue(cfg.layout.cellCount);
    m_gutter->setValue(cfg.layout.gutter);
    m_seed->setText(cfg.layout.seed ? QString::number(*cfg.layout.seed) : QString());

    m_kenBurnsEnabled->setChecked(cfg.animation.kenBurns.enabled);
    m_kbDurationMs->setValue(cfg.animation.kenBurns.durationMs);
    m_kbZoomFrom->setValue(cfg.animation.kenBurns.zoomFrom);
    m_kbZoomTo->setValue(cfg.animation.kenBurns.zoomTo);
    m_collageEnabled->setChecked(cfg.animation.collage.enabled);
    m_collageFadeInMs->setValue(cfg.animation.collage.fadeInMs);
    m_collageDriftPx->setValue(cfg.animation.collage.driftPx);
    m_transitionMs->setValue(cfg.animation.transitionMs);
    m_stillMs->setValue(cfg.animation.stillMs);
    m_showBadge->setChecked(cfg.animation.showBadge);

    m_targetFps->setValue(cfg.rendering.targetFps);
    m_vsync->setChecked(cfg.rendering.vsync);
    m_blurBackground->setChecked(cfg.rendering.blurBackground);
    {
        const QString shader = toQs(cfg.rendering.shader);
        const int idx = m_shader->findData(shader);
        m_shader->setCurrentIndex(std::max(0, idx));
    }

    m_sgdbEnabled->setChecked(cfg.enrichment.steamGridDbEnabled);
    m_sgdbKeyEnvVar->setText(toQs(cfg.enrichment.apiKeyEnvVar));
    m_hltbEnabled->setChecked(cfg.enrichment.hltbEnabled);
    m_logLevel->setCurrentText(toQs(cfg.advanced.logLevel));
    m_cacheDir->setText(cfg.advanced.cacheDir ? toQs(*cfg.advanced.cacheDir)
                                             : QString());
    m_debugUi->setChecked(cfg.advanced.debugUi);
}

config::AppConfig ConfigWindow::collectFromUi() const
{
    config::AppConfig cfg;
    if (!m_steamRoot->text().trimmed().isEmpty()) {
        cfg.content.steamRoot = m_steamRoot->text().trimmed().toStdString();
    }
    cfg.content.includeKinds.clear();
    if (m_kindHero->isChecked())   cfg.content.includeKinds.insert("hero");
    if (m_kindGrid->isChecked())   cfg.content.includeKinds.insert("grid");
    if (m_kindHeader->isChecked()) cfg.content.includeKinds.insert("header");
    if (m_kindLogo->isChecked())   cfg.content.includeKinds.insert("logo");
    if (m_kindIcon->isChecked())   cfg.content.includeKinds.insert("icon");
    cfg.content.installedOnly = m_installedOnly->isChecked();
    cfg.content.excludeAppIds = m_extraExcludedIds;
    for (int i = 0; i < m_gameList->count(); ++i) {
        const QListWidgetItem* item = m_gameList->item(i);
        if (item->checkState() == Qt::Unchecked) {
            cfg.content.excludeAppIds.insert(
                item->data(Qt::UserRole).toString().toStdString());
        }
    }
    cfg.content.minImageWidth = static_cast<std::uint32_t>(m_minImageWidth->value());

    cfg.layout.mode =
        m_layoutMode->currentData().toString().toStdString();
    cfg.layout.cellCount = m_cellCount->value();
    cfg.layout.gutter = m_gutter->value();
    bool ok = false;
    const quint64 seed = m_seed->text().trimmed().toULongLong(&ok);
    if (ok) {
        cfg.layout.seed = seed;
    }

    cfg.animation.kenBurns.enabled = m_kenBurnsEnabled->isChecked();
    cfg.animation.kenBurns.durationMs = m_kbDurationMs->value();
    cfg.animation.kenBurns.zoomFrom = static_cast<float>(m_kbZoomFrom->value());
    cfg.animation.kenBurns.zoomTo = static_cast<float>(m_kbZoomTo->value());
    cfg.animation.collage.enabled = m_collageEnabled->isChecked();
    cfg.animation.collage.fadeInMs = m_collageFadeInMs->value();
    cfg.animation.collage.driftPx = m_collageDriftPx->value();
    cfg.animation.transitionMs = m_transitionMs->value();
    cfg.animation.stillMs = m_stillMs->value();
    cfg.animation.showBadge = m_showBadge->isChecked();

    cfg.rendering.targetFps = m_targetFps->value();
    cfg.rendering.vsync = m_vsync->isChecked();
    cfg.rendering.blurBackground = m_blurBackground->isChecked();
    cfg.rendering.shader = m_shader->currentData().toString().toStdString();

    cfg.enrichment.steamGridDbEnabled = m_sgdbEnabled->isChecked();
    cfg.enrichment.apiKeyEnvVar = m_sgdbKeyEnvVar->text().trimmed().toStdString();
    cfg.enrichment.hltbEnabled = m_hltbEnabled->isChecked();
    cfg.advanced.logLevel = m_logLevel->currentText().trimmed().toStdString();
    if (!m_cacheDir->text().trimmed().isEmpty()) {
        cfg.advanced.cacheDir = m_cacheDir->text().trimmed().toStdString();
    }
    cfg.advanced.debugUi = m_debugUi->isChecked();
    return cfg;
}

void ConfigWindow::reload()
{
    loadToUi(config::loadConfig(m_path));
    statusBar()->showMessage(
        QStringLiteral("Loaded %1").arg(toQs(m_path)), 5000);
}

void ConfigWindow::save()
{
    const auto result = config::saveConfig(m_path, collectFromUi());
    if (result) {
        statusBar()->showMessage(
            QStringLiteral("Saved %1").arg(toQs(m_path)), 5000);
    } else {
        QMessageBox::warning(this, QStringLiteral("Save failed"),
                             toQs(result.error().message));
    }
}

void ConfigWindow::saveAs()
{
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save config"), toQs(m_path),
        QStringLiteral("JSON files (*.json)"));
    if (file.isEmpty()) {
        return;
    }
    m_path = file.toStdString();
    save();
}

void ConfigWindow::browseSteamRoot()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Steam install root"), m_steamRoot->text());
    if (!dir.isEmpty()) {
        m_steamRoot->setText(dir);
    }
}

void ConfigWindow::browseCacheDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Cache directory"), m_cacheDir->text());
    if (!dir.isEmpty()) {
        m_cacheDir->setText(dir);
    }
}

void ConfigWindow::populateGameList(const config::AppConfig& cfg)
{
    content::DiscoveryContext ctx;
    ctx.steamRootOverride = cfg.content.steamRoot;
    ctx.installedOnly = cfg.content.installedOnly;
    for (const auto& kind : cfg.content.includeKinds) {
        if (auto k = content::artworkKindFromString(kind)) {
            ctx.includeKinds.insert(*k);
        }
    }
    // Deliberately not applying excludeAppIds: the list must contain
    // excluded games so their check state can reflect/change it.

    content::SteamLocalProvider provider;
    const auto artworks = provider.discover(ctx);

    std::map<std::string, std::string> names; // appId -> first seen name
    for (const auto& a : artworks) {
        auto& slot = names[a.appId];
        if (slot.empty()) {
            slot = a.gameName;
        }
    }

    std::vector<std::pair<QString, std::string>> rows;
    rows.reserve(names.size());
    for (const auto& [appId, name] : names) {
        const QString label =
            name.empty()
                ? toQs(appId)
                : QStringLiteral("%1  (%2)").arg(toQs(name), toQs(appId));
        rows.emplace_back(label, appId);
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) {
                  return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
              });

    const QSignalBlocker blocker(m_gameList);
    m_gameList->clear();
    m_extraExcludedIds = cfg.content.excludeAppIds;
    for (auto& [label, appId] : rows) {
        auto* item = new QListWidgetItem(label, m_gameList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(Qt::UserRole, toQs(appId));
        const bool excluded = cfg.content.excludeAppIds.contains(appId);
        item->setCheckState(excluded ? Qt::Unchecked : Qt::Checked);
        m_extraExcludedIds.erase(appId);
    }
    filterGames(m_gameSearch->text());
    updateGameCount();
}

void ConfigWindow::filterGames(const QString& text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_gameList->count(); ++i) {
        QListWidgetItem* item = m_gameList->item(i);
        const bool match = needle.isEmpty() ||
                           item->text().contains(needle,
                                                 Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void ConfigWindow::setVisibleGamesChecked(bool checked)
{
    const QSignalBlocker blocker(m_gameList);
    for (int i = 0; i < m_gameList->count(); ++i) {
        QListWidgetItem* item = m_gameList->item(i);
        if (!item->isHidden()) {
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
    }
    updateGameCount();
}

void ConfigWindow::updateGameCount()
{
    int excluded = 0;
    for (int i = 0; i < m_gameList->count(); ++i) {
        if (m_gameList->item(i)->checkState() == Qt::Unchecked) {
            ++excluded;
        }
    }
    m_gameCount->setText(QStringLiteral("%1 games • %2 excluded")
                             .arg(m_gameList->count())
                             .arg(excluded));
}

} // namespace slc::configurator
