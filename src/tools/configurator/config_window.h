#pragma once

// Qt settings UI for config.json (doc §5.7). Reads/writes through
// slc::config so file semantics (strict-tolerant parse, atomic-ish save)
// stay identical to what the hosts consume.

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QSpinBox>

#include "slc/config/app_config.h"

#include <unordered_set>

namespace slc::configurator {

class ConfigWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit ConfigWindow(QWidget* parent = nullptr);

  private slots:
    void reload();
    void save();
    void saveAs();
    void browseSteamRoot();
    void browseCacheDir();
    void filterGames(const QString& text);
    void setVisibleGamesChecked(bool checked);

  private:
    QWidget* makeContentTab();
    QWidget* makeGamesTab();
    QWidget* makeLayoutTab();
    QWidget* makeAnimationTab();
    QWidget* makeRenderingTab();
    QWidget* makeAdvancedTab();

    void loadToUi(const config::AppConfig& cfg);
    void populateGameList(const config::AppConfig& cfg);
    void updateGameCount();
    [[nodiscard]] config::AppConfig collectFromUi() const;

    std::filesystem::path m_path;

    // Content
    QLineEdit* m_steamRoot = nullptr;
    QCheckBox* m_kindHero = nullptr;
    QCheckBox* m_kindGrid = nullptr;
    QCheckBox* m_kindHeader = nullptr;
    QCheckBox* m_kindLogo = nullptr;
    QCheckBox* m_kindIcon = nullptr;
    QCheckBox* m_installedOnly = nullptr;
    QSpinBox* m_minImageWidth = nullptr;

    // Game filter — checked = included, unchecked = excluded. Excluded ids
    // with no discovered artwork entry are preserved in m_extraExcludedIds.
    QLineEdit* m_gameSearch = nullptr;
    QListWidget* m_gameList = nullptr;
    QLabel* m_gameCount = nullptr;
    std::unordered_set<std::string> m_extraExcludedIds;

    // Layout (multi-image strategy is scene-random; mode picks the mix)
    QComboBox* m_layoutMode = nullptr;
    QSpinBox* m_cellCount = nullptr;
    QSpinBox* m_gutter = nullptr;
    QLineEdit* m_seed = nullptr; // empty = random

    // Animation
    QCheckBox* m_kenBurnsEnabled = nullptr;
    QSpinBox* m_kbDurationMs = nullptr;
    QDoubleSpinBox* m_kbZoomFrom = nullptr;
    QDoubleSpinBox* m_kbZoomTo = nullptr;
    QCheckBox* m_collageEnabled = nullptr;
    QSpinBox* m_collageFadeInMs = nullptr;
    QSpinBox* m_collageDriftPx = nullptr;
    QSpinBox* m_transitionMs = nullptr;
    QSpinBox* m_stillMs = nullptr;
    QCheckBox* m_showBadge = nullptr;

    // Rendering
    QSpinBox* m_targetFps = nullptr;
    QCheckBox* m_vsync = nullptr;
    QCheckBox* m_blurBackground = nullptr;
    QComboBox* m_shader = nullptr;

    // Enrichment + Advanced
    QCheckBox* m_sgdbEnabled = nullptr;
    QLineEdit* m_sgdbKeyEnvVar = nullptr;
    QCheckBox* m_hltbEnabled = nullptr;
    QComboBox* m_logLevel = nullptr;
    QLineEdit* m_cacheDir = nullptr;
    QCheckBox* m_debugUi = nullptr;
};

} // namespace slc::configurator
