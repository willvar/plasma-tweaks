#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDomDocument>
#include <QObject>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <functional>

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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Core DBus Gui GuiPrivate Widgets)
find_package(KF6 REQUIRED COMPONENTS Config I18n IconThemes ItemModels WindowSystem XmlGui Package CoreAddons KIO JobWidgets Service)
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
    width: 760
    height: 760
    visible: true
    minimumWidth: 640
    minimumHeight: 560

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "Applet Tweaks" }
            TabButton { text: "Font Strategy" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScrollView {
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        width: parent.width
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
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScrollView {
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        width: parent.width
                        spacing: 12

                        Frame {
                            Layout.fillWidth: true

                            ColumnLayout {
                                width: parent.width
                                spacing: 6

                                Label {
                                    text: "Fonts & IDE"
                                    font.bold: true
                                    font.pixelSize: 20
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Start here: check the current setup, choose a quick action, then apply to the targets you want."
                                    wrapMode: Text.WordWrap
                                    opacity: 0.75
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Default profile: IBM Plex Sans + HarmonyOS Sans SC fallback + Maple Mono CN"
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 12
                                    opacity: 0.65
                                }
                            }
                        }

                        GroupBox {
                            title: "Current Setup"
                            Layout.fillWidth: true

                            ColumnLayout {
                                width: parent.width
                                spacing: 8

                                Label {
                                    Layout.fillWidth: true
                                    text: backend.fontState
                                    wrapMode: Text.WordWrap
                                }

                                Button {
                                    text: "Read Current System"
                                    enabled: !backend.busy
                                    onClicked: backend.refreshFontState()
                                }
                            }
                        }

                        GroupBox {
                            title: "Quick Actions"
                            Layout.fillWidth: true

                            ColumnLayout {
                                width: parent.width
                                spacing: 8

                                Label {
                                    Layout.fillWidth: true
                                    text: "Pick the path that matches what you want. You can still fine-tune everything below."
                                    wrapMode: Text.WordWrap
                                    opacity: 0.75
                                }

                                GridLayout {
                                    width: parent.width
                                    columns: 2
                                    columnSpacing: 12
                                    rowSpacing: 8

                                    Button {
                                        Layout.fillWidth: true
                                        text: "Use Default Profile"
                                        enabled: !backend.busy
                                        onClicked: {
                                            backend.uiFontFamily = "IBM Plex Sans"
                                            backend.fallbackFontFamily = "HarmonyOS Sans SC"
                                            backend.monoFontFamily = "Maple Mono CN"
                                            backend.desktopUiSize = 14
                                            backend.desktopMonoSize = 15
                                            backend.ideUiSize = 17
                                            backend.ideCodeSize = 16
                                        }
                                    }

                                    Button {
                                        Layout.fillWidth: true
                                        text: "Select Everything"
                                        enabled: !backend.busy
                                        onClicked: {
                                            backend.applyKde = true
                                            backend.applyGtk = true
                                            backend.applySddm = true
                                            backend.applyJetBrains = true
                                            backend.applyAndroidStudio = true
                                        }
                                    }

                                    Button {
                                        Layout.fillWidth: true
                                        text: "Desktop Only"
                                        enabled: !backend.busy
                                        onClicked: {
                                            backend.applyKde = true
                                            backend.applyGtk = true
                                            backend.applySddm = true
                                            backend.applyJetBrains = false
                                            backend.applyAndroidStudio = false
                                        }
                                    }

                                    Button {
                                        Layout.fillWidth: true
                                        text: "IDEs Only"
                                        enabled: !backend.busy
                                        onClicked: {
                                            backend.applyKde = false
                                            backend.applyGtk = false
                                            backend.applySddm = false
                                            backend.applyJetBrains = true
                                            backend.applyAndroidStudio = true
                                        }
                                    }
                                }
                            }
                        }

                        GroupBox {
                            title: "Apply Scope"
                            Layout.fillWidth: true

                            ColumnLayout {
                                width: parent.width
                                spacing: 8

                                Label {
                                    Layout.fillWidth: true
                                    text: "These are the places that will be updated when you apply this profile."
                                    wrapMode: Text.WordWrap
                                    opacity: 0.75
                                }

                                GridLayout {
                                    width: parent.width
                                    columns: 2
                                    columnSpacing: 12
                                    rowSpacing: 8

                                    CheckBox {
                                        text: "KDE + user fontconfig"
                                        checked: backend.applyKde
                                        enabled: !backend.busy
                                        onToggled: backend.applyKde = checked
                                    }
                                    CheckBox {
                                        text: "GTK + xsettingsd"
                                        checked: backend.applyGtk
                                        enabled: !backend.busy
                                        onToggled: backend.applyGtk = checked
                                    }
                                    CheckBox {
                                        text: "SDDM + system fontconfig"
                                        checked: backend.applySddm
                                        enabled: !backend.busy
                                        onToggled: backend.applySddm = checked
                                    }
                                    CheckBox {
                                        text: "JetBrains IDEs"
                                        checked: backend.applyJetBrains
                                        enabled: !backend.busy
                                        onToggled: backend.applyJetBrains = checked
                                    }
                                    CheckBox {
                                        text: "Android Studio"
                                        checked: backend.applyAndroidStudio
                                        enabled: !backend.busy
                                        onToggled: backend.applyAndroidStudio = checked
                                    }
                                }

                                Button {
                                    text: "Apply To Selected Targets"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 40
                                    enabled: !backend.busy
                                    highlighted: true
                                    onClicked: backend.applyFontStrategy()
                                }
                            }
                        }

                        GroupBox {
                            title: "Advanced Settings"
                            Layout.fillWidth: true

                            ColumnLayout {
                                width: parent.width
                                spacing: 12

                                Label {
                                    Layout.fillWidth: true
                                    text: "Only touch these when you want to override the default profile or tune sizes manually."
                                    wrapMode: Text.WordWrap
                                    opacity: 0.75
                                }

                                GroupBox {
                                    title: "Desktop Fonts"
                                    Layout.fillWidth: true

                                    ColumnLayout {
                                        width: parent.width

                                        GridLayout {
                                            width: parent.width
                                            columns: 2
                                            columnSpacing: 12
                                            rowSpacing: 8

                                            Label { text: "UI font" }
                                            TextField {
                                                Layout.fillWidth: true
                                                text: backend.uiFontFamily
                                                enabled: !backend.busy
                                                onEditingFinished: backend.uiFontFamily = text
                                            }

                                            Label { text: "CJK fallback" }
                                            TextField {
                                                Layout.fillWidth: true
                                                text: backend.fallbackFontFamily
                                                enabled: !backend.busy
                                                onEditingFinished: backend.fallbackFontFamily = text
                                            }

                                            Label { text: "Monospace" }
                                            TextField {
                                                Layout.fillWidth: true
                                                text: backend.monoFontFamily
                                                enabled: !backend.busy
                                                onEditingFinished: backend.monoFontFamily = text
                                            }

                                            Label { text: "Desktop UI size" }
                                            SpinBox {
                                                from: 8
                                                to: 24
                                                value: backend.desktopUiSize
                                                enabled: !backend.busy
                                                onValueModified: backend.desktopUiSize = value
                                            }

                                            Label { text: "Desktop mono size" }
                                            SpinBox {
                                                from: 8
                                                to: 24
                                                value: backend.desktopMonoSize
                                                enabled: !backend.busy
                                                onValueModified: backend.desktopMonoSize = value
                                            }
                                        }
                                    }
                                }

                                GroupBox {
                                    title: "IDE Defaults"
                                    Layout.fillWidth: true

                                    ColumnLayout {
                                        width: parent.width

                                        GridLayout {
                                            width: parent.width
                                            columns: 2
                                            columnSpacing: 12
                                            rowSpacing: 8

                                            Label { text: "IDE UI size" }
                                            SpinBox {
                                                from: 8
                                                to: 32
                                                value: backend.ideUiSize
                                                enabled: !backend.busy
                                                onValueModified: backend.ideUiSize = value
                                            }

                                            Label { text: "Editor / Terminal size" }
                                            SpinBox {
                                                from: 8
                                                to: 32
                                                value: backend.ideCodeSize
                                                enabled: !backend.busy
                                                onValueModified: backend.ideCodeSize = value
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        GroupBox {
            title: "Log"
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                TextArea {
                    id: logArea
                    width: parent.width
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
    Q_PROPERTY(QString uiFontFamily READ uiFontFamily WRITE setUiFontFamily NOTIFY fontSettingsChanged)
    Q_PROPERTY(QString fallbackFontFamily READ fallbackFontFamily WRITE setFallbackFontFamily NOTIFY fontSettingsChanged)
    Q_PROPERTY(QString monoFontFamily READ monoFontFamily WRITE setMonoFontFamily NOTIFY fontSettingsChanged)
    Q_PROPERTY(int desktopUiSize READ desktopUiSize WRITE setDesktopUiSize NOTIFY fontSettingsChanged)
    Q_PROPERTY(int desktopMonoSize READ desktopMonoSize WRITE setDesktopMonoSize NOTIFY fontSettingsChanged)
    Q_PROPERTY(int ideUiSize READ ideUiSize WRITE setIdeUiSize NOTIFY fontSettingsChanged)
    Q_PROPERTY(int ideCodeSize READ ideCodeSize WRITE setIdeCodeSize NOTIFY fontSettingsChanged)
    Q_PROPERTY(bool applyKde READ applyKde WRITE setApplyKde NOTIFY fontSettingsChanged)
    Q_PROPERTY(bool applyGtk READ applyGtk WRITE setApplyGtk NOTIFY fontSettingsChanged)
    Q_PROPERTY(bool applySddm READ applySddm WRITE setApplySddm NOTIFY fontSettingsChanged)
    Q_PROPERTY(bool applyJetBrains READ applyJetBrains WRITE setApplyJetBrains NOTIFY fontSettingsChanged)
    Q_PROPERTY(bool applyAndroidStudio READ applyAndroidStudio WRITE setApplyAndroidStudio NOTIFY fontSettingsChanged)
    Q_PROPERTY(QString fontState READ fontState NOTIFY fontStateChanged)

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

        refreshFontState();
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
    QString uiFontFamily() const { return m_uiFontFamily; }
    QString fallbackFontFamily() const { return m_fallbackFontFamily; }
    QString monoFontFamily() const { return m_monoFontFamily; }
    int desktopUiSize() const { return m_desktopUiSize; }
    int desktopMonoSize() const { return m_desktopMonoSize; }
    int ideUiSize() const { return m_ideUiSize; }
    int ideCodeSize() const { return m_ideCodeSize; }
    bool applyKde() const { return m_applyKde; }
    bool applyGtk() const { return m_applyGtk; }
    bool applySddm() const { return m_applySddm; }
    bool applyJetBrains() const { return m_applyJetBrains; }
    bool applyAndroidStudio() const { return m_applyAndroidStudio; }
    QString fontState() const { return m_fontState; }

    void setUiFontFamily(const QString &v) {
        if (m_uiFontFamily != v) { m_uiFontFamily = v; emit fontSettingsChanged(); }
    }
    void setFallbackFontFamily(const QString &v) {
        if (m_fallbackFontFamily != v) { m_fallbackFontFamily = v; emit fontSettingsChanged(); }
    }
    void setMonoFontFamily(const QString &v) {
        if (m_monoFontFamily != v) { m_monoFontFamily = v; emit fontSettingsChanged(); }
    }
    void setDesktopUiSize(int v) {
        v = qBound(8, v, 32);
        if (m_desktopUiSize != v) { m_desktopUiSize = v; emit fontSettingsChanged(); }
    }
    void setDesktopMonoSize(int v) {
        v = qBound(8, v, 32);
        if (m_desktopMonoSize != v) { m_desktopMonoSize = v; emit fontSettingsChanged(); }
    }
    void setIdeUiSize(int v) {
        v = qBound(8, v, 32);
        if (m_ideUiSize != v) { m_ideUiSize = v; emit fontSettingsChanged(); }
    }
    void setIdeCodeSize(int v) {
        v = qBound(8, v, 32);
        if (m_ideCodeSize != v) { m_ideCodeSize = v; emit fontSettingsChanged(); }
    }
    void setApplyKde(bool v) {
        if (m_applyKde != v) { m_applyKde = v; emit fontSettingsChanged(); }
    }
    void setApplyGtk(bool v) {
        if (m_applyGtk != v) { m_applyGtk = v; emit fontSettingsChanged(); }
    }
    void setApplySddm(bool v) {
        if (m_applySddm != v) { m_applySddm = v; emit fontSettingsChanged(); }
    }
    void setApplyJetBrains(bool v) {
        if (m_applyJetBrains != v) { m_applyJetBrains = v; emit fontSettingsChanged(); }
    }
    void setApplyAndroidStudio(bool v) {
        if (m_applyAndroidStudio != v) { m_applyAndroidStudio = v; emit fontSettingsChanged(); }
    }

    // ── Slots ───────────────────────────────────────────────────────

    Q_INVOKABLE void prevIconSize() { setIconSizeIndex(m_iconSizeIdx - 1); }
    Q_INVOKABLE void nextIconSize() { setIconSizeIndex(m_iconSizeIdx + 1); }
    Q_INVOKABLE void refreshFontState() { refreshFontStateImpl(); }
    Q_INVOKABLE void applyFontStrategy() { applyFontStrategyImpl(); }

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
        p.start(QStringLiteral("plasmashell"), {QStringLiteral("--version")});
        p.waitForFinished(5000);
        QString output = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        QRegularExpression re(QStringLiteral(R"(plasmashell\s+(\d+\.\d+\.\d+))"));
        auto match = re.match(output);
        if (!match.hasMatch()) {
            appendLog(QStringLiteral("ERROR: Could not detect Plasma version"));
            appendLog(QStringLiteral("plasmashell output: ") + output);
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
            return patchApplicationsPage() && patchKickoffQml() && patchSystrayQml()
                && patchShowdesktopQml() && patchDefaultCompactQml();
        });

        // 2. Update build config files (in case templates changed)
        addStepAction("Updating build config...", [=, this]() {
            return updateBuildConfig(kickBuild, systrayBuild);
        });

        // 3. Reconfigure
        addStep("Reconfiguring kickoff...",
                QStringLiteral("cmake .. -GNinja -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(pwd)/out -Wno-dev"),
                kickBuild + QStringLiteral("/build"));

        addStep("Reconfiguring systemtray...",
                QStringLiteral("cmake .. -GNinja -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(pwd)/out "
                               "-DBUILD_TESTING=OFF -Wno-dev"),
                systrayBuild + QStringLiteral("/build"));

        // 4. Build kickoff
        addStep("Building kickoff...",
                QStringLiteral("ninja"), kickBuild + QStringLiteral("/build"));

        // 5. Build systray
        addStep("Building systemtray...",
                QStringLiteral("ninja"), systrayBuild + QStringLiteral("/build"));

        // 6. Write install script
        addStepAction("Preparing install...", [=, this]() {
            return writeInstallScript(kickBuild, systrayBuild);
        });

        // 7. Stop plasmashell before replacing .so
        addStep("Stopping plasmashell...",
                QStringLiteral("kquitapp6 plasmashell"),
                QString());

        // 8. Install via pkexec
        addStep("Installing (pkexec)...",
                QStringLiteral("pkexec bash ") + m_dataDir + QStringLiteral("/install.sh"),
                m_dataDir);

        // 9. Start plasmashell
        addStep("Starting plasmashell...",
                QStringLiteral("systemctl --user start plasma-plasmashell"),
                QString());

        // 10. Save settings (records what's now installed)
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
    void fontSettingsChanged();
    void fontStateChanged();

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

    QString readFile(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        QString content = QString::fromUtf8(f.readAll());
        f.close();
        return content;
    }

    bool writeFile(const QString &path, const QString &content) {
        QFileInfo info(path);
        if (!info.dir().exists())
            QDir().mkpath(info.dir().absolutePath());

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(content.toUtf8());
        f.close();
        return true;
    }

    void setNeedsInit(const QString &reason) {
        m_needsInit = true;
        m_initialized = false;
        emit needsInitChanged();
        emit initializedChanged();
        appendLog(reason);
    }

    // ── Build directory setup ───────────────────────────────────────

    bool setupKickoffBuild(const QString &buildDir, const QString &cloneDir) {
        QDir().mkpath(buildDir + QStringLiteral("/build"));

        // Write wrapper CMakeLists.txt
        if (!writeFile(buildDir + QStringLiteral("/CMakeLists.txt"), QString::fromLatin1(KICKOFF_CMAKE))) {
            appendLog(QStringLiteral("  ERROR: Cannot write kickoff CMakeLists.txt"));
            return false;
        }

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
        if (!writeFile(buildDir + QStringLiteral("/CMakeLists.txt"), QString::fromLatin1(SYSTRAY_CMAKE))) {
            appendLog(QStringLiteral("  ERROR: Cannot write systray CMakeLists.txt"));
            return false;
        }

        // Symlinks to workspace subdirectories
        auto link = [&](const QString &name, const QString &target) {
            QFile::link(target, buildDir + QLatin1Char('/') + name);
        };
        link(QStringLiteral("systemtray"),            cloneDir + QStringLiteral("/applets/systemtray"));
        link(QStringLiteral("libdbusmenuqt"),          cloneDir + QStringLiteral("/libdbusmenuqt"));
        link(QStringLiteral("libkmpris"),              cloneDir + QStringLiteral("/libkmpris"));
        link(QStringLiteral("statusnotifierwatcher"),  cloneDir + QStringLiteral("/statusnotifierwatcher"));

        // Write config-X11.h into build dir
        if (!writeFile(buildDir + QStringLiteral("/build/config-X11.h"), QString::fromLatin1(CONFIG_X11_H))) {
            appendLog(QStringLiteral("  ERROR: Cannot write config-X11.h"));
            return false;
        }

        // Patch systemtray CMakeLists.txt: add include_directories for config-X11.h
        QString systraycmake = cloneDir + QStringLiteral("/applets/systemtray/CMakeLists.txt");
        QString content = readFile(systraycmake);
        if (content.isNull()) {
            appendLog(QStringLiteral("  ERROR: Cannot read systemtray CMakeLists.txt"));
            return false;
        }

        const QString marker = QStringLiteral("include_directories(${CMAKE_BINARY_DIR})");
        if (!content.contains(marker)) {
            const QString searchStr = QStringLiteral(
                R"(add_definitions(-DTRANSLATION_DOMAIN=\"plasma_applet_org.kde.plasma.systemtray\"))");
            if (!content.contains(searchStr)) {
                appendLog(QStringLiteral("  ERROR: Cannot find add_definitions line in systemtray CMakeLists.txt"));
                return false;
            }
            content.replace(searchStr, searchStr + QStringLiteral("\n") + marker);
            if (!writeFile(systraycmake, content)) {
                appendLog(QStringLiteral("  ERROR: Cannot write systemtray CMakeLists.txt"));
                return false;
            }
        }

        appendLog(QStringLiteral("  Created systray-build/"));
        return true;
    }

    bool updateBuildConfig(const QString &kickBuild, const QString &systrayBuild) {
        QFile f1(kickBuild + QStringLiteral("/CMakeLists.txt"));
        if (!f1.open(QIODevice::WriteOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot write kickoff CMakeLists.txt"));
            return false;
        }
        f1.write(KICKOFF_CMAKE);
        f1.close();

        QFile f2(systrayBuild + QStringLiteral("/CMakeLists.txt"));
        if (!f2.open(QIODevice::WriteOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot write systray CMakeLists.txt"));
            return false;
        }
        f2.write(SYSTRAY_CMAKE);
        f2.close();
        return true;
    }

    // ── QML patching ────────────────────────────────────────────────

    // Upstream fix for Plasma 6.7.0: sidebar category highlight missing when
    // switchCategoryOnHover is enabled. Fixed in 6.7.1 (KDE bug #521558, commit 1cb436c).
    // Only the visible condition changes — removes the switchCategoryOnHover guard.
    bool patchApplicationsPage() {
        QString path = m_dataDir + QStringLiteral("/kickoff-build/kickoff/ApplicationsPage.qml");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot open ") + path);
            return false;
        }
        QString content = QString::fromUtf8(f.readAll());
        f.close();

        const QString stockVisible = QStringLiteral(
            "                visible: !Plasmoid.configuration.switchCategoryOnHover\n"
            "                    && !sideBarDelegate.isSeparator\n"
            "                    && hovered");

        const QString fixedVisible = QStringLiteral(
            "                visible: !sideBarDelegate.isSeparator && hovered");

        if (!content.contains(stockVisible)) {
            // Already patched (6.7.1+) or format changed — skip, non-fatal
            return true;
        }

        content.replace(stockVisible, fixedVisible);

        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + path);
            return false;
        }
        f.write(content.toUtf8());
        f.close();
        appendLog(QStringLiteral("  Sidebar hover highlight fixed"));
        return true;
    }

    bool patchKickoffQml() {
        QString path = m_dataDir + QStringLiteral("/kickoff-build/kickoff/KickoffListDelegate.qml");
        QString content = readFile(path);
        if (content.isNull()) {
            appendLog(QStringLiteral("  ERROR: Cannot open ") + path);
            return false;
        }

        // Two cases: already patched (has "isCategoryListItem ? <N> :") or stock
        QRegularExpression patchedRe(QStringLiteral(R"(isCategoryListItem \? \d+ :)"));
        if (patchedRe.match(content).hasMatch()) {
            content.replace(patchedRe, QString("isCategoryListItem ? %1 :").arg(m_padding));
        } else {
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

        if (!writeFile(path, content)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + path);
            return false;
        }
        appendLog(QString("  Kickoff padding: %1 px").arg(m_padding));
        return true;
    }

    bool patchSystrayQml() {
        QString path = m_dataDir + QStringLiteral("/systray-build/systemtray/qml/main.qml");
        QString content = readFile(path);
        if (content.isNull()) {
            appendLog(QStringLiteral("  ERROR: Cannot open ") + path);
            return false;
        }

        // Find "if (autoSize) {" then replace the entire return statement on the next line
        QRegularExpression findBlock(QStringLiteral(R"(if \(autoSize\) \{)"));
        auto blockMatch = findBlock.match(content);
        if (!blockMatch.hasMatch()) {
            appendLog(QStringLiteral("  ERROR: Cannot find autoSize block in main.qml"));
            return false;
        }

        QRegularExpression findReturn(QStringLiteral(R"(return [^\n]+)"));
        auto retMatch = findReturn.match(content, blockMatch.capturedEnd());
        if (!retMatch.hasMatch()) {
            appendLog(QStringLiteral("  ERROR: Cannot find return in autoSize block"));
            return false;
        }

        content.replace(retMatch.capturedStart(), retMatch.capturedLength(),
                        QString("return %1").arg(iconSize()));

        if (!writeFile(path, content)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + path);
            return false;
        }
        appendLog(QString("  Systray icon size: %1 px").arg(iconSize()));
        return true;
    }

    bool patchShowdesktopQml() {
        QString path = m_dataDir + QStringLiteral("/kickoff-build/showdesktop/main.qml");
        QString content = readFile(path);
        if (content.isNull()) {
            appendLog(QStringLiteral("  ERROR: Cannot open ") + path);
            return false;
        }

        // Match both stock (Kirigami.Units.iconSizes.medium) and already-patched (numeric) forms
        QRegularExpression minW(QStringLiteral(R"(Layout\.minimumWidth: (?:Kirigami\.Units\.iconSizes\.medium|\d+))"));
        QRegularExpression maxW(QStringLiteral(R"(Layout\.maximumWidth: (?:Layout\.minimumWidth|\d+))"));
        QRegularExpression minH(QStringLiteral(R"(Layout\.minimumHeight: (?:Kirigami\.Units\.iconSizes\.medium|\d+))"));
        QRegularExpression maxH(QStringLiteral(R"(Layout\.maximumHeight: (?:Layout\.minimumHeight|\d+))"));

        if (!minW.match(content).hasMatch()) {
            appendLog(QStringLiteral("  ERROR: Cannot find Layout size pattern in showdesktop main.qml"));
            return false;
        }

        QString sizeStr = QString::number(iconSize());
        content.replace(minW, QStringLiteral("Layout.minimumWidth: ") + sizeStr);
        content.replace(maxW, QStringLiteral("Layout.maximumWidth: ") + sizeStr);
        content.replace(minH, QStringLiteral("Layout.minimumHeight: ") + sizeStr);
        content.replace(maxH, QStringLiteral("Layout.maximumHeight: ") + sizeStr);

        if (!writeFile(path, content)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + path);
            return false;
        }
        appendLog(QString("  Showdesktop icon size: %1 px").arg(iconSize()));
        return true;
    }

    bool patchDefaultCompactQml() {
        // Patch the shell's DefaultCompactRepresentation.qml to constrain icon size.
        // CompactApplet.qml sets anchors.fill on the compact rep AFTER creation,
        // so we use Component.onCompleted + Qt.callLater to override it:
        // undo anchors.fill, center in parent, constrain width/height.
        const QString sysPath = QStringLiteral(
            "/usr/share/plasma/shells/org.kde.plasma.desktop/contents/applet/DefaultCompactRepresentation.qml");
        // Read from .bak (stock original) if it exists, otherwise from the live file
        QString srcPath = sysPath + QStringLiteral(".bak");
        if (!QFile::exists(srcPath))
            srcPath = sysPath;

        QString content = readFile(srcPath);
        if (content.isNull()) {
            if (!QFile::exists(srcPath)) {
                appendLog(QStringLiteral("  DefaultCompactRepresentation.qml not found, skipping"));
                return true;
            }
            appendLog(QStringLiteral("  ERROR: Cannot read ") + srcPath);
            return false;
        }

        int lastBrace = content.lastIndexOf(QLatin1Char('}'));
        if (lastBrace < 0) {
            appendLog(QStringLiteral("  ERROR: Cannot find closing brace in DefaultCompactRepresentation.qml"));
            return false;
        }

        content.insert(lastBrace, QString(
            "\n    // plasma-tweaks: constrain icon size\n"
            "    Component.onCompleted: Qt.callLater(() => {\n"
            "        anchors.fill = undefined;\n"
            "        anchors.centerIn = parent;\n"
            "        width = Qt.binding(() => Math.min(parent ? parent.width : 0, parent ? parent.height : 0, %1));\n"
            "        height = Qt.binding(() => width);\n"
            "    })\n").arg(iconSize()));

        QString patchedPath = m_dataDir + QStringLiteral("/DefaultCompactRepresentation.qml");
        if (!writeFile(patchedPath, content)) {
            appendLog(QStringLiteral("  ERROR: Cannot write patched DefaultCompactRepresentation.qml"));
            return false;
        }
        appendLog(QString("  Default compact icon size: %1 px").arg(iconSize()));
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

        const QString compactQmlSrc = m_dataDir + QStringLiteral("/DefaultCompactRepresentation.qml");
        const QString compactQmlDst = QStringLiteral(
            "/usr/share/plasma/shells/org.kde.plasma.desktop/contents/applet/DefaultCompactRepresentation.qml");

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
)").arg(m_appletsDir, kickoffSo, showdesktopSo, systraySo);

        // Patch DefaultCompactRepresentation.qml (plain QML file, no rebuild needed)
        if (QFile::exists(compactQmlSrc)) {
            script += QString(R"(
# Backup and patch DefaultCompactRepresentation.qml
COMPACT_QML="%1"
[ ! -f "${COMPACT_QML}.bak" ] && cp "$COMPACT_QML" "${COMPACT_QML}.bak"
cp "%2" "$COMPACT_QML"
)").arg(compactQmlDst, compactQmlSrc);
        }

        script += QStringLiteral("\necho \"Installation complete\"\n");

        if (!writeFile(m_dataDir + QStringLiteral("/install.sh"), script)) {
            appendLog(QStringLiteral("  ERROR: Cannot write install.sh"));
            return false;
        }
        return true;
    }


    QString runCommandCapture(const QString &program, const QStringList &args, int timeoutMs = 10000) const {
        QProcess proc;
        proc.start(program, args);
        if (!proc.waitForFinished(timeoutMs))
            return QString();
        return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    }

    QString parseFcFamily(const QString &line) const {
        QRegularExpression quoted(QStringLiteral("\"([^\"]+)\""));
        auto match = quoted.match(line);
        if (match.hasMatch())
            return match.captured(1).trimmed();

        int colon = line.indexOf(QLatin1Char(':'));
        return (colon >= 0 ? line.left(colon) : line).trimmed();
    }

    QString extractIniValue(const QString &content, const QString &section, const QString &key) const {
        const QStringList lines = content.split(QLatin1Char('\n'));
        const QString sectionHeader = QStringLiteral("[") + section + QStringLiteral("]");
        bool inSection = false;

        for (const QString &rawLine : lines) {
            const QString trimmed = rawLine.trimmed();
            if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
                inSection = (trimmed == sectionHeader);
                continue;
            }
            if (!inSection || trimmed.startsWith(QLatin1Char('#')) || trimmed.startsWith(QLatin1Char(';')))
                continue;
            if (trimmed.startsWith(key + QLatin1Char('=')))
                return rawLine.mid(rawLine.indexOf(QLatin1Char('=')) + 1).trimmed();
        }

        return QString();
    }

    QString setIniValue(QString content, const QString &section, const QString &key, const QString &value) const {
        QStringList lines = content.split(QLatin1Char('\n'));
        const QString sectionHeader = QStringLiteral("[") + section + QStringLiteral("]");
        int sectionStart = -1;
        int sectionEnd = lines.size();

        for (int i = 0; i < lines.size(); ++i) {
            const QString trimmed = lines[i].trimmed();
            if (trimmed == sectionHeader) {
                sectionStart = i;
                for (int j = i + 1; j < lines.size(); ++j) {
                    const QString candidate = lines[j].trimmed();
                    if (candidate.startsWith(QLatin1Char('[')) && candidate.endsWith(QLatin1Char(']'))) {
                        sectionEnd = j;
                        break;
                    }
                }
                break;
            }
        }

        if (sectionStart < 0) {
            if (!content.isEmpty() && !content.endsWith(QLatin1Char('\n')))
                content += QLatin1Char('\n');
            if (!content.isEmpty() && !content.endsWith(QStringLiteral("\n\n")))
                content += QLatin1Char('\n');
            content += sectionHeader + QLatin1Char('\n') + key + QLatin1Char('=') + value + QLatin1Char('\n');
            return content;
        }

        for (int i = sectionStart + 1; i < sectionEnd; ++i) {
            const QString trimmed = lines[i].trimmed();
            if (trimmed.startsWith(key + QLatin1Char('='))) {
                lines[i] = key + QLatin1Char('=') + value;
                return lines.join(QLatin1Char('\n'));
            }
        }

        lines.insert(sectionEnd, key + QLatin1Char('=') + value);
        return lines.join(QLatin1Char('\n'));
    }

    QString setLineValue(QString content, const QString &key, const QString &value) const {
        const QRegularExpression re(QStringLiteral("(?m)^%1.*$").arg(QRegularExpression::escape(key)));
        const QString replacement = key + value;
        if (content.contains(re))
            content.replace(re, replacement);
        else {
            if (!content.isEmpty() && !content.endsWith(QLatin1Char('\n')))
                content += QLatin1Char('\n');
            content += replacement + QLatin1Char('\n');
        }
        return content;
    }

    QString quotedGtkFont(int size) const {
        return QStringLiteral("\"") + m_uiFontFamily + QStringLiteral(",  ") + QString::number(size) + QStringLiteral("\"");
    }

    QString plainGtkFont(int size) const {
        return m_uiFontFamily + QStringLiteral(",  ") + QString::number(size);
    }

    QString kdeFontValue(const QString &family, int size) const {
        return QStringLiteral("%1,%2,-1,5,400,0,0,0,0,0,0,0,0,0,0,1,,0,0")
            .arg(family, QString::number(size));
    }

    QString sanitizeXmlText(QString value) const {
        return value.toHtmlEscaped();
    }

    QString fontconfigBlock() const {
        return QString(
            "  <!-- plasma-tweaks: font strategy begin -->\n"
            "  <match>\n"
            "    <test compare=\"contains\" name=\"lang\">\n"
            "      <string>zh</string>\n"
            "    </test>\n"
            "    <test name=\"family\">\n"
            "      <string>monospace</string>\n"
            "    </test>\n"
            "    <edit binding=\"strong\" mode=\"prepend\" name=\"family\">\n"
            "      <string>%1</string>\n"
            "    </edit>\n"
            "  </match>\n"
            "  <alias>\n"
            "    <family>sans-serif</family>\n"
            "    <prefer>\n"
            "      <family>%2</family>\n"
            "      <family>%3</family>\n"
            "    </prefer>\n"
            "  </alias>\n"
            "  <alias>\n"
            "    <family>serif</family>\n"
            "    <prefer>\n"
            "      <family>%2</family>\n"
            "      <family>%3</family>\n"
            "    </prefer>\n"
            "  </alias>\n"
            "  <alias>\n"
            "    <family>monospace</family>\n"
            "    <prefer>\n"
            "      <family>%1</family>\n"
            "    </prefer>\n"
            "  </alias>\n"
            "  <match target=\"pattern\">\n"
            "    <test name=\"family\">\n"
            "      <string>%2</string>\n"
            "    </test>\n"
            "    <edit binding=\"weak\" mode=\"append\" name=\"family\">\n"
            "      <string>%3</string>\n"
            "    </edit>\n"
            "  </match>\n"
            "  <!-- plasma-tweaks: font strategy end -->")
            .arg(sanitizeXmlText(m_monoFontFamily), sanitizeXmlText(m_uiFontFamily), sanitizeXmlText(m_fallbackFontFamily));
    }

    QString systemFontconfigContent() const {
        return QString(
            "<?xml version=\"1.0\"?>\n"
            "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
            "<fontconfig>\n"
            "  <match target=\"pattern\">\n"
            "    <test qual=\"any\" name=\"family\">\n"
            "      <string>sans-serif</string>\n"
            "    </test>\n"
            "    <edit name=\"family\" mode=\"prepend_first\" binding=\"strong\">\n"
            "      <string>%1</string>\n"
            "    </edit>\n"
            "  </match>\n\n"
            "  <match target=\"pattern\">\n"
            "    <test qual=\"any\" name=\"family\">\n"
            "      <string>serif</string>\n"
            "    </test>\n"
            "    <edit name=\"family\" mode=\"prepend_first\" binding=\"strong\">\n"
            "      <string>%1</string>\n"
            "    </edit>\n"
            "  </match>\n\n"
            "  <match target=\"pattern\">\n"
            "    <test qual=\"any\" name=\"family\">\n"
            "      <string>monospace</string>\n"
            "    </test>\n"
            "    <edit name=\"family\" mode=\"prepend_first\" binding=\"strong\">\n"
            "      <string>%2</string>\n"
            "    </edit>\n"
            "  </match>\n\n"
            "  <match target=\"pattern\">\n"
            "    <test qual=\"any\" name=\"family\">\n"
            "      <string>%1</string>\n"
            "    </test>\n"
            "    <edit name=\"family\" mode=\"append\" binding=\"weak\">\n"
            "      <string>%3</string>\n"
            "    </edit>\n"
            "  </match>\n\n"
            "  <match target=\"pattern\">\n"
            "    <test qual=\"any\" name=\"family\">\n"
            "      <string>Fira Sans</string>\n"
            "    </test>\n"
            "    <edit name=\"family\" mode=\"assign\" binding=\"strong\">\n"
            "      <string>%1</string>\n"
            "    </edit>\n"
            "  </match>\n"
            "</fontconfig>\n")
            .arg(sanitizeXmlText(m_uiFontFamily), sanitizeXmlText(m_monoFontFamily), sanitizeXmlText(m_fallbackFontFamily));
    }

    bool updateUserFontconfig() {
        const QString path = QDir::homePath() + QStringLiteral("/.config/fontconfig/fonts.conf");
        QString content = readFile(path);
        if (content.isNull() || !content.contains(QStringLiteral("</fontconfig>"))) {
            content = QStringLiteral("<?xml version='1.0'?>\n<!DOCTYPE fontconfig SYSTEM 'urn:fontconfig:fonts.dtd'>\n<fontconfig>\n</fontconfig>\n");
        }

        const QString block = fontconfigBlock();
        const QRegularExpression managedBlock(
            QStringLiteral("\\s*<!-- plasma-tweaks: font strategy begin -->.*?<!-- plasma-tweaks: font strategy end -->\\s*"),
            QRegularExpression::DotMatchesEverythingOption);
        if (content.contains(managedBlock)) {
            content.replace(managedBlock, QStringLiteral("\n") + block + QStringLiteral("\n"));
        } else {
            content.replace(QStringLiteral("</fontconfig>"), QStringLiteral("\n") + block + QStringLiteral("\n</fontconfig>"));
        }

        if (!writeFile(path, content)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + path);
            return false;
        }
        appendLog(QStringLiteral("  Updated user fontconfig"));
        return true;
    }

    bool updateKdeGlobals() {
        const QString path = QDir::homePath() + QStringLiteral("/.config/kdeglobals");
        QString content = readFile(path);
        content = setIniValue(content, QStringLiteral("General"), QStringLiteral("font"), kdeFontValue(m_uiFontFamily, m_desktopUiSize));
        content = setIniValue(content, QStringLiteral("General"), QStringLiteral("fixed"), kdeFontValue(m_monoFontFamily, m_desktopMonoSize));
        if (!writeFile(path, content)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + path);
            return false;
        }
        appendLog(QStringLiteral("  Updated KDE fonts"));
        return true;
    }

    bool updateGtkConfigs() {
        const QString gtkFont = plainGtkFont(m_desktopUiSize);

        QString gtk3Path = QDir::homePath() + QStringLiteral("/.config/gtk-3.0/settings.ini");
        QString gtk3 = setIniValue(readFile(gtk3Path), QStringLiteral("Settings"), QStringLiteral("gtk-font-name"), gtkFont);
        if (!writeFile(gtk3Path, gtk3)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + gtk3Path);
            return false;
        }

        QString gtk4Path = QDir::homePath() + QStringLiteral("/.config/gtk-4.0/settings.ini");
        QString gtk4 = setIniValue(readFile(gtk4Path), QStringLiteral("Settings"), QStringLiteral("gtk-font-name"), gtkFont);
        if (!writeFile(gtk4Path, gtk4)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + gtk4Path);
            return false;
        }

        const QString gtk2Path = QDir::homePath() + QStringLiteral("/.gtkrc-2.0");
        QString gtk2 = setLineValue(readFile(gtk2Path), QStringLiteral("gtk-font-name="), quotedGtkFont(m_desktopUiSize));
        if (!writeFile(gtk2Path, gtk2)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + gtk2Path);
            return false;
        }

        const QString xsettingsPath = QDir::homePath() + QStringLiteral("/.config/xsettingsd/xsettingsd.conf");
        QString xsettings = setLineValue(readFile(xsettingsPath), QStringLiteral("Gtk/FontName "), quotedGtkFont(m_desktopUiSize));
        if (!writeFile(xsettingsPath, xsettings)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + xsettingsPath);
            return false;
        }

        appendLog(QStringLiteral("  Updated GTK + xsettingsd fonts"));
        return true;
    }

    QDomElement ensureApplicationRoot(QDomDocument &doc) const {
        QDomElement root = doc.documentElement();
        if (!root.isNull() && root.tagName() == QStringLiteral("application"))
            return root;

        doc.clear();
        doc.appendChild(doc.createProcessingInstruction(QStringLiteral("xml"), QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
        root = doc.createElement(QStringLiteral("application"));
        doc.appendChild(root);
        return root;
    }

    bool loadApplicationXml(const QString &path, QDomDocument &doc) {
        QFile file(path);
        if (!file.exists()) {
            ensureApplicationRoot(doc);
            return true;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            appendLog(QStringLiteral("  ERROR: Cannot read ") + path);
            return false;
        }

        const auto parseResult = doc.setContent(&file);
        if (!parseResult) {
            appendLog(QStringLiteral("  ERROR: XML parse failed for %1 (%2:%3 %4)")
                          .arg(path)
                          .arg(parseResult.errorLine)
                          .arg(parseResult.errorColumn)
                          .arg(parseResult.errorMessage));
            return false;
        }

        ensureApplicationRoot(doc);
        return true;
    }

    QDomElement ensureComponent(QDomDocument &doc, const QString &name) const {
        QDomElement root = ensureApplicationRoot(doc);
        for (QDomNode node = root.firstChild(); !node.isNull(); node = node.nextSibling()) {
            const QDomElement element = node.toElement();
            if (!element.isNull() && element.tagName() == QStringLiteral("component")
                && element.attribute(QStringLiteral("name")) == name) {
                return element;
            }
        }

        QDomElement component = doc.createElement(QStringLiteral("component"));
        component.setAttribute(QStringLiteral("name"), name);
        root.appendChild(component);
        return component;
    }

    QDomElement ensureOption(QDomDocument &doc, QDomElement component, const QString &name) const {
        for (QDomNode node = component.firstChild(); !node.isNull(); node = node.nextSibling()) {
            const QDomElement element = node.toElement();
            if (!element.isNull() && element.tagName() == QStringLiteral("option")
                && element.attribute(QStringLiteral("name")) == name) {
                return element;
            }
        }

        QDomElement option = doc.createElement(QStringLiteral("option"));
        option.setAttribute(QStringLiteral("name"), name);
        component.appendChild(option);
        return option;
    }

    void setOptionValue(QDomDocument &doc, QDomElement component, const QString &name, const QString &value) const {
        QDomElement option = ensureOption(doc, component, name);
        option.setAttribute(QStringLiteral("value"), value);
    }

    QString optionValue(const QDomElement &component, const QString &name) const {
        for (QDomNode node = component.firstChild(); !node.isNull(); node = node.nextSibling()) {
            const QDomElement element = node.toElement();
            if (!element.isNull() && element.tagName() == QStringLiteral("option")
                && element.attribute(QStringLiteral("name")) == name) {
                return element.attribute(QStringLiteral("value")).trimmed();
            }
        }
        return QString();
    }

    bool saveXml(const QString &path, const QDomDocument &doc) {
        return writeFile(path, doc.toString(2));
    }

    QStringList ideOptionDirs(const QString &basePath, const QString &prefix = QString()) const {
        QStringList result;
        QDir base(basePath);
        if (!base.exists())
            return result;

        const QFileInfoList entries = base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            const QString name = entry.fileName();
            if (!prefix.isEmpty() && !name.startsWith(prefix))
                continue;
            if (name.contains(QStringLiteral("backup"), Qt::CaseInsensitive))
                continue;

            const QString optionsDir = entry.filePath() + QStringLiteral("/options");
            if (QDir(optionsDir).exists())
                result.append(optionsDir);
        }
        return result;
    }

    bool updateIdeProfile(const QString &optionsDir) {
        QDomDocument otherDoc;
        const QString otherPath = optionsDir + QStringLiteral("/other.xml");
        if (!loadApplicationXml(otherPath, otherDoc))
            return false;
        {
            QDomElement component = ensureComponent(otherDoc, QStringLiteral("NotRoamableUiSettings"));
            setOptionValue(otherDoc, component, QStringLiteral("fontFace"), m_uiFontFamily);
            setOptionValue(otherDoc, component, QStringLiteral("fontSize"), QString::number(m_ideUiSize) + QStringLiteral(".0"));
            setOptionValue(otherDoc, component, QStringLiteral("overrideLafFonts"), QStringLiteral("true"));
        }
        if (!saveXml(otherPath, otherDoc)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + otherPath);
            return false;
        }

        QDomDocument editorDoc;
        const QString editorPath = optionsDir + QStringLiteral("/editor-font.xml");
        if (!loadApplicationXml(editorPath, editorDoc))
            return false;
        {
            QDomElement component = ensureComponent(editorDoc, QStringLiteral("DefaultFont"));
            setOptionValue(editorDoc, component, QStringLiteral("VERSION"), QStringLiteral("1"));
            setOptionValue(editorDoc, component, QStringLiteral("FONT_SIZE"), QString::number(m_ideCodeSize));
            setOptionValue(editorDoc, component, QStringLiteral("FONT_SIZE_2D"), QString::number(m_ideCodeSize) + QStringLiteral(".0"));
            setOptionValue(editorDoc, component, QStringLiteral("FONT_FAMILY"), m_monoFontFamily);
            setOptionValue(editorDoc, component, QStringLiteral("SECONDARY_FONT_FAMILY"), m_fallbackFontFamily);
        }
        if (!saveXml(editorPath, editorDoc)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + editorPath);
            return false;
        }

        QDomDocument terminalDoc;
        const QString terminalPath = optionsDir + QStringLiteral("/terminal-font.xml");
        if (!loadApplicationXml(terminalPath, terminalDoc))
            return false;
        {
            QDomElement component = ensureComponent(terminalDoc, QStringLiteral("TerminalFontOptions"));
            setOptionValue(terminalDoc, component, QStringLiteral("VERSION"), QStringLiteral("1"));
            setOptionValue(terminalDoc, component, QStringLiteral("FONT_SIZE"), QString::number(m_ideCodeSize));
            setOptionValue(terminalDoc, component, QStringLiteral("FONT_SIZE_2D"), QString::number(m_ideCodeSize) + QStringLiteral(".0"));
            setOptionValue(terminalDoc, component, QStringLiteral("FONT_FAMILY"), m_monoFontFamily);
            setOptionValue(terminalDoc, component, QStringLiteral("SECONDARY_FONT_FAMILY"), m_fallbackFontFamily);
        }
        if (!saveXml(terminalPath, terminalDoc)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + terminalPath);
            return false;
        }

        return true;
    }

    int updateIdeRoot(const QString &basePath, const QString &prefix) {
        int updated = 0;
        const QStringList dirs = ideOptionDirs(basePath, prefix);
        for (const QString &dir : dirs) {
            if (!updateIdeProfile(dir))
                return -1;
            ++updated;
        }
        return updated;
    }

    bool stageRootFontFiles() {
        const QString systemConfPath = m_dataDir + QStringLiteral("/99-len-fonts.conf");
        if (!writeFile(systemConfPath, systemFontconfigContent())) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + systemConfPath);
            return false;
        }

        const QString sddmPath = QStringLiteral("/etc/sddm.conf.d/kde_settings.conf");
        QString sddmContent = readFile(sddmPath);
        sddmContent = setIniValue(sddmContent, QStringLiteral("Theme"), QStringLiteral("Font"), kdeFontValue(m_uiFontFamily, m_desktopUiSize));
        const QString stagedSddmPath = m_dataDir + QStringLiteral("/kde_settings.conf");
        if (!writeFile(stagedSddmPath, sddmContent)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + stagedSddmPath);
            return false;
        }

        const QString scriptPath = m_dataDir + QStringLiteral("/apply-font-root.sh");
        const QString script = QString(
            "#!/bin/bash\n"
            "set -e\n"
            "install -Dm644 %1 /etc/fonts/conf.d/99-len-fonts.conf\n"
            "install -Dm644 %2 /etc/sddm.conf.d/kde_settings.conf\n"
            "fc-cache -f\n"
            "echo Root font strategy installed\n")
            .arg(shellQuote(systemConfPath), shellQuote(stagedSddmPath));
        if (!writeFile(scriptPath, script)) {
            appendLog(QStringLiteral("  ERROR: Cannot write ") + scriptPath);
            return false;
        }
        QFile::setPermissions(scriptPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                         | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                         | QFileDevice::ReadOther | QFileDevice::ExeOther);
        appendLog(QStringLiteral("  Staged root font files"));
        return true;
    }

    QString shellQuote(const QString &value) const {
        QString escaped = value;
        escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        return QStringLiteral("'") + escaped + QStringLiteral("'");
    }

    bool applyUserFontStrategy() {
        if (m_applyKde) {
            if (!updateUserFontconfig() || !updateKdeGlobals())
                return false;
        }

        if (m_applyGtk && !updateGtkConfigs())
            return false;

        if (m_applyJetBrains) {
            const int count = updateIdeRoot(QDir::homePath() + QStringLiteral("/.config/JetBrains"), QString());
            if (count < 0)
                return false;
            appendLog(QStringLiteral("  Updated %1 JetBrains profile(s)").arg(count));
        }

        if (m_applyAndroidStudio) {
            const int count = updateIdeRoot(QDir::homePath() + QStringLiteral("/.config/Google"), QStringLiteral("AndroidStudio"));
            if (count < 0)
                return false;
            appendLog(QStringLiteral("  Updated %1 Android Studio profile(s)").arg(count));
        }

        return true;
    }


    void refreshFontStateImpl() {
        QString detectedUi = parseFcFamily(runCommandCapture(QStringLiteral("fc-match"), {QStringLiteral("sans-serif")}));
        QString detectedMono = parseFcFamily(runCommandCapture(QStringLiteral("fc-match"), {QStringLiteral("monospace")}));
        QStringList fallbackLines = runCommandCapture(QStringLiteral("fc-match"), {QStringLiteral("-s"), QStringLiteral("sans-serif")}).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QString detectedFallback;
        for (const QString &line : fallbackLines) {
            const QString family = parseFcFamily(line);
            if (!family.isEmpty() && family != detectedUi) {
                detectedFallback = family;
                break;
            }
        }

        const QString kdeGlobals = readFile(QDir::homePath() + QStringLiteral("/.config/kdeglobals"));
        const QString kdeFont = extractIniValue(kdeGlobals, QStringLiteral("General"), QStringLiteral("font"));
        const QString kdeFixed = extractIniValue(kdeGlobals, QStringLiteral("General"), QStringLiteral("fixed"));

        auto parseFontTuple = [](const QString &value, QString *family, int *size) {
            if (family)
                family->clear();
            if (size)
                *size = 0;
            if (value.isEmpty())
                return;
            const QStringList parts = value.split(QLatin1Char(','));
            if (family && !parts.isEmpty())
                *family = parts[0].trimmed();
            if (size && parts.size() > 1)
                *size = parts[1].trimmed().toInt();
        };

        QString kdeUiFamily;
        int kdeUiSize = 0;
        parseFontTuple(kdeFont, &kdeUiFamily, &kdeUiSize);
        QString kdeMonoFamily;
        int kdeMonoSize = 0;
        parseFontTuple(kdeFixed, &kdeMonoFamily, &kdeMonoSize);

        const QString gtk3 = readFile(QDir::homePath() + QStringLiteral("/.config/gtk-3.0/settings.ini"));
        const QString gtkFont = extractIniValue(gtk3, QStringLiteral("Settings"), QStringLiteral("gtk-font-name"));
        const QString xsettings = readFile(QDir::homePath() + QStringLiteral("/.config/xsettingsd/xsettingsd.conf"));
        QString xsettingsFont;
        {
            QRegularExpression re(QStringLiteral("(?m)^Gtk/FontName\\s+\"([^\"]+)\""));
            auto match = re.match(xsettings);
            if (match.hasMatch())
                xsettingsFont = match.captured(1).trimmed();
        }

        const QString sddm = readFile(QStringLiteral("/etc/sddm.conf.d/kde_settings.conf"));
        const QString sddmFont = extractIniValue(sddm, QStringLiteral("Theme"), QStringLiteral("Font"));

        const QStringList jetBrainsDirs = ideOptionDirs(QDir::homePath() + QStringLiteral("/.config/JetBrains"));
        const QStringList androidStudioDirs = ideOptionDirs(QDir::homePath() + QStringLiteral("/.config/Google"), QStringLiteral("AndroidStudio"));

        if (!detectedUi.isEmpty())
            m_uiFontFamily = detectedUi;
        if (!detectedFallback.isEmpty())
            m_fallbackFontFamily = detectedFallback;
        if (!detectedMono.isEmpty())
            m_monoFontFamily = detectedMono;
        if (kdeUiSize > 0)
            m_desktopUiSize = kdeUiSize;
        if (kdeMonoSize > 0)
            m_desktopMonoSize = kdeMonoSize;

        auto loadIdeSizes = [this](const QStringList &dirs) {
            for (const QString &dir : dirs) {
                QDomDocument otherDoc;
                if (loadApplicationXml(dir + QStringLiteral("/other.xml"), otherDoc)) {
                    const QDomElement comp = ensureComponent(otherDoc, QStringLiteral("NotRoamableUiSettings"));
                    const QString uiSize = optionValue(comp, QStringLiteral("fontSize"));
                    if (!uiSize.isEmpty()) {
                        m_ideUiSize = uiSize.toDouble();
                        break;
                    }
                }
            }
            for (const QString &dir : dirs) {
                QDomDocument editorDoc;
                if (loadApplicationXml(dir + QStringLiteral("/editor-font.xml"), editorDoc)) {
                    const QDomElement comp = ensureComponent(editorDoc, QStringLiteral("DefaultFont"));
                    const QString codeSize = optionValue(comp, QStringLiteral("FONT_SIZE"));
                    if (!codeSize.isEmpty()) {
                        m_ideCodeSize = codeSize.toInt();
                        break;
                    }
                }
            }
        };
        if (!jetBrainsDirs.isEmpty())
            loadIdeSizes(jetBrainsDirs);
        else if (!androidStudioDirs.isEmpty())
            loadIdeSizes(androidStudioDirs);

        QStringList stateLines;
        stateLines << QStringLiteral("Fontconfig sans-serif: %1").arg(detectedUi.isEmpty() ? QStringLiteral("(unknown)") : detectedUi)
                   << QStringLiteral("Fontconfig CJK fallback: %1").arg(detectedFallback.isEmpty() ? QStringLiteral("(unknown)") : detectedFallback)
                   << QStringLiteral("Fontconfig monospace: %1").arg(detectedMono.isEmpty() ? QStringLiteral("(unknown)") : detectedMono)
                   << QStringLiteral("KDE UI / fixed: %1 / %2").arg(kdeFont.isEmpty() ? QStringLiteral("(unset)") : kdeFont,
                                                                   kdeFixed.isEmpty() ? QStringLiteral("(unset)") : kdeFixed)
                   << QStringLiteral("GTK: %1").arg(gtkFont.isEmpty() ? QStringLiteral("(unset)") : gtkFont)
                   << QStringLiteral("xsettingsd: %1").arg(xsettingsFont.isEmpty() ? QStringLiteral("(unset)") : xsettingsFont)
                   << QStringLiteral("SDDM: %1").arg(sddmFont.isEmpty() ? QStringLiteral("(unset)") : sddmFont)
                   << QStringLiteral("JetBrains profiles: %1").arg(jetBrainsDirs.size())
                   << QStringLiteral("Android Studio profiles: %1").arg(androidStudioDirs.size())
                   << QStringLiteral("IDE defaults: UI %1 / code %2").arg(m_ideUiSize).arg(m_ideCodeSize);

        const QString newState = stateLines.join(QLatin1Char('\n'));
        if (m_fontState != newState) {
            m_fontState = newState;
            emit fontStateChanged();
        }
        emit fontSettingsChanged();
    }

    void applyFontStrategyImpl() {
        if (m_busy)
            return;

        m_steps.clear();
        addStepAction(QStringLiteral("Writing user font settings..."), [this]() {
            return applyUserFontStrategy();
        });

        if (m_applySddm) {
            addStepAction(QStringLiteral("Preparing root font files..."), [this]() {
                return stageRootFontFiles();
            });
            addStep(QStringLiteral("Installing SDDM + system fontconfig (pkexec)..."),
                    QStringLiteral("pkexec bash ") + shellQuote(m_dataDir + QStringLiteral("/apply-font-root.sh")),
                    m_dataDir);
        }

        addStep(QStringLiteral("Refreshing font cache..."), QStringLiteral("fc-cache -f"), QString());
        if (m_applyGtk) {
            addStep(QStringLiteral("Reloading xsettingsd..."), QStringLiteral("pkill -HUP xsettingsd || true"), QString());
        }
        addStepAction(QStringLiteral("Refreshing font summary..."), [this]() {
            refreshFontState();
            return true;
        });
        runSteps();
    }

    // ── Settings persistence ────────────────────────────────────────

    void saveSettings() {
        writeFile(m_dataDir + QStringLiteral("/settings"),
                  QString("%1 %2\n").arg(m_padding).arg(iconSize()));
    }

    bool loadSettings() {
        QString line = readFile(m_dataDir + QStringLiteral("/settings"));
        if (line.isNull()) return false;
        QStringList parts = line.trimmed().split(QLatin1Char(' '));
        if (parts.size() < 2) return false;

        m_padding = parts[0].toInt();
        emit paddingChanged();
        setIconSizeFromValue(parts[1].toInt());
        return true;
    }

    void setIconSizeFromValue(int size) {
        auto it = std::min_element(m_iconSizes.begin(), m_iconSizes.end(),
            [size](int a, int b) { return qAbs(a - size) < qAbs(b - size); });
        m_iconSizeIdx = static_cast<int>(std::distance(m_iconSizes.begin(), it));
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

        QString kickoffContent = readFile(m_dataDir + QStringLiteral("/kickoff-build/kickoff/KickoffListDelegate.qml"));
        if (!kickoffContent.isNull()) {
            QRegularExpression re(QStringLiteral(R"(isCategoryListItem \? (\d+) :)"));
            auto match = re.match(kickoffContent);
            if (match.hasMatch()) {
                m_padding = match.captured(1).toInt();
                emit paddingChanged();
                paddingPatched = true;
            }
        }

        QString systrayContent = readFile(m_dataDir + QStringLiteral("/systray-build/systemtray/qml/main.qml"));
        if (!systrayContent.isNull()) {
            QRegularExpression findBlock(QStringLiteral(R"(if \(autoSize\) \{)"));
            auto blockMatch = findBlock.match(systrayContent);
            if (blockMatch.hasMatch()) {
                QRegularExpression findReturn(QStringLiteral(R"(return (\d+)\s*\n)"));
                auto retMatch = findReturn.match(systrayContent, blockMatch.capturedEnd());
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
    QString m_dataDir;
    QString m_plasmaVersion;
    QString m_appletsDir;
    QString m_uiFontFamily = QStringLiteral("IBM Plex Sans");
    QString m_fallbackFontFamily = QStringLiteral("HarmonyOS Sans SC");
    QString m_monoFontFamily = QStringLiteral("Maple Mono CN");
    int m_desktopUiSize = 14;
    int m_desktopMonoSize = 15;
    int m_ideUiSize = 17;
    int m_ideCodeSize = 16;
    bool m_applyKde = true;
    bool m_applyGtk = true;
    bool m_applySddm = true;
    bool m_applyJetBrains = true;
    bool m_applyAndroidStudio = true;
    QString m_fontState;

    QList<Step> m_steps;
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
