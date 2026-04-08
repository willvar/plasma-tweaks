#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QObject>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

// ─── Embedded CMakeLists.txt for kickoff standalone build ───────────

static const char *KICKOFF_CMAKE = R"(cmake_minimum_required(VERSION 3.22)
project(kickoff-custom)

# Must be set before find_package(Qt6) to avoid QML macro errors
set(QT_MAJOR_VERSION 6)
set(QT_DEFAULT_MAJOR_VERSION 6)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Quick DBus)
find_package(KF6 REQUIRED COMPONENTS Config Package CoreAddons WindowSystem)
find_package(Plasma REQUIRED)

add_subdirectory(kickoff)
add_subdirectory(showdesktop)
)";

// ─── Embedded CMakeLists.txt for systemtray standalone build ────────

static const char *SYSTRAY_CMAKE = R"(cmake_minimum_required(VERSION 3.22)
project(systemtray-custom)

# Must be set before find_package(Qt6) to avoid QML macro errors
set(QT_MAJOR_VERSION 6)
set(QT_DEFAULT_MAJOR_VERSION 6)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQtDeclareLoggingCategory)

find_package(Qt6 REQUIRED COMPONENTS Quick Core DBus Gui GuiPrivate Widgets)
find_package(KF6 REQUIRED COMPONENTS Config I18n IconThemes ItemModels WindowSystem XmlGui Package CoreAddons)
find_package(KF6StatusNotifierItem REQUIRED)
find_package(Plasma REQUIRED)
find_package(PlasmaQuick REQUIRED)

set(plasma-workspace_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(KSTATUSNOTIFIERITEM_DBUS_INTERFACES_DIR /usr/share/dbus-1/interfaces)

add_subdirectory(libdbusmenuqt)
add_subdirectory(systemtray)
)";

// ─── config-X11.h content ──────────────────────────────────────────

static const char *CONFIG_X11_H = R"(#define HAVE_XCURSOR 1
#define HAVE_XFIXES 1
#define HAVE_X11 1
)";

// ─── QML UI ────────────────────────────────────────────────────────

static const char *QML_UI = R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    title: "Plasma Tweaks"
    width: 440
    height: 520
    visible: true
    minimumWidth: 380
    minimumHeight: 400

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        GroupBox {
            title: "Kickoff Category Padding"
            Layout.fillWidth: true
            RowLayout {
                spacing: 8
                SpinBox {
                    id: paddingSpin
                    from: 0
                    to: 30
                    enabled: !backend.busy && backend.initialized
                    Component.onCompleted: value = backend.padding
                    onValueModified: backend.padding = value
                    Connections {
                        target: backend
                        function onPaddingChanged() { paddingSpin.value = backend.padding }
                    }
                }
                Label { text: "px" }
            }
        }

        GroupBox {
            title: "System Tray Icon Size"
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                RowLayout {
                    spacing: 12
                    Button {
                        text: "<"
                        implicitWidth: 36
                        implicitHeight: 36
                        enabled: !backend.busy && backend.initialized && backend.iconSizeIndex > 0
                        onClicked: backend.prevIconSize()
                    }
                    Label {
                        text: backend.iconSize + " px"
                        font.bold: true
                        font.pixelSize: 16
                        horizontalAlignment: Text.AlignHCenter
                        Layout.minimumWidth: 60
                    }
                    Button {
                        text: ">"
                        implicitWidth: 36
                        implicitHeight: 36
                        enabled: !backend.busy && backend.initialized && backend.iconSizeIndex < 4
                        onClicked: backend.nextIconSize()
                    }
                }
                Label {
                    text: "Tiers: 16, 22, 32, 48, 64"
                    font.pixelSize: 11
                    opacity: 0.6
                }
            }
        }

        Button {
            text: backend.needsInit ? "Initialize" : "Apply"
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            enabled: !backend.busy
            highlighted: true
            onClicked: {
                if (backend.needsInit)
                    backend.doInit()
                else
                    backend.apply()
            }
        }

        GroupBox {
            title: "Build Log"
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                anchors.fill: parent
                clip: true
                TextArea {
                    id: logArea
                    readOnly: true
                    text: backend.logText
                    font.family: "monospace"
                    font.pixelSize: 12
                    wrapMode: TextEdit.Wrap
                    onTextChanged: cursorPosition = length
                }
            }
        }
    }

    Component.onCompleted: backend.checkInit()
}
)";

// ─── TweaksBackend ─────────────────────────────────────────────────

class TweaksBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int padding READ padding WRITE setPadding NOTIFY paddingChanged)
    Q_PROPERTY(int iconSizeIndex READ iconSizeIndex WRITE setIconSizeIndex NOTIFY iconSizeChanged)
    Q_PROPERTY(int iconSize READ iconSize NOTIFY iconSizeChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool needsInit READ needsInit NOTIFY needsInitChanged)
    Q_PROPERTY(QString initReason READ initReason NOTIFY needsInitChanged)

public:
    explicit TweaksBackend(QObject *parent = nullptr)
        : QObject(parent)
        , m_dataDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                    + QStringLiteral("/plasma-tweaks"))
    {
        QDir().mkpath(m_dataDir);

        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
            QString out = QString::fromUtf8(m_proc->readAllStandardOutput()).trimmed();
            if (!out.isEmpty()) appendLog(out);
        });
        connect(m_proc, &QProcess::finished, this, &TweaksBackend::onProcessFinished);
    }

    // ── Properties ──────────────────────────────────────────────────

    int padding() const { return m_padding; }
    void setPadding(int v) {
        if (m_padding != v) { m_padding = v; emit paddingChanged(); }
    }

    int iconSizeIndex() const { return m_iconSizeIdx; }
    void setIconSizeIndex(int v) {
        v = qBound(0, v, m_iconSizes.size() - 1);
        if (m_iconSizeIdx != v) { m_iconSizeIdx = v; emit iconSizeChanged(); }
    }
    int iconSize() const { return m_iconSizes.value(m_iconSizeIdx, 32); }

    QString logText() const { return m_log; }
    bool busy() const { return m_busy; }
    bool initialized() const { return m_initialized; }
    bool needsInit() const { return m_needsInit; }
    QString initReason() const { return m_initReason; }
    // ── Slots ───────────────────────────────────────────────────────

    Q_INVOKABLE void prevIconSize() { setIconSizeIndex(m_iconSizeIdx - 1); }
    Q_INVOKABLE void nextIconSize() { setIconSizeIndex(m_iconSizeIdx + 1); }

    Q_INVOKABLE void checkInit() {
        m_log.clear();
        emit logChanged();

        // Detect Qt6 plugin path via qmake6 (part of qt6-base)
        QProcess pq;
        pq.start(QStringLiteral("qmake6"), {QStringLiteral("-query"), QStringLiteral("QT_INSTALL_PLUGINS")});
        pq.waitForFinished(5000);
        QString pluginDir = QString::fromUtf8(pq.readAllStandardOutput()).trimmed();
        if (pluginDir.isEmpty())
            pluginDir = QStringLiteral("/usr/lib/qt6/plugins");
        m_appletsDir = pluginDir + QStringLiteral("/plasma/applets");
        appendLog(QStringLiteral("Applets dir: ") + m_appletsDir);

        // Detect plasma version
        QProcess p;
        p.start(QStringLiteral("pacman"), {QStringLiteral("-Q"), QStringLiteral("plasma-desktop")});
        p.waitForFinished(5000);
        QString output = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        QRegularExpression re(QStringLiteral(R"(plasma-desktop\s+(\d+\.\d+\.\d+))"));
        auto match = re.match(output);
        if (!match.hasMatch()) {
            appendLog(QStringLiteral("ERROR: Could not detect Plasma version"));
            appendLog(QStringLiteral("pacman output: ") + output);
            return;
        }
        m_plasmaVersion = match.captured(1);
        appendLog(QStringLiteral("Plasma version: ") + m_plasmaVersion);

        // Check saved version
        QString savedVersion;
        QFile vf(m_dataDir + QStringLiteral("/version"));
        if (vf.open(QIODevice::ReadOnly)) {
            savedVersion = QString::fromUtf8(vf.readAll()).trimmed();
            vf.close();
        }

        bool srcOk = QDir(m_dataDir + QStringLiteral("/src/plasma-desktop/applets/kickoff")).exists()
                   && QDir(m_dataDir + QStringLiteral("/src/plasma-desktop/applets/showdesktop")).exists()
                   && QDir(m_dataDir + QStringLiteral("/src/plasma-workspace/applets/systemtray")).exists();
        bool buildsOk = QFile::exists(m_dataDir + QStringLiteral("/kickoff-build/build/build.ninja"))
                     && QFile::exists(m_dataDir + QStringLiteral("/systray-build/build/build.ninja"));

        if (!srcOk || !buildsOk) {
            setNeedsInit(QStringLiteral("First run - initialization required"));
            return;
        }
        if (savedVersion != m_plasmaVersion) {
            setNeedsInit(QString("Version changed: %1 -> %2, re-initialization required")
                             .arg(savedVersion, m_plasmaVersion));
            return;
        }

        m_needsInit = false;
        m_initialized = true;
        emit needsInitChanged();
        emit initializedChanged();
        appendLog(QStringLiteral("Ready (version ") + m_plasmaVersion + QStringLiteral(")"));
        readCurrentValues();
    }

    Q_INVOKABLE void doInit() {
        if (m_busy) return;

        m_steps.clear();
        const QString srcDir       = m_dataDir + QStringLiteral("/src");
        const QString kickClone    = srcDir + QStringLiteral("/plasma-desktop");
        const QString wsClone      = srcDir + QStringLiteral("/plasma-workspace");
        const QString kickBuild    = m_dataDir + QStringLiteral("/kickoff-build");
        const QString systrayBuild = m_dataDir + QStringLiteral("/systray-build");

        // 1. Clean old build dirs + source
        addStep("Cleaning old data...",
                QString("rm -rf %1 %2 %3 %4").arg(kickClone, wsClone, kickBuild, systrayBuild),
                m_dataDir);

        // 2. Create src dir
        addStep("Creating directories...",
                QStringLiteral("mkdir -p ") + srcDir, m_dataDir);

        // 3. Clone plasma-desktop (sparse)
        addStep("Cloning plasma-desktop (kickoff)...",
                QString("git clone --depth 1 --filter=blob:none --sparse "
                        "https://invent.kde.org/plasma/plasma-desktop.git "
                        "--branch v%1 %2 && cd %2 && git sparse-checkout set applets/kickoff applets/showdesktop")
                    .arg(m_plasmaVersion, kickClone),
                srcDir);

        // 4. Clone plasma-workspace (sparse)
        addStep("Cloning plasma-workspace (systemtray)...",
                QString("git clone --depth 1 --filter=blob:none --sparse "
                        "https://invent.kde.org/plasma/plasma-workspace.git "
                        "--branch v%1 %2 && cd %2 && "
                        "git sparse-checkout set applets/systemtray libdbusmenuqt "
                        "statusnotifierwatcher libkmpris")
                    .arg(m_plasmaVersion, wsClone),
                srcDir);

        // 5. Setup kickoff build dir
        addStepAction("Setting up kickoff build...", [=, this]() {
            return setupKickoffBuild(kickBuild, kickClone);
        });

        // 6. Setup systray build dir
        addStepAction("Setting up systray build...", [=, this]() {
            return setupSystrayBuild(systrayBuild, wsClone);
        });

        // 7. cmake configure kickoff
        addStep("Configuring kickoff (cmake)...",
                QStringLiteral("cmake .. -GNinja -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(pwd)/out -Wno-dev"),
                kickBuild + QStringLiteral("/build"));

        // 8. cmake configure systray
        addStep("Configuring systemtray (cmake)...",
                QStringLiteral("cmake .. -GNinja -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(pwd)/out "
                               "-DBUILD_TESTING=OFF -Wno-dev"),
                systrayBuild + QStringLiteral("/build"));

        // 9. Save version
        addStepAction("Saving version...", [this]() {
            QFile vf(m_dataDir + QStringLiteral("/version"));
            if (!vf.open(QIODevice::WriteOnly)) return false;
            vf.write(m_plasmaVersion.toUtf8());
            vf.close();

            m_needsInit = false;
            m_initialized = true;
            emit needsInitChanged();
            emit initializedChanged();
            readCurrentValues();
            return true;
        });

        runSteps();
    }

    Q_INVOKABLE void apply() {
        if (m_busy || !m_initialized) return;

        m_steps.clear();
        const QString kickBuild    = m_dataDir + QStringLiteral("/kickoff-build");
        const QString systrayBuild = m_dataDir + QStringLiteral("/systray-build");

        // 1. Patch QML
        addStepAction("Patching QML files...", [this]() {
            return patchKickoffQml() && patchSystrayQml()
                && patchShowdesktopQml();
        });

        // 2. Build kickoff
        addStep("Building kickoff...",
                QStringLiteral("ninja"), kickBuild + QStringLiteral("/build"));

        // 3. Build systray
        addStep("Building systemtray...",
                QStringLiteral("ninja"), systrayBuild + QStringLiteral("/build"));

        // 4. Write install script
        addStepAction("Preparing install...", [=, this]() {
            return writeInstallScript(kickBuild, systrayBuild);
        });

        // 5. Stop plasmashell before replacing .so
        addStep("Stopping plasmashell...",
                QStringLiteral("kquitapp6 plasmashell"),
                QString());

        // 6. Install via pkexec
        addStep("Installing (pkexec)...",
                QStringLiteral("pkexec bash ") + m_dataDir + QStringLiteral("/install.sh"),
                m_dataDir);

        // 7. Start plasmashell
        addStep("Starting plasmashell...",
                QStringLiteral("systemctl --user start plasma-plasmashell"),
                QString());

        // 8. Save settings (records what's now installed)
        addStepAction("Saving settings...", [this]() {
            saveSettings();
            return true;
        });

        runSteps();
    }

signals:
    void paddingChanged();
    void iconSizeChanged();
    void logChanged();
    void busyChanged();
    void initializedChanged();
    void needsInitChanged();

private:
    // ── Step queue ──────────────────────────────────────────────────

    struct Step {
        QString label;
        QString command;
        QString workDir;
        std::function<bool()> action;
    };

    void addStep(const QString &label, const QString &cmd, const QString &workDir) {
        m_steps.append({label, cmd, workDir, nullptr});
    }
    void addStepAction(const QString &label, std::function<bool()> action) {
        m_steps.append({label, QString(), QString(), std::move(action)});
    }

    void runSteps() {
        m_busy = true;
        emit busyChanged();
        m_stepIdx = -1;
        runNextStep();
    }

    void runNextStep() {
        m_stepIdx++;
        if (m_stepIdx >= m_steps.size()) {
            appendLog(QStringLiteral("> Done!"));
            m_busy = false;
            emit busyChanged();
            return;
        }

        const auto &step = m_steps[m_stepIdx];
        appendLog(QStringLiteral("> ") + step.label);

        if (step.action) {
            if (!step.action()) {
                appendLog(QStringLiteral("ERROR: Step failed"));
                m_busy = false;
                emit busyChanged();
                return;
            }
        }

        if (step.command.isEmpty()) {
            QTimer::singleShot(0, this, &TweaksBackend::runNextStep);
            return;
        }

        if (!step.workDir.isEmpty())
            m_proc->setWorkingDirectory(step.workDir);
        m_proc->start(QStringLiteral("/bin/bash"), {QStringLiteral("-c"), step.command});
    }

    void onProcessFinished(int exitCode, QProcess::ExitStatus status) {
        QString remaining = QString::fromUtf8(m_proc->readAllStandardOutput()).trimmed();
        if (!remaining.isEmpty()) appendLog(remaining);

        if (exitCode != 0 || status != QProcess::NormalExit) {
            appendLog(QString("ERROR: Command failed (exit code %1)").arg(exitCode));
            m_busy = false;
            emit busyChanged();
            return;
        }
        runNextStep();
    }

    // ── Helpers ─────────────────────────────────────────────────────

    void appendLog(const QString &msg) {
        if (!m_log.isEmpty()) m_log += QLatin1Char('\n');
        m_log += msg;
        emit logChanged();
    }

    void setNeedsInit(const QString &reason) {
        m_needsInit = true;
        m_initialized = false;
        m_initReason = reason;
        emit needsInitChanged();
        emit initializedChanged();
        appendLog(reason);
    }

    // ── Build directory setup ───────────────────────────────────────

    bool setupKickoffBuild(const QString &buildDir, const QString &cloneDir) {
        QDir().mkpath(buildDir + QStringLiteral("/build"));

        // Write wrapper CMakeLists.txt
        QFile cmake(buildDir + QStringLiteral("/CMakeLists.txt"));
        if (!cmake.open(QIODevice::WriteOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot write kickoff CMakeLists.txt"));
            return false;
        }
        cmake.write(KICKOFF_CMAKE);
        cmake.close();

        // Symlink applet sources
        QFile::link(cloneDir + QStringLiteral("/applets/kickoff"),
                    buildDir + QStringLiteral("/kickoff"));
        QFile::link(cloneDir + QStringLiteral("/applets/showdesktop"),
                    buildDir + QStringLiteral("/showdesktop"));

        appendLog(QStringLiteral("  Created kickoff-build/"));
        return true;
    }

    bool setupSystrayBuild(const QString &buildDir, const QString &cloneDir) {
        QDir().mkpath(buildDir + QStringLiteral("/build"));

        // Write wrapper CMakeLists.txt
        QFile cmake(buildDir + QStringLiteral("/CMakeLists.txt"));
        if (!cmake.open(QIODevice::WriteOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot write systray CMakeLists.txt"));
            return false;
        }
        cmake.write(SYSTRAY_CMAKE);
        cmake.close();

        // Symlinks to workspace subdirectories
        auto link = [&](const QString &name, const QString &target) {
            QFile::link(target, buildDir + QLatin1Char('/') + name);
        };
        link(QStringLiteral("systemtray"),            cloneDir + QStringLiteral("/applets/systemtray"));
        link(QStringLiteral("libdbusmenuqt"),          cloneDir + QStringLiteral("/libdbusmenuqt"));
        link(QStringLiteral("libkmpris"),              cloneDir + QStringLiteral("/libkmpris"));
        link(QStringLiteral("statusnotifierwatcher"),  cloneDir + QStringLiteral("/statusnotifierwatcher"));

        // Write config-X11.h into build dir
        QFile cfg(buildDir + QStringLiteral("/build/config-X11.h"));
        if (!cfg.open(QIODevice::WriteOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot write config-X11.h"));
            return false;
        }
        cfg.write(CONFIG_X11_H);
        cfg.close();

        // Patch systemtray CMakeLists.txt: add include_directories for config-X11.h
        QString systraycmake = cloneDir + QStringLiteral("/applets/systemtray/CMakeLists.txt");
        QFile sf(systraycmake);
        if (!sf.open(QIODevice::ReadOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot read systemtray CMakeLists.txt"));
            return false;
        }
        QString content = QString::fromUtf8(sf.readAll());
        sf.close();

        const QString marker = QStringLiteral("include_directories(${CMAKE_BINARY_DIR})");
        if (!content.contains(marker)) {
            // The file has literal backslash-escaped quotes: \"
            const QString searchStr = QStringLiteral(
                R"(add_definitions(-DTRANSLATION_DOMAIN=\"plasma_applet_org.kde.plasma.systemtray\"))");
            if (!content.contains(searchStr)) {
                appendLog(QStringLiteral("  ERROR: Cannot find add_definitions line in systemtray CMakeLists.txt"));
                return false;
            }
            content.replace(searchStr, searchStr + QStringLiteral("\n") + marker);
            if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                appendLog(QStringLiteral("  ERROR: Cannot write systemtray CMakeLists.txt"));
                return false;
            }
            sf.write(content.toUtf8());
            sf.close();
        }

        appendLog(QStringLiteral("  Created systray-build/"));
        return true;
    }

    // ── QML patching ────────────────────────────────────────────────

    bool patchKickoffQml() {
        QString path = m_dataDir + QStringLiteral("/kickoff-build/kickoff/KickoffListDelegate.qml");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot open ") + path);
            return false;
        }
        QString content = QString::fromUtf8(f.readAll());
        f.close();

        // Two cases: already patched (has "isCategoryListItem ? <N> :") or stock
        QRegularExpression patchedRe(QStringLiteral(R"(isCategoryListItem \? \d+ :)"));
        if (patchedRe.match(content).hasMatch()) {
            // Already patched - just update the number
            content.replace(patchedRe, QString("isCategoryListItem ? %1 :").arg(m_padding));
        } else {
            // Stock format: "compact && !isCategoryListItem ? Kirigami.Units.mediumSpacing : Kirigami.Units.smallSpacing"
            // Transform to: "!compact && isCategoryListItem ? <N> : (compact && !isCategoryListItem ? Kirigami.Units.mediumSpacing : Kirigami.Units.smallSpacing)"
            const QString stockExpr = QStringLiteral(
                "compact && !isCategoryListItem ? Kirigami.Units.mediumSpacing : Kirigami.Units.smallSpacing");
            const QString patchedExpr = QString(
                "!compact && isCategoryListItem ? %1 : ("
                "compact && !isCategoryListItem ? Kirigami.Units.mediumSpacing : Kirigami.Units.smallSpacing)")
                .arg(m_padding);
            if (!content.contains(stockExpr)) {
                appendLog(QStringLiteral("  ERROR: Cannot find padding pattern in KickoffListDelegate.qml"));
                return false;
            }
            content.replace(stockExpr, patchedExpr);
        }

        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + path);
            return false;
        }
        f.write(content.toUtf8());
        f.close();
        appendLog(QString("  Kickoff padding: %1 px").arg(m_padding));
        return true;
    }

    bool patchSystrayQml() {
        QString path = m_dataDir + QStringLiteral("/systray-build/systemtray/qml/main.qml");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot open ") + path);
            return false;
        }
        QString content = QString::fromUtf8(f.readAll());
        f.close();

        // Find "if (autoSize) {" then replace the entire return statement on the next line
        QRegularExpression findBlock(QStringLiteral(R"(if \(autoSize\) \{)"));
        auto blockMatch = findBlock.match(content);
        if (!blockMatch.hasMatch()) {
            appendLog(QStringLiteral("  ERROR: Cannot find autoSize block in main.qml"));
            return false;
        }

        // Match "return <anything-to-end-of-line>" after the autoSize block
        QRegularExpression findReturn(QStringLiteral(R"(return [^\n]+)"));
        auto retMatch = findReturn.match(content, blockMatch.capturedEnd());
        if (!retMatch.hasMatch()) {
            appendLog(QStringLiteral("  ERROR: Cannot find return in autoSize block"));
            return false;
        }

        content.replace(retMatch.capturedStart(), retMatch.capturedLength(),
                        QString("return %1").arg(iconSize()));

        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + path);
            return false;
        }
        f.write(content.toUtf8());
        f.close();
        appendLog(QString("  Systray icon size: %1 px").arg(iconSize()));
        return true;
    }

    bool patchShowdesktopQml() {
        QString path = m_dataDir + QStringLiteral("/kickoff-build/showdesktop/main.qml");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot open ") + path);
            return false;
        }
        QString content = QString::fromUtf8(f.readAll());
        f.close();

        // Replace "Layout.maximumWidth: Layout.minimumWidth" with fixed size
        // and "Layout.minimumWidth: Kirigami.Units.iconSizes.medium" likewise
        QRegularExpression minW(QStringLiteral(
            R"(Layout\.minimumWidth: Kirigami\.Units\.iconSizes\.medium)"));
        QRegularExpression maxW(QStringLiteral(
            R"(Layout\.maximumWidth: Layout\.minimumWidth)"));
        QRegularExpression minH(QStringLiteral(
            R"(Layout\.minimumHeight: Kirigami\.Units\.iconSizes\.medium)"));
        QRegularExpression maxH(QStringLiteral(
            R"(Layout\.maximumHeight: Layout\.minimumHeight)"));

        // Also handle already-patched (numeric values)
        QRegularExpression minWPatched(QStringLiteral(R"(Layout\.minimumWidth: \d+)"));
        QRegularExpression maxWPatched(QStringLiteral(R"(Layout\.maximumWidth: \d+)"));
        QRegularExpression minHPatched(QStringLiteral(R"(Layout\.minimumHeight: \d+)"));
        QRegularExpression maxHPatched(QStringLiteral(R"(Layout\.maximumHeight: \d+)"));

        QString sizeStr = QString::number(iconSize());

        if (minW.match(content).hasMatch()) {
            content.replace(minW, QStringLiteral("Layout.minimumWidth: ") + sizeStr);
            content.replace(maxW, QStringLiteral("Layout.maximumWidth: ") + sizeStr);
            content.replace(minH, QStringLiteral("Layout.minimumHeight: ") + sizeStr);
            content.replace(maxH, QStringLiteral("Layout.maximumHeight: ") + sizeStr);
        } else if (minWPatched.match(content).hasMatch()) {
            content.replace(minWPatched, QStringLiteral("Layout.minimumWidth: ") + sizeStr);
            content.replace(maxWPatched, QStringLiteral("Layout.maximumWidth: ") + sizeStr);
            content.replace(minHPatched, QStringLiteral("Layout.minimumHeight: ") + sizeStr);
            content.replace(maxHPatched, QStringLiteral("Layout.maximumHeight: ") + sizeStr);
        } else {
            appendLog(QStringLiteral("  ERROR: Cannot find Layout size pattern in showdesktop main.qml"));
            return false;
        }

        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + path);
            return false;
        }
        f.write(content.toUtf8());
        f.close();
        appendLog(QString("  Showdesktop icon size: %1 px").arg(iconSize()));
        return true;
    }

    // ── Install script ──────────────────────────────────────────────

    bool writeInstallScript(const QString &kickBuild, const QString &systrayBuild) {
        QString kickoffSo = kickBuild + QStringLiteral("/build/out/plasma/applets/org.kde.plasma.kickoff.so");
        QString showdesktopSo = kickBuild + QStringLiteral("/build/out/plasma/applets/org.kde.plasma.showdesktop.so");
        QString systraySo = systrayBuild + QStringLiteral("/build/out/plasma/applets/org.kde.plasma.systemtray.so");

        for (const auto &[name, path] : {std::pair{QStringLiteral("kickoff"), kickoffSo},
                                          {QStringLiteral("showdesktop"), showdesktopSo},
                                          {QStringLiteral("systemtray"), systraySo}}) {
            if (!QFile::exists(path)) {
                appendLog(QStringLiteral("  ERROR: %1 .so not found: %2").arg(name, path));
                return false;
            }
        }

        QString script = QString(R"(#!/bin/bash
set -e
APPLETS_DIR="%1"

# Backup originals (only if .bak doesn't exist yet)
for SO in org.kde.plasma.kickoff.so org.kde.plasma.showdesktop.so org.kde.plasma.systemtray.so; do
    [ ! -f "$APPLETS_DIR/${SO}.bak" ] && cp "$APPLETS_DIR/$SO" "$APPLETS_DIR/${SO}.bak"
done

# Copy new .so files
cp "%2" "$APPLETS_DIR/org.kde.plasma.kickoff.so"
cp "%3" "$APPLETS_DIR/org.kde.plasma.showdesktop.so"
cp "%4" "$APPLETS_DIR/org.kde.plasma.systemtray.so"

echo "Installation complete"
)").arg(m_appletsDir, kickoffSo, showdesktopSo, systraySo);

        QFile f(m_dataDir + QStringLiteral("/install.sh"));
        if (!f.open(QIODevice::WriteOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot write install.sh"));
            return false;
        }
        f.write(script.toUtf8());
        f.close();
        return true;
    }

    // ── Settings persistence ────────────────────────────────────────

    void saveSettings() {
        QFile f(m_dataDir + QStringLiteral("/settings"));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QString("%1 %2\n").arg(m_padding).arg(iconSize()).toUtf8());
            f.close();
        }
    }

    bool loadSettings() {
        QFile f(m_dataDir + QStringLiteral("/settings"));
        if (!f.open(QIODevice::ReadOnly)) return false;
        QString line = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        QStringList parts = line.split(QLatin1Char(' '));
        if (parts.size() < 2) return false;

        m_padding = parts[0].toInt();
        emit paddingChanged();
        setIconSizeFromValue(parts[1].toInt());
        return true;
    }

    void setIconSizeFromValue(int size) {
        int idx = m_iconSizes.indexOf(size);
        if (idx < 0) {
            // Find nearest preset
            int bestDiff = INT_MAX;
            for (int i = 0; i < m_iconSizes.size(); i++) {
                int diff = qAbs(m_iconSizes[i] - size);
                if (diff < bestDiff) { bestDiff = diff; idx = i; }
            }
        }
        m_iconSizeIdx = idx;
        emit iconSizeChanged();
    }

    // ── Read current values from QML files ──────────────────────────

    void readCurrentValues() {
        // Try settings file first (= what's currently installed)
        if (loadSettings()) {
            appendLog(QString("  Installed: padding=%1, icon size=%2")
                          .arg(m_padding).arg(iconSize()));
            return;
        }

        // Fall back to reading from QML source (stock or last-patched)
        bool paddingPatched = false;
        bool iconPatched = false;

        QFile f1(m_dataDir + QStringLiteral("/kickoff-build/kickoff/KickoffListDelegate.qml"));
        if (f1.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(f1.readAll());
            f1.close();
            QRegularExpression re(QStringLiteral(R"(isCategoryListItem \? (\d+) :)"));
            auto match = re.match(content);
            if (match.hasMatch()) {
                m_padding = match.captured(1).toInt();
                emit paddingChanged();
                paddingPatched = true;
            }
        }

        QFile f2(m_dataDir + QStringLiteral("/systray-build/systemtray/qml/main.qml"));
        if (f2.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(f2.readAll());
            f2.close();
            QRegularExpression findBlock(QStringLiteral(R"(if \(autoSize\) \{)"));
            auto blockMatch = findBlock.match(content);
            if (blockMatch.hasMatch()) {
                // Check if patched (simple "return <N>") vs stock (return Kirigami.Units...)
                QRegularExpression findReturn(QStringLiteral(R"(return (\d+)\s*\n)"));
                auto retMatch = findReturn.match(content, blockMatch.capturedEnd());
                if (retMatch.hasMatch()) {
                    setIconSizeFromValue(retMatch.captured(1).toInt());
                    iconPatched = true;
                }
            }
        }

        appendLog(QString("  Kickoff padding: %1, Systray icon size: %2")
                      .arg(paddingPatched ? QString("%1 px").arg(m_padding) : QStringLiteral("stock"),
                           iconPatched ? QString("%1 px").arg(iconSize()) : QStringLiteral("stock")));
    }

    // ── Data members ────────────────────────────────────────────────

    int m_padding = 8; // stock KDE value
    int m_iconSizeIdx = 2; // index 2 = 32px (stock is 40, nearest preset)
    const QList<int> m_iconSizes{16, 22, 32, 48, 64};
    QString m_log;
    bool m_busy = false;
    bool m_initialized = false;
    bool m_needsInit = false;
    QString m_initReason;
    QString m_dataDir;
    QString m_plasmaVersion;
    QString m_appletsDir;

    QVector<Step> m_steps;
    int m_stepIdx = -1;
    QProcess *m_proc;
};

// ─── main ──────────────────────────────────────────────────────────

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Plasma Tweaks"));

    TweaksBackend backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.loadData(QByteArray(QML_UI));

    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}

#include "plasma-tweaks.moc"
