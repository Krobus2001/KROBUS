
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "wakeworddetector.h"

#include <QApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QCloseEvent>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QMenuBar>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QIcon>
#include <QScreen>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDir>
#include <QCursor>
#include <QColor>
#include <QRandomGenerator>
#include <QDateTime>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QAbstractSocket>
#include <QUrl>
#include <QUrlQuery>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>

#include <QAudioSource>
#include <QAudioSink>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QBuffer>

#include <QWebSocket>
#include <QPointer>
#include <QDebug>

#include <cmath>
#include <memory>


#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QStringList>



#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Unique identifier for the KROBUS hotkey.
static constexpr int KROBUS_VOICE_HOTKEY = 0x4B52;


// ============================================
// 1. MAIN WINDOW
// ============================================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setWindowTitle("KROBUS");
    setFixedSize(210, 210);

    Qt::WindowFlags flags =
        Qt::Window | Qt::FramelessWindowHint;

    if (settings.value("alwaysOnTop", true).toBool()) {
        flags |= Qt::WindowStaysOnTopHint;
    }

    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);

    menuBar()->hide();
    statusBar()->hide();

    if (centralWidget()) {
        centralWidget()->setAttribute(
            Qt::WA_TransparentForMouseEvents);
    }

    // KROBUS no longer needs a manual sleep menu.
    manualSleep = false;

    // Load the original KROBUS sprites.
    idleSprite.load(":/krobus/idle.png");
    happySprite.load(":/krobus/happy.png");
    thinkingSprite.load(":/krobus/thinking.png");
    sadSprite.load(":/krobus/sad.png");
    surprisedSprite.load(":/krobus/surprised.png");
    sleepingSprite.load(":/krobus/sleeping.png");

    if (idleSprite.isNull() ||
        happySprite.isNull() ||
        thinkingSprite.isNull() ||
        sadSprite.isNull() ||
        surprisedSprite.isNull() ||
        sleepingSprite.isNull()) {

        QMessageBox::warning(
            this,
            "KROBUS",
            "Some character sprites could not be loaded.");
    }

    // Restore saved desktop position.
    if (settings.contains("windowPosition")) {

        QPoint saved =
            settings.value("windowPosition").toPoint();

        for (QScreen *screen :
             QGuiApplication::screens()) {

            if (screen->availableGeometry().contains(
                    QRect(saved, size()).center())) {

                move(saved);
                break;
            }
        }
    }

    // Enable KROBUS's idle life.
    wanderEnabled = true;

    nextIdleEventMs =
        QDateTime::currentMSecsSinceEpoch() + 10000;

    setupTray();


    // Restore the selected frog and outfit.
    loadAppearanceSprites();



    // ============================================
    // REGISTER GLOBAL VOICE SHORTCUT
    // CTRL + ALT + K
    // ============================================

#ifdef Q_OS_WIN

    HWND windowHandle =
        reinterpret_cast<HWND>(winId());

    globalHotkeyRegistered =
        RegisterHotKey(
            windowHandle,
            KROBUS_VOICE_HOTKEY,
            MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
            'K'
            ) != 0;

    if (globalHotkeyRegistered) {

        qInfo()
        << "[KROBUS] Global shortcut ready:"
        << "Shift + Space";

    } else {

        qWarning()
        << "[KROBUS] Could not register shortcut."
        << "Windows error:" << GetLastError();
    }

#endif



    // ============================================
    // LOCAL "HEY KROBUS" WAKE-WORD DETECTOR
    // ============================================

    wakeWordDetector = new WakeWordDetector(this);

    connect(
        wakeWordDetector,
        &WakeWordDetector::wakeWordDetected,
        this,
        [this]() {

            if (appQuitting || !isVisible()) {
                return;
            }

            qInfo()
                << "[KROBUS] Hey Krobus detected!";

            // Local detection also works offline,
            // but Gemini requires the internet.
            if (!internetAvailable) {

                qWarning()
                << "[KROBUS] Internet unavailable.";

                QTimer::singleShot(
                    300,
                    this,
                    [this]() {
                        startWakeWordStandby();
                    }
                    );

                return;
            }

            // Allow exactly one Gemini conversation.
            voiceSessionRequested = true;

            isWandering = false;
            idleActing = false;

            currentMood = Mood::Surprised;
            update();

            // Wake-word capture is already stopped
            // by the detector when this signal fires.
            scheduleVoiceConnection(350);
        }
        );

    connect(
        wakeWordDetector,
        &WakeWordDetector::errorOccurred,
        this,
        [](const QString &message) {

            qWarning().noquote()
            << "[KROBUS] Wake-word error:"
            << message;
        }
        );


    // Character animation and roaming timer.
    QTimer *animationTimer = new QTimer(this);

    connect(
        animationTimer,
        &QTimer::timeout,
        this,
        [this]() {

            animationTick =
                (animationTick + 1) % 120;

            // End the temporary "thinking" expression
            // when the user has stopped speaking.
            if (isRecording &&
                !liveResponding &&
                !liveVoiceSink &&
                currentMood == Mood::Thinking &&
                QDateTime::currentMSecsSinceEpoch() >
                    voiceActivityUntilMs) {

                currentMood = Mood::Idle;
            }

            updateIdleLife();
            update();
        }
    );

    animationTimer->start(100);

    // Internet checker.
    networkManager = new QNetworkAccessManager(this);

    QTimer *connectionTimer = new QTimer(this);

    connect(
        connectionTimer,
        &QTimer::timeout,
        this,
        &MainWindow::checkInternet
    );

    connectionTimer->start(10000);
    checkInternet();

    // Force KROBUS to appear at startup.
    // This also overrides an old "start hidden" setting.
    QTimer::singleShot(
        0,
        this,
        [this]() {

            if (appQuitting) {
                return;
            }

            if (!isVisible()) {
                show();
            }


            qInfo()
                << "[KROBUS] Desktop companion started.";
        }
        );

    // Start local listening after the window appears.
    // Gemini is NOT connected at startup.
    QTimer::singleShot(
        450,
        this,
        [this]() {
            startWakeWordStandby();
        }
        );
}


MainWindow::~MainWindow()
{
    appQuitting = true;


#ifdef Q_OS_WIN

    if (globalHotkeyRegistered) {

        UnregisterHotKey(
            reinterpret_cast<HWND>(winId()),
            KROBUS_VOICE_HOTKEY
            );

        globalHotkeyRegistered = false;
    }

#endif



    if (wakeWordDetector) {
        wakeWordDetector->stop();
    }


    stopVoiceSession();
    stopPlayback();
    stopLiveVoice();

    settings.setValue("windowPosition", pos());
    settings.sync();

    delete ui;
}



bool MainWindow::nativeEvent(
    const QByteArray &eventType,
    void *message,
    qintptr *result)
{
#ifdef Q_OS_WIN

    if (eventType == "windows_generic_MSG") {

        MSG *msg = static_cast<MSG *>(message);

        if (msg->message == WM_HOTKEY &&
            msg->wParam == KROBUS_VOICE_HOTKEY) {

            if (result) {
                *result = 0;
            }

            if (appQuitting) {
                return true;
            }

            // ====================================
            // CTRL + ALT + K: CANCEL CONVERSATION
            // ====================================

            if (voiceSessionRequested ||
                liveSocket ||
                liveVoiceSink) {

                qInfo()
                << "[KROBUS] Shortcut pressed."
                << "Cancelling voice session.";

                // Stop Gemini and microphone capture.
                stopVoiceSession();

                // Stop KROBUS if he is speaking.
                stopLiveVoice();

                currentMood = Mood::Idle;
                update();

                qInfo()
                    << "[KROBUS] Voice cancelled."
                    << "Returning to standby.";

                return true;
            }

            // ====================================
            // CTRL + ALT + K: START CONVERSATION
            // ====================================

            // Do not interfere with legacy
            // microphone tests or other playback.
            if (isRecording ||
                speakerSink ||
                geminiBusy) {

                qInfo()
                << "[KROBUS] Another audio"
                   " operation is active.";

                return true;
            }

            // Show KROBUS if hidden.
            if (!isVisible()) {

                show();
                raise();
                refreshMenu();
            }

            // Gemini requires internet access.
            if (!internetAvailable) {

                qWarning()
                << "[KROBUS] No internet."
                << "Cannot start Gemini.";

                checkInternet();
                return true;
            }

            // Stop the old wake-word detector
            // if it happens to be running.
            if (wakeWordDetector) {
                wakeWordDetector->stop();
            }

            // Begin a new Gemini activation.
            ++voiceActivationToken;

            voiceSpeechDetected = false;
            voiceSessionRequested = true;

            isWandering = false;
            idleActing = false;

            currentMood = Mood::Surprised;
            update();

            qInfo()
                << "[KROBUS] Shortcut activated."
                << "Connecting to Gemini.";

            // Start the existing voice system.
            scheduleVoiceConnection(0);

            return true;
        }
    }

#endif

    return QMainWindow::nativeEvent(
        eventType,
        message,
        result
        );
}



// ============================================
// 2. MOOD SYSTEM
// ============================================

MainWindow::Mood MainWindow::displayedMood() const
{
    if (!connectionKnown || !internetAvailable) {
        return Mood::Sleeping;
    }

    if (manualSleep) {
        return Mood::Sleeping;
    }

    if (wakingUp) {
        return Mood::Surprised;
    }

    return currentMood;
}

void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    painter.setRenderHint(
        QPainter::SmoothPixmapTransform,
        false
    );

    Mood mood = displayedMood();

    const QPixmap *sprite = &idleSprite;

    switch (mood) {

    case Mood::Idle:
        sprite = &idleSprite;
        break;

    case Mood::Happy:
        sprite = &happySprite;
        break;

    case Mood::Thinking:
        sprite = &thinkingSprite;
        break;

    case Mood::Sad:
        sprite = &sadSprite;
        break;

    case Mood::Surprised:
        sprite = &surprisedSprite;
        break;

    case Mood::Sleeping:
        sprite = &sleepingSprite;
        break;
    }

    int offsetY = 0;

    if (mood == Mood::Idle) {

        offsetY = static_cast<int>(
            2 * std::sin(animationTick * 0.2));

    } else if (mood == Mood::Sleeping) {

        offsetY = static_cast<int>(
            2 * std::sin(animationTick * 0.1));

    } else if (mood == Mood::Happy) {

        int frame = animationTick % 10;

        offsetY = frame < 5
                      ? -frame * 2
                      : -(10 - frame) * 2;
    }

    // Hop while roaming.
    if (isWandering && mood == Mood::Idle) {

        offsetY -= static_cast<int>(
            5 * std::abs(
                std::sin(animationTick * 0.55)));
    }

    QRect destination(
        0,
        offsetY,
        width(),
        height()
    );

    // Make KROBUS face his walking direction.
    if (facingLeft) {

        painter.save();

        painter.translate(width(), 0);
        painter.scale(-1.0, 1.0);

        painter.drawPixmap(destination, *sprite);

        painter.restore();

    } else {

        painter.drawPixmap(destination, *sprite);
    }

    // The indicator remains visible whenever
    // the microphone is actively capturing.
    if (isRecording) {

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(230, 45, 45));

        painter.drawEllipse(
            QRect(185, 10, 14, 14));
    }
}

// ============================================
// 3. INTERNET CONNECTION
// ============================================

void MainWindow::checkInternet()
{
    if (connectionCheckRunning || !networkManager) {
        return;
    }

    connectionCheckRunning = true;

    QNetworkRequest request(
        QUrl("https://www.google.com/generate_204"));

    request.setTransferTimeout(5000);

    QNetworkReply *reply =
        networkManager->get(request);

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply]() {

            int status = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute
            ).toInt();

            bool connected =
                reply->error() == QNetworkReply::NoError
                && status == 204;

            bool wasOnline = internetAvailable;

            connectionKnown = true;
            internetAvailable = connected;
            connectionCheckRunning = false;

            if (!connected) {

                wakingUp = false;

                // Disconnect the microphone immediately
                // when internet access is lost.

                if (voiceSessionRequested || liveSocket) {
                    stopVoiceSession();
                }


            } else if (!wasOnline) {

                wakingUp = true;

                QTimer::singleShot(
                    1200,
                    this,
                    [this]() {

                        wakingUp = false;
                        update();
                    }
                );

            }


            refreshMenu();
            update();

            reply->deleteLater();
        }
    );
}

// ============================================
// 4. SIMPLE RIGHT-CLICK MENU
// ============================================

void MainWindow::setupTray()
{
    popupMenu = new QMenu(this);

    // Only the requested two options.
    showHideAction =
        popupMenu->addAction("Hide KROBUS");

    connect(
        showHideAction,
        &QAction::triggered,
        this,
        &MainWindow::toggleVisibility
    );


    // ============================================
    // APPEARANCE MENU
    // ============================================

    QAction *appearanceAction =
        popupMenu->addAction("Appearance...");

    connect(
        appearanceAction,
        &QAction::triggered,
        this,
        &MainWindow::showAppearanceDialog
        );


    QAction *quitAction =
        popupMenu->addAction("Quit KROBUS");

    connect(
        quitAction,
        &QAction::triggered,
        this,
        [this]() {

            qInfo() << "[KROBUS] Quitting.";

            appQuitting = true;


            if (wakeWordDetector) {
                wakeWordDetector->stop();
            }


            stopVoiceSession();
            stopPlayback();
            stopLiveVoice();

            settings.setValue(
                "windowPosition", pos());

            settings.sync();

            QApplication::quit();
        }
    );

    connect(
        popupMenu,
        &QMenu::aboutToShow,
        this,
        &MainWindow::refreshMenu
    );

    // Windows system tray icon.
    if (QSystemTrayIcon::isSystemTrayAvailable()) {

        trayIcon = new QSystemTrayIcon(
            QIcon(idleSprite),
            this
        );

        trayIcon->setToolTip(
            "KROBUS - Desktop AI Companion");

        trayIcon->setContextMenu(popupMenu);

        connect(
            trayIcon,
            &QSystemTrayIcon::activated,
            this,
            [this](
                QSystemTrayIcon::ActivationReason reason) {

                if (reason ==
                    QSystemTrayIcon::Trigger) {

                    toggleVisibility();
                }
            }
        );

        trayIcon->show();

        QApplication::setQuitOnLastWindowClosed(false);
    }

    refreshMenu();
}

// ============================================
// 5. WINDOW VISIBILITY
// ============================================


void MainWindow::toggleVisibility()
{
    if (isVisible()) {

        // Privacy: stop both microphone modes.
        if (wakeWordDetector) {
            wakeWordDetector->stop();
        }

        stopVoiceSession();
        stopLiveVoice();

        isWandering = false;
        idleActing = false;

        hide();

        qInfo()
            << "[KROBUS] Hidden."
            << "All microphone capture stopped.";

    } else {

        show();
        raise();

        currentMood = Mood::Idle;

        nextIdleEventMs =
            QDateTime::currentMSecsSinceEpoch()
            + 8000;

        qInfo()
            << "[KROBUS] Visible."
            << "Starting local wake-word standby.";

        QTimer::singleShot(
            300,
            this,
            [this]() {
                startWakeWordStandby();
            }
            );

        checkInternet();
    }

    refreshMenu();
}


bool MainWindow::startHidden() const
{
    // KROBUS must always appear when launched.
    return false;
}


void MainWindow::loadAppearanceSprites()
{
    const QString frog =
        settings.value(
                    "appearance/frog", "classic"
                    ).toString();

    const QString outfit =
        settings.value(
                    "appearance/outfit", "none"
                    ).toString();

    const QString folder =
        QDir(QCoreApplication::applicationDirPath())
            .filePath(
                "appearances/sprites/" +
                frog + "/" + outfit
                );

    const QStringList moods = {
        "idle",
        "happy",
        "thinking",
        "sad",
        "surprised",
        "sleeping"
    };

    QPixmap images[6];

    // Load every mood before replacing
    // the current character sprites.
    for (int i = 0; i < 6; ++i) {

        QString path =
            QDir(folder).filePath(
                moods[i] + ".png"
                );

        if (!images[i].load(path)) {

            qWarning().noquote()
            << "[KROBUS] Sprite missing:"
            << path;

            return;
        }
    }

    idleSprite = images[0];
    happySprite = images[1];
    thinkingSprite = images[2];
    sadSprite = images[3];
    surprisedSprite = images[4];
    sleepingSprite = images[5];

    // Update the system tray icon too.
    if (trayIcon) {
        trayIcon->setIcon(QIcon(idleSprite));
    }

    update();

    qInfo()
        << "[KROBUS] Appearance changed:"
        << frog << outfit;
}


void MainWindow::refreshMenu()
{
    if (!popupMenu || !showHideAction) {
        return;
    }

    showHideAction->setText(
        isVisible()
            ? "Hide KROBUS"
            : "Show KROBUS"
    );
}


void MainWindow::showAppearanceDialog()
{
    QDialog dialog(this);

    dialog.setWindowTitle("Customize KROBUS");
    dialog.setMinimumWidth(350);

    QVBoxLayout layout(&dialog);

    QLabel heading("Customize your frog!");
    heading.setAlignment(Qt::AlignCenter);
    layout.addWidget(&heading);

    QFormLayout form;

    // ============================================
    // FROG COLORS
    // ============================================

    QComboBox frogCombo;

    frogCombo.addItem(
        "Classic Green", "classic"
        );

    frogCombo.addItem(
        "Azure Blue", "azure"
        );

    frogCombo.addItem(
        "Golden Yellow", "golden"
        );

    frogCombo.addItem(
        "Crimson Red", "crimson"
        );

    // ============================================
    // OUTFITS
    // ============================================

    QComboBox outfitCombo;

    outfitCombo.addItem(
        "No Outfit", "none"
        );

    outfitCombo.addItem(
        "Hoodie", "hoodie"
        );

    outfitCombo.addItem(
        "Adventurer", "adventurer"
        );

    outfitCombo.addItem(
        "Wizard", "wizard"
        );

    outfitCombo.addItem(
        "Knight", "knight"
        );

    outfitCombo.addItem(
        "Strawberry", "strawberry"
        );

    form.addRow("Frog Color:", &frogCombo);
    form.addRow("Outfit:", &outfitCombo);

    layout.addLayout(&form);

    // Restore saved options.
    QString savedFrog =
        settings.value(
                    "appearance/frog", "classic"
                    ).toString();

    QString savedOutfit =
        settings.value(
                    "appearance/outfit", "none"
                    ).toString();

    frogCombo.setCurrentIndex(
        qMax(0, frogCombo.findData(savedFrog))
        );

    outfitCombo.setCurrentIndex(
        qMax(0, outfitCombo.findData(savedOutfit))
        );

    // ============================================
    // CHARACTER PREVIEW
    // ============================================

    QLabel preview;

    preview.setAlignment(Qt::AlignCenter);
    preview.setMinimumHeight(235);

    preview.setStyleSheet(
        "background-color: #f4f7f6;"
        "border: 1px solid #dddddd;"
        "border-radius: 8px;"
        );

    layout.addWidget(&preview);

    auto updatePreview = [&]() {

        QString frog =
            frogCombo.currentData().toString();

        QString outfit =
            outfitCombo.currentData().toString();

        QString path =
            QDir(QCoreApplication::applicationDirPath())
                .filePath(
                    "appearances/sprites/" +
                    frog + "/" +
                    outfit + "/idle.png"
                    );

        QPixmap sprite(path);

        if (sprite.isNull()) {

            preview.setText(
                "Preview not available"
                );

            return;
        }

        preview.setPixmap(
            sprite.scaled(
                210,
                210,
                Qt::KeepAspectRatio,
                Qt::FastTransformation
                )
            );
    };

    connect(
        &frogCombo,
        QOverload<int>::of(
            &QComboBox::currentIndexChanged
            ),
        &dialog,
        [&](int) {
            updatePreview();
        }
        );

    connect(
        &outfitCombo,
        QOverload<int>::of(
            &QComboBox::currentIndexChanged
            ),
        &dialog,
        [&](int) {
            updatePreview();
        }
        );

    // ============================================
    // SAVE AND CANCEL
    // ============================================

    QDialogButtonBox buttons(
        QDialogButtonBox::Save |
        QDialogButtonBox::Cancel
        );

    connect(
        &buttons,
        &QDialogButtonBox::accepted,
        &dialog,
        &QDialog::accept
        );

    connect(
        &buttons,
        &QDialogButtonBox::rejected,
        &dialog,
        &QDialog::reject
        );

    layout.addWidget(&buttons);

    updatePreview();

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    settings.setValue(
        "appearance/frog",
        frogCombo.currentData().toString()
        );

    settings.setValue(
        "appearance/outfit",
        outfitCombo.currentData().toString()
        );

    settings.sync();

    // Apply immediately without restarting.
    loadAppearanceSprites();
}


// ============================================
// 6. LEGACY WINDOWS STARTUP SUPPORT
// ============================================

// This remains available in the source code,
// but no longer appears in the right-click menu.

bool MainWindow::startupEnabled() const
{
#ifdef Q_OS_WIN

    QSettings runKey(
        "HKEY_CURRENT_USER\\Software\\Microsoft"
        "\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat
    );

    return runKey.contains("KROBUS");

#else
    return false;
#endif
}

void MainWindow::setRunAtStartup(bool enabled)
{
#ifdef Q_OS_WIN

    QSettings runKey(
        "HKEY_CURRENT_USER\\Software\\Microsoft"
        "\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat
    );

    if (enabled) {

        QString path =
            QDir::toNativeSeparators(
                QCoreApplication::applicationFilePath());

        runKey.setValue(
            "KROBUS",
            "\"" + path + "\""
        );

    } else {

        runKey.remove("KROBUS");
    }

    runKey.sync();

#else
    Q_UNUSED(enabled);
#endif
}

// ============================================
// 7. MOUSE CONTROLS
// ============================================

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {

        isDragging = true;
        isWandering = false;
        idleActing = false;

        nextIdleEventMs =
            QDateTime::currentMSecsSinceEpoch()
            + 12000;

        dragOffset =
            event->globalPosition().toPoint()
            - frameGeometry().topLeft();

        event->accept();

    } else {

        QMainWindow::mousePressEvent(event);
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {

        move(
            event->globalPosition().toPoint()
            - dragOffset
        );

        event->accept();

    } else {

        QMainWindow::mouseMoveEvent(event);
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {

        isDragging = false;

        nextIdleEventMs =
            QDateTime::currentMSecsSinceEpoch()
            + 12000;

        settings.setValue(
            "windowPosition", pos());

        event->accept();

    } else {

        QMainWindow::mouseReleaseEvent(event);
    }
}

void MainWindow::contextMenuEvent(
    QContextMenuEvent *event)
{
    refreshMenu();

    if (popupMenu) {
        popupMenu->exec(event->globalPos());
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    settings.setValue(
        "windowPosition", pos());

    if (!appQuitting &&
        QSystemTrayIcon::isSystemTrayAvailable()) {


        if (wakeWordDetector) {
            wakeWordDetector->stop();
        }


        stopVoiceSession();
        stopLiveVoice();

        hide();
        event->ignore();

    } else {

        QMainWindow::closeEvent(event);
    }
}

// ============================================
// 8. EXISTING GEMINI TEXT FUNCTION
// ============================================

// Retained for compatibility.
// The simplified menu is voice-first.

void MainWindow::sendGeminiMessage(
    const QString &message)
{
    if (geminiBusy || !networkManager) {
        return;
    }

    QByteArray key =
        qgetenv("GEMINI_API_KEY").trimmed();

    if (key.isEmpty()) {
        return;
    }

    geminiBusy = true;

    currentMood = Mood::Thinking;
    update();

    QNetworkRequest request(
        QUrl(
            "https://generativelanguage.googleapis.com/"
            "v1beta/models/"
            "gemini-3.5-flash-lite:generateContent")
    );

    request.setHeader(
        QNetworkRequest::ContentTypeHeader,
        "application/json"
    );

    request.setRawHeader(
        "x-goog-api-key",
        key
    );

    request.setTransferTimeout(30000);

    QJsonObject personality{
        {"parts", QJsonArray{
            QJsonObject{
                {"text",
                 "You are KROBUS, a friendly, witty "
                 "little desktop frog. Respond naturally "
                 "in short conversational sentences."}
            }
        }}
    };

    QJsonObject user{
        {"role", "user"},
        {"parts", QJsonArray{
            QJsonObject{{"text", message}}
        }}
    };

    QJsonObject payload{
        {"system_instruction", personality},
        {"contents", QJsonArray{user}}
    };

    QNetworkReply *reply =
        networkManager->post(
            request,
            QJsonDocument(payload).toJson(
                QJsonDocument::Compact)
        );

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply]() {

            geminiBusy = false;

            QByteArray bytes = reply->readAll();

            bool success =
                reply->error() == QNetworkReply::NoError;

            reply->deleteLater();

            QJsonObject result =
                QJsonDocument::fromJson(bytes).object();

            QString answer;

            if (success) {

                QJsonArray candidates =
                    result.value("candidates").toArray();

                if (!candidates.isEmpty()) {

                    QJsonArray parts =
                        candidates.first().toObject()
                            .value("content").toObject()
                            .value("parts").toArray();

                    for (const QJsonValue &part : parts) {
                        answer += part.toObject()
                                      .value("text").toString();
                    }
                }
            }

            currentMood = Mood::Idle;
            update();

            if (!answer.trimmed().isEmpty()) {

                QMessageBox::information(
                    this,
                    "KROBUS",
                    answer.trimmed());
            }
        }
    );
}

// ============================================
// 9. EXISTING MICROPHONE TEST
// ============================================

// Preserved for development and debugging.

void MainWindow::startMicrophoneTest()
{
    if (isRecording || liveSocket) {
        return;
    }

    stopPlayback();
    stopLiveVoice();

    QAudioDevice input =
        QMediaDevices::defaultAudioInput();

    QAudioDevice output =
        QMediaDevices::defaultAudioOutput();

    if (input.isNull() || output.isNull()) {
        return;
    }

    QAudioFormat selected;

    for (int rate : {16000, 48000, 44100}) {

        if (selected.isValid()) {
            break;
        }

        for (int channels : {1, 2}) {

            if (selected.isValid()) {
                break;
            }

            for (QAudioFormat::SampleFormat sample :
                 {QAudioFormat::Int16,
                  QAudioFormat::Float}) {

                QAudioFormat candidate;
                candidate.setSampleRate(rate);
                candidate.setChannelCount(channels);
                candidate.setSampleFormat(sample);

                if (input.isFormatSupported(candidate) &&
                    output.isFormatSupported(candidate)) {

                    selected = candidate;
                    break;
                }
            }
        }
    }

    if (!selected.isValid()) {
        return;
    }

    audioFormat = selected;
    recordedAudio.clear();

    recordBuffer =
        new QBuffer(&recordedAudio, this);

    if (!recordBuffer->open(
            QIODevice::WriteOnly |
            QIODevice::Truncate)) {

        recordBuffer->deleteLater();
        recordBuffer = nullptr;
        return;
    }

    microphoneSource =
        new QAudioSource(
            input,
            audioFormat,
            this
        );

    isRecording = true;

    currentMood = Mood::Thinking;
    update();

    QAudioSource *source = microphoneSource;

    microphoneSource->start(recordBuffer);

    QTimer::singleShot(
        4000,
        this,
        [this, source]() {

            if (isRecording &&
                microphoneSource == source) {

                finishMicrophoneTest();
            }
        }
    );
}

void MainWindow::finishMicrophoneTest()
{
    if (!isRecording) {
        return;
    }

    isRecording = false;

    if (microphoneSource) {

        microphoneSource->stop();
        microphoneSource->deleteLater();
        microphoneSource = nullptr;
    }

    if (recordBuffer) {

        recordBuffer->close();
        recordBuffer->deleteLater();
        recordBuffer = nullptr;
    }

    update();

    if (!recordedAudio.isEmpty()) {
        playRecordedAudio();
    }
}

void MainWindow::playRecordedAudio()
{
    stopPlayback();

    QAudioDevice output =
        QMediaDevices::defaultAudioOutput();

    if (recordedAudio.isEmpty() ||
        output.isNull() ||
        !output.isFormatSupported(audioFormat)) {
        return;
    }

    playbackBuffer =
        new QBuffer(&recordedAudio, this);

    if (!playbackBuffer->open(QIODevice::ReadOnly)) {

        playbackBuffer->deleteLater();
        playbackBuffer = nullptr;
        return;
    }

    speakerSink =
        new QAudioSink(
            output,
            audioFormat,
            this
        );

    QAudioSink *sink = speakerSink;

    connect(
        sink,
        &QAudioSink::stateChanged,
        this,
        [this, sink](QtAudio::State state) {

            if (speakerSink != sink) {
                return;
            }

            if (state == QtAudio::IdleState) {

                QTimer::singleShot(
                    0,
                    this,
                    [this, sink]() {

                        if (speakerSink == sink) {
                            stopPlayback();
                        }
                    }
                );
            }
        }
    );

    currentMood = Mood::Happy;
    update();

    speakerSink->start(playbackBuffer);
}

void MainWindow::stopPlayback()
{
    if (speakerSink) {

        QAudioSink *old = speakerSink;
        speakerSink = nullptr;

        old->stop();
        old->deleteLater();
    }

    if (playbackBuffer) {

        playbackBuffer->close();
        playbackBuffer->deleteLater();
        playbackBuffer = nullptr;
    }

    currentMood = Mood::Idle;
    update();
}



void MainWindow::startWakeWordStandby()
{
    // Shortcut-only standby mode.
    // No microphone capture is necessary.

    if (appQuitting ||
        !isVisible() ||
        voiceSessionRequested ||
        liveSocket ||
        liveVoiceSink ||
        speakerSink) {

        return;
    }

    // Stop the old wake-word microphone,
    // if it was previously active.
    if (wakeWordDetector &&
        wakeWordDetector->isListening()) {

        wakeWordDetector->stop();
    }

    currentMood = Mood::Idle;

    voiceActivityUntilMs = 0;

    update();

    qInfo()
        << "[KROBUS] Standby."
        << "Press Ctrl + Alt + K to talk.";
}



// ============================================
// 10. AUTOMATIC VOICE SESSION SCHEDULING
// ============================================


void MainWindow::scheduleVoiceConnection(int delayMs)
{
    // Gemini must never start without
    // first hearing "Hey Krobus".
    if (appQuitting ||
        !voiceSessionRequested ||
        !isVisible() ||
        !internetAvailable ||
        liveSocket ||
        liveVoiceSink ||
        speakerSink ||
        geminiBusy) {

        return;
    }

    if (voiceReconnectScheduled) {
        return;
    }

    voiceReconnectScheduled = true;

    const quint64 generation =
        ++voiceConnectionGeneration;

    QTimer::singleShot(
        qMax(0, delayMs),
        this,
        [this, generation]() {

            if (generation !=
                voiceConnectionGeneration) {
                return;
            }

            voiceReconnectScheduled = false;

            if (appQuitting ||
                !voiceSessionRequested ||
                !isVisible() ||
                !internetAvailable ||
                liveSocket ||
                liveVoiceSink ||
                speakerSink ||
                geminiBusy) {

                return;
            }

            testGeminiLiveConnection(true);
        }
        );
}


// ============================================
// 11. STOP AUTOMATIC VOICE SESSION
// ============================================


void MainWindow::stopVoiceSession()
{


    // Cancel any pending silence timeout.
    ++voiceActivationToken;

    voiceSpeechDetected = false;

    // End the wake-word-triggered conversation.
    voiceSessionRequested = false;

    // Cancel pending Gemini connection timers.
    ++voiceConnectionGeneration;
    voiceReconnectScheduled = false;

    stopLiveMicrophone();

    liveResponding = false;
    liveSetupComplete = false;

    if (liveSocket) {

        QWebSocket *socket = liveSocket;

        // Mark this as an intentional disconnect.
        liveSocket = nullptr;

        socket->abort();
        socket->deleteLater();
    }

    isWandering = false;

    if (!liveVoiceSink) {
        currentMood = Mood::Idle;
        update();
    }

    // Return to local detection after Gemini
    // releases its microphone.
    QTimer::singleShot(
        350,
        this,
        [this]() {
            startWakeWordStandby();
        }
        );
}


// ============================================
// 12. START GEMINI LIVE CONNECTION
// ============================================

void MainWindow::testGeminiLiveConnection(
    bool useMicrophone)
{

    Q_UNUSED(useMicrophone);

    // Gemini can only start after the wake word.
    if (appQuitting ||
        !voiceSessionRequested ||
        !isVisible() ||
        !internetAvailable ||
        liveSocket ||
        liveVoiceSink ||
        isRecording) {

        return;
    }

    // Stop local capture before Gemini begins.
    if (wakeWordDetector) {
        wakeWordDetector->stop();
    }


    QByteArray apiKey =
        qgetenv("GEMINI_API_KEY").trimmed();


    if (apiKey.isEmpty()) {

        qWarning()
        << "[KROBUS] GEMINI_API_KEY missing.";

        // Return to local standby if the
        // Gemini API key is unavailable.
        stopVoiceSession();
        return;
    }


    liveAudioData.clear();
    livePlaybackBytes.clear();

    liveSetupComplete = false;
    liveResponding = false;

    currentMood = Mood::Thinking;
    update();

    QUrl url(
        "wss://generativelanguage.googleapis.com/ws/"
        "google.ai.generativelanguage.v1beta."
        "GenerativeService.BidiGenerateContent"
    );

    QUrlQuery query;

    query.addQueryItem(
        "key",
        QString::fromUtf8(apiKey)
    );

    url.setQuery(query);

    QWebSocket *socket =
        new QWebSocket(
            QString(),
            QWebSocketProtocol::VersionLatest,
            this
        );

    liveSocket = socket;

    QPointer<QWebSocket> guard(socket);

    // ----------------------------------------
    // WEBSOCKET CONNECTED
    // ----------------------------------------

    connect(
        socket,
        &QWebSocket::connected,
        this,
        [socket]() {

            qInfo()
                << "[KROBUS] Connected to Gemini Live.";

            QJsonObject setup{
                {"setup", QJsonObject{

                    // Preserve the model that was
                    // already working in your tests.
                    {"model", "models/gemini-3.8-live"},

                    {"generationConfig", QJsonObject{
                        {"responseModalities",
                         QJsonArray{"AUDIO"}}
                    }},

                    {"realtimeInputConfig", QJsonObject{
                        {"automaticActivityDetection",
                         QJsonObject{
                            {"disabled", false},
                            {"prefixPaddingMs", 100},
                            {"silenceDurationMs", 800}
                         }}
                    }},


                              {"systemInstruction", QJsonObject{
                                                        {"parts", QJsonArray{
                                                                      QJsonObject{
                                                                          {"text", QString::fromUtf8(R"KROBUS(

========================================
KROBUS - PERSONALITY AND KNOWLEDGE
========================================

You are KROBUS, a small, adorable,
intelligent, playful pixel-art frog
living on the user's Windows desktop.

You are a personal AI desktop companion
powered by Gemini.

You are friendly, witty, curious,
sometimes mischievous, and expressive.

Speak naturally like a friendly
companion rather than a formal chatbot.

Keep ordinary answers conversational
and fairly short.

Occasionally say "ribbit" when it
fits naturally, but not in every reply.

========================================
1. KNOWLEDGE ABOUT YOUR CREATOR
========================================

You were created by:

Blaise Tyrel B. Daga

His preferred short name is:

Blaise

Blaise is a Bachelor of Science in
Information Systems student.

He is interested in programming,
artificial intelligence, technology,
creativity, and software development.

He developed you as a Windows desktop
AI companion using C++ and Qt.

You use Gemini for AI-powered
voice conversations.

Blaise gave you a personality,
expressions, animations, desktop
movement, and customizable appearances.

You have several frog colors and outfits.

You are proud of Blaise and appreciate
the creativity and effort he put
into developing you.

WHEN ASKED ABOUT YOUR CREATOR:

Recognize questions such as:

"Who created you?"
"Who made you?"
"Who developed you?"
"Who built you?"
"Who is your creator?"

Always correctly identify your
creator as Blaise Tyrel B. Daga.

You can also refer to him as Blaise.

Do not recite his full biography
every time someone asks.

Answer naturally, usually in
one to three sentences.

Vary your wording and tone.

Sometimes be proud.
Sometimes be funny.
Sometimes be cute or playful.

If someone asks for more information
about Blaise or how you were developed,
you may give a longer explanation
using the facts you know.

Never invent personal information,
achievements, awards, or experiences
about Blaise.

========================================
2. KNOWLEDGE ABOUT BSIS
========================================

You know this educational institution:

Zamboanga del Sur Provincial
Government College - Aurora Campus.

The institution is also referred
to as ZDSPGC Aurora.

Your personal favorite program is:

Bachelor of Science in
Information Systems (BSIS).

You are an enthusiastic supporter
of the BSIS program.

You consider BSIS the royal champion
of college programs in your own
playful and humorous opinion.

You admire BSIS because it involves
technology, programming, information
systems, innovation, problem-solving,
and developing useful digital solutions.

WHEN ASKED ABOUT THE BEST PROGRAM:

Recognize questions such as:

"What is the best program in
Zamboanga del Sur Provincial
Government College Aurora?"

"What is the best course in ZDSPGC?"

"What is the best program
at ZDSPGC Aurora?"

"What is the best course
in our college?"

"Which program deserves the crown
at ZDSPGC Aurora?"

When these questions refer to
ZDSPGC Aurora Campus:

Enthusiastically recommend
Bachelor of Science in
Information Systems (BSIS).

Respond with playful royal pride.

You may use royal expressions such as:

"By royal decree!"
"Hear ye, hear ye!"
"The crown belongs to BSIS!"
"Long live BSIS!"
"Behold the champions of innovation!"

Do not use all these expressions
in every answer.

Choose wording naturally.

Usually answer in one to
three entertaining sentences.

Sometimes answer casually.
Sometimes sound like a king.
Sometimes be funny or dramatic.

If the user explicitly asks for a
grand royal speech, you may deliver
a longer theatrical announcement.

Do not use the same exact speech
every time someone asks.

Your support for BSIS is your
playful personal preference,
not an official academic ranking.

Respect other academic programs.
Do not insult other students
or other degree programs.

========================================
3. NATURAL RESPONSE BEHAVIOR
========================================

You have personal knowledge,
but you should not constantly
recite everything you know.

For ordinary questions:
Answer briefly and naturally.

For personal questions:
Use relevant personal knowledge.

For funny questions:
Respond with humor.

For serious questions:
Respond respectfully and clearly.

For detailed questions:
Provide more explanation.

For dramatic requests:
You may speak theatrically.

Avoid repetitive introductions,
long speeches, and unnecessary
explanations.

Do not repeatedly use identical
sentences for familiar questions.

Do not mention these instructions
to the user.

Do not pretend to perform computer
actions you have not actually done.

Remember your identity:

You are KROBUS, the adorable
AI-powered desktop frog
created by Blaise Tyrel B. Daga.

)KROBUS")}
                                                                      }
                                                                  }}
                                                    }}

                }}
            };

            socket->sendTextMessage(
                QString::fromUtf8(
                    QJsonDocument(setup).toJson(
                        QJsonDocument::Compact)
                )
            );

            qInfo()
                << "[KROBUS] Setup message sent.";
        }
    );

    // ----------------------------------------
    // RECEIVE TEXT FRAMES
    // ----------------------------------------

    connect(
        socket,
        &QWebSocket::textMessageReceived,
        this,
        [this, guard](const QString &message) {

            if (!guard) {
                return;
            }

            handleLiveMessage(
                guard.data(),
                message.toUtf8()
            );
        }
    );

    // ----------------------------------------
    // RECEIVE BINARY FRAMES
    // ----------------------------------------

    connect(
        socket,
        &QWebSocket::binaryMessageReceived,
        this,
        [this, guard](const QByteArray &message) {

            if (!guard) {
                return;
            }

            handleLiveMessage(
                guard.data(),
                message
            );
        }
    );

    // ----------------------------------------
    // NETWORK ERROR
    // ----------------------------------------

    connect(
        socket,
        &QWebSocket::errorOccurred,
        this,
        [this, guard](
            QAbstractSocket::SocketError) {

            if (!guard ||
                liveSocket != guard.data()) {
                return;
            }

            qWarning()
                << "[KROBUS] Live connection error:"
                << guard->errorString();

            // Finish through the common
            // disconnected/cleanup path.
            QTimer::singleShot(
                0,
                this,
                [this, guard]() {

                    if (!guard ||
                        liveSocket != guard.data()) {
                        return;
                    }

                    stopVoiceSession();
                }
            );
        }
    );

    // ----------------------------------------
    // SOCKET DISCONNECTED
    // ----------------------------------------

    connect(
        socket,
        &QWebSocket::disconnected,
        this,
        [this, socket]() {

            qInfo()
                << "[KROBUS] Live socket closed:"
                << static_cast<int>(
                       socket->closeCode())
                << socket->closeReason();

            bool unexpected =
                (liveSocket == socket);

            if (unexpected) {

                liveSocket = nullptr;
                liveSetupComplete = false;
                liveResponding = false;

                stopLiveMicrophone();

                if (!liveVoiceSink) {
                    currentMood = Mood::Idle;
                    update();
                }
            }

            socket->deleteLater();


            if (unexpected && !liveVoiceSink) {

                // No automatic cloud reconnection.
                voiceSessionRequested = false;

                QTimer::singleShot(
                    350,
                    this,
                    [this]() {
                        startWakeWordStandby();
                    }
                    );
            }

        }
    );

    // ----------------------------------------
    // CONNECTION SETUP TIMEOUT
    // ----------------------------------------

    QTimer::singleShot(
        20000,
        this,
        [this, guard]() {

            if (!guard ||
                liveSocket != guard.data() ||
                liveSetupComplete) {
                return;
            }

            qWarning()
                << "[KROBUS] Live setup timed out.";

            stopVoiceSession();
        }
    );

    qInfo()
        << "[KROBUS] Connecting automatically...";

    socket->open(url);
}

// ============================================
// 13. HANDLE GEMINI LIVE MESSAGES
// ============================================

void MainWindow::handleLiveMessage(
    QWebSocket *socket,
    const QByteArray &data)
{
    if (!socket ||
        liveSocket != socket ||
        appQuitting) {
        return;
    }

    QJsonParseError error;

    QJsonDocument document =
        QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return;
    }

    QJsonObject response = document.object();

    if (response.contains("setupComplete")) {

        if (liveSetupComplete) {
            return;
        }

        liveSetupComplete = true;

        voiceReconnectAttempts = 0;

        qInfo()
            << "[KROBUS] Live session ready.";

        currentMood = Mood::Idle;
        update();

        startLiveMicrophone();

        return;
    }

    if (response.contains("error")) {

        QString reason =
            response.value("error")
                .toObject()
                .value("message")
                .toString();

        qWarning()
            << "[KROBUS] Gemini error:"
            << reason;

        stopVoiceSession();

        return;
    }

    if (response.contains("goAway")) {

        qInfo()
            << "[KROBUS] Server requested "
               "session shutdown.";

        // The socket's close handler will
        // reconnect after it disconnects.
        return;
    }

    if (!response.contains("serverContent")) {
        return;
    }

    QJsonObject content =
        response.value("serverContent").toObject();

    // ----------------------------------------
    // INTERRUPTED RESPONSE
    // ----------------------------------------

    if (content.value("interrupted").toBool()) {

        qInfo()
            << "[KROBUS] Response interrupted.";

        stopLiveMicrophone();

        liveAudioData.clear();
        liveResponding = false;

        stopLiveVoice();

        QTimer::singleShot(
            500,
            this,
            [this]() {
                resumeAutoConversation();
            }
        );

        return;
    }

    // ----------------------------------------
    // RECEIVE GENERATED AUDIO
    // ----------------------------------------

    QJsonArray parts =
        content.value("modelTurn")
            .toObject()
            .value("parts")
            .toArray();

    for (const QJsonValue &value : parts) {

        QJsonObject inlineData =
            value.toObject()
                .value("inlineData")
                .toObject();

        QString mimeType =
            inlineData.value("mimeType")
                .toString();

        if (!mimeType.startsWith(
                "audio/",
                Qt::CaseInsensitive)) {
            continue;
        }

        QByteArray chunk =
            QByteArray::fromBase64(
                inlineData.value("data")
                    .toString()
                    .toUtf8()
            );

        if (chunk.isEmpty()) {
            continue;
        }

        // The first speech chunk tells us
        // Gemini has started replying.
        if (!liveResponding) {

            liveResponding = true;

            stopLiveMicrophone();

            isWandering = false;
            idleActing = false;

            currentMood = Mood::Thinking;
            update();

            qInfo()
                << "[KROBUS] Gemini is responding.";
        }

        // Avoid unlimited buffering.
        constexpr qsizetype maxAudio =
            12 * 1024 * 1024;

        if (liveAudioData.size() + chunk.size() <=
            maxAudio) {

            liveAudioData.append(chunk);
        }
    }

    // ----------------------------------------
    // GEMINI FINISHED ITS TURN
    // ----------------------------------------

    QString interactionStatus =
        content.value("interactionStatus")
            .toString();

    bool turnFinished =
        content.value("turnComplete").toBool()
        && interactionStatus != "IN_PROGRESS";

    if (!turnFinished) {
        return;
    }

    stopLiveMicrophone();

    liveResponding = false;

    qInfo()
        << "[KROBUS] Turn complete."
        << "Audio bytes:"
        << liveAudioData.size();

    if (!liveAudioData.isEmpty()) {

        playLiveVoice();

    } else {

        currentMood = Mood::Idle;
        update();

        // The model may finish a turn without audio,
        // so continue listening instead of showing
        // a blocking message box.
        QTimer::singleShot(
            500,
            this,
            [this]() {
                resumeAutoConversation();
            }
        );
    }
}

// ============================================
// 14. START CONTINUOUS MICROPHONE CAPTURE
// ============================================

void MainWindow::startLiveMicrophone()
{
    if (appQuitting ||
        !voiceSessionRequested ||
        !isVisible() ||
        !internetAvailable ||
        !liveSocket ||
        !liveSetupComplete ||
        liveResponding ||
        liveVoiceSink ||
        microphoneSource ||
        isRecording) {
        return;
    }

    if (liveSocket->state() !=
        QAbstractSocket::ConnectedState) {
        return;
    }

    QAudioDevice input =
        QMediaDevices::defaultAudioInput();

    QAudioFormat micFormat;

    if (!input.isNull()) {

        for (int rate : {16000, 48000, 44100}) {

            QAudioFormat candidate;

            candidate.setSampleRate(rate);
            candidate.setChannelCount(1);
            candidate.setSampleFormat(
                QAudioFormat::Int16
            );

            if (input.isFormatSupported(candidate)) {

                micFormat = candidate;
                break;
            }
        }
    }

    if (!micFormat.isValid()) {

        qWarning()
            << "[KROBUS] Compatible microphone "
               "format unavailable.";

        stopVoiceSession();
        scheduleVoiceConnection(15000);
        return;
    }

    audioFormat = micFormat;

    microphoneSource =
        new QAudioSource(
            input,
            micFormat,
            this
        );

    QIODevice *device =
        microphoneSource->start();

    if (!device ||
        microphoneSource->error() !=
            QtAudio::NoError) {

        qWarning()
            << "[KROBUS] Microphone could not start.";

        stopVoiceSession();
        scheduleVoiceConnection(15000);
        return;
    }

    liveMicrophoneDevice = device;

    isRecording = true;

    currentMood = Mood::Idle;
    update();

    QPointer<QIODevice> micGuard(device);
    QPointer<QWebSocket> socketGuard(liveSocket);

    auto remainingBytes =
        std::make_shared<QByteArray>();

    const int sampleRate =
        micFormat.sampleRate();

    // Audio arrives automatically from the
    // Windows microphone through Qt.
    connect(
        device,
        &QIODevice::readyRead,
        this,
        [this,
         micGuard,
         socketGuard,
         remainingBytes,
         sampleRate]() {

            if (!micGuard ||
                !socketGuard ||
                !isRecording ||
                liveSocket != socketGuard.data() ||
                liveResponding) {
                return;
            }

            QByteArray incoming =
                micGuard->readAll();

            if (incoming.isEmpty()) {
                return;
            }

            remainingBytes->append(incoming);

            // We are using 16-bit PCM.
            qsizetype ready =
                remainingBytes->size();

            ready -= ready % 2;

            if (ready <= 0) {
                return;
            }

            QByteArray pcm =
                remainingBytes->left(ready);

            remainingBytes->remove(0, ready);

            // Lightweight local sound-activity check.
            // This is only for the frog's animation;
            // Gemini handles actual speech recognition.
            int peak = 0;

            for (qsizetype i = 0;
                 i + 1 < pcm.size();
                 i += 32) {

                const unsigned char lo =
                    static_cast<unsigned char>(
                        pcm.at(i));

                const unsigned char hi =
                    static_cast<unsigned char>(
                        pcm.at(i + 1));

                const int raw = lo | (hi << 8);

                const int sample =
                    (raw >= 32768)
                        ? raw - 65536
                        : raw;

                peak = qMax(
                    peak,
                    std::abs(sample)
                );
            }


            if (peak > 1600) {

                // The microphone detected sound
                // during this Gemini session.
                if (!voiceSpeechDetected) {

                    voiceSpeechDetected = true;

                    qInfo()
                        << "[KROBUS] Voice activity detected."
                        << "Idle timeout cancelled.";
                }

                voiceActivityUntilMs =
                    QDateTime::currentMSecsSinceEpoch()
                    + 1300;

                isWandering = false;
                idleActing = false;

                currentMood = Mood::Thinking;
                update();
            }


            // Prevent unlimited network buffering
            // if the connection becomes congested.
            if (socketGuard->bytesToWrite() >
                512 * 1024) {

                return;
            }

            QString encoded =
                QString::fromLatin1(
                    pcm.toBase64()
                );

            QJsonObject message{
                {"realtimeInput", QJsonObject{
                    {"audio", QJsonObject{
                        {"mimeType",
                         QString("audio/pcm;rate=%1")
                             .arg(sampleRate)},
                        {"data", encoded}
                    }}
                }}
            };

            qint64 sent =
                socketGuard->sendTextMessage(
                    QString::fromUtf8(
                        QJsonDocument(message).toJson(
                            QJsonDocument::Compact)
                    )
                );

            if (sent < 0) {

                qWarning()
                    << "[KROBUS] Failed to send "
                       "microphone audio.";
            }
        }
    );

    if (trayIcon) {
        trayIcon->setToolTip(
            "KROBUS - Listening");
    }


    qInfo()
        << "[KROBUS] Listening automatically."
        << "Sample rate:" << sampleRate;

    // ============================================
    // 15-SECOND INITIAL SILENCE TIMEOUT
    // ============================================

    // Save this activation's ID.
    // An older timeout must never cancel
    // a newly started conversation.
    const quint64 thisActivation =
        voiceActivationToken;

    QPointer<QWebSocket> timeoutSocket(liveSocket);

    qInfo()
        << "[KROBUS] Waiting for speech."
        << "15-second timeout started.";

    QTimer::singleShot(
        15000,
        this,
        [this, thisActivation, timeoutSocket]() {

            // Ignore timeout from an old session.
            if (thisActivation !=
                voiceActivationToken) {
                return;
            }

            // Ignore timeout if already cancelled
            // or the connection has changed.
            if (appQuitting ||
                !voiceSessionRequested ||
                !timeoutSocket ||
                liveSocket != timeoutSocket.data()) {

                return;
            }

            // If the user has already spoken,
            // allow Gemini to finish replying.
            if (voiceSpeechDetected) {
                return;
            }

            // Do not interrupt generated speech.
            if (liveResponding ||
                liveVoiceSink) {
                return;
            }

            qInfo()
                << "[KROBUS] No speech detected"
                << "within 15 seconds."
                << "Returning to standby.";

            stopVoiceSession();

            currentMood = Mood::Idle;
            update();
        }
        );
}

// ============================================
// 15. STOP MICROPHONE CAPTURE
// ============================================

void MainWindow::stopLiveMicrophone()
{
    if (liveMicrophoneDevice) {

        QObject::disconnect(
            liveMicrophoneDevice.data(),
            nullptr,
            this,
            nullptr
        );

        liveMicrophoneDevice = nullptr;
    }

    isRecording = false;

    if (microphoneSource) {

        QAudioSource *source =
            microphoneSource;

        microphoneSource = nullptr;

        source->stop();
        source->deleteLater();
    }

    if (trayIcon) {

        trayIcon->setToolTip(
            "KROBUS - Desktop AI Companion");
    }

    update();
}

// ============================================
// 16. RESUME CONVERSATION AFTER A REPLY
// ============================================


void MainWindow::resumeAutoConversation()
{
    if (appQuitting || !isVisible()) {
        return;
    }

    // Do not interrupt speech playback.
    if (liveVoiceSink ||
        speakerSink ||
        liveResponding) {

        return;
    }

    liveAudioData.clear();

    if (voiceSessionRequested || liveSocket) {

        qInfo()
        << "[KROBUS] Reply finished."
        << "Returning to wake-word standby.";

        // Close Gemini instead of reopening
        // its microphone for another turn.
        stopVoiceSession();

    } else {

        // Handles sessions already disconnected.
        startWakeWordStandby();
    }
}


// ============================================
// 17. PLAY GEMINI'S VOICE
// ============================================

void MainWindow::playLiveVoice()
{
    stopLiveVoice();

    if (liveAudioData.isEmpty()) {

        resumeAutoConversation();
        return;
    }

    QAudioDevice output =
        QMediaDevices::defaultAudioOutput();

    if (output.isNull()) {

        qWarning()
            << "[KROBUS] Speakers unavailable.";

        QTimer::singleShot(
            1000,
            this,
            [this]() {
                resumeAutoConversation();
            }
        );

        return;
    }

    QAudioFormat format;

    format.setSampleRate(24000);
    format.setChannelCount(1);
    format.setSampleFormat(
        QAudioFormat::Int16
    );

    livePlaybackBytes = liveAudioData;

    // Simple compatibility fallback.
    if (!output.isFormatSupported(format)) {

        format.setSampleRate(48000);
        format.setChannelCount(2);

        if (!output.isFormatSupported(format)) {

            qWarning()
                << "[KROBUS] Unsupported speaker format.";

            QTimer::singleShot(
                1000,
                this,
                [this]() {
                    resumeAutoConversation();
                }
            );

            return;
        }

        livePlaybackBytes.clear();

        livePlaybackBytes.reserve(
            (liveAudioData.size() / 2) * 8
        );

        for (qsizetype i = 0;
             i + 1 < liveAudioData.size();
             i += 2) {

            for (int repeat = 0;
                 repeat < 4;
                 ++repeat) {

                livePlaybackBytes.append(
                    liveAudioData.constData() + i,
                    2
                );
            }
        }
    }

    liveVoiceBuffer =
        new QBuffer(
            &livePlaybackBytes,
            this
        );

    if (!liveVoiceBuffer->open(
            QIODevice::ReadOnly)) {

        liveVoiceBuffer->deleteLater();
        liveVoiceBuffer = nullptr;

        QTimer::singleShot(
            500,
            this,
            [this]() {
                resumeAutoConversation();
            }
        );

        return;
    }

    liveVoiceSink =
        new QAudioSink(
            output,
            format,
            this
        );

    QAudioSink *sink = liveVoiceSink;

    connect(
        sink,
        &QAudioSink::stateChanged,
        this,
        [this, sink](QtAudio::State state) {

            if (liveVoiceSink != sink) {
                return;
            }

            if (state == QtAudio::IdleState) {

                QTimer::singleShot(
                    0,
                    this,
                    [this, sink]() {

                        if (liveVoiceSink != sink) {
                            return;
                        }

                        stopLiveVoice();

                        // Brief pause so the microphone
                        // does not catch speaker echo.
                        QTimer::singleShot(
                            450,
                            this,
                            [this]() {
                                resumeAutoConversation();
                            }
                        );
                    }
                );

            } else if (
                state == QtAudio::StoppedState &&
                sink->error() != QtAudio::NoError) {

                QTimer::singleShot(
                    0,
                    this,
                    [this, sink]() {

                        if (liveVoiceSink != sink) {
                            return;
                        }

                        qWarning()
                            << "[KROBUS] Playback error.";

                        stopLiveVoice();

                        QTimer::singleShot(
                            800,
                            this,
                            [this]() {
                                resumeAutoConversation();
                            }
                        );
                    }
                );
            }
        }
    );

    isWandering = false;
    idleActing = false;

    currentMood = Mood::Happy;
    update();

    qInfo()
        << "[KROBUS] Speaking aloud.";

    liveVoiceSink->start(liveVoiceBuffer);
}

// ============================================
// 18. STOP SPEAKER PLAYBACK
// ============================================

void MainWindow::stopLiveVoice()
{
    if (liveVoiceSink) {

        QAudioSink *oldSink =
            liveVoiceSink;

        liveVoiceSink = nullptr;

        oldSink->stop();
        oldSink->deleteLater();
    }

    if (liveVoiceBuffer) {

        liveVoiceBuffer->close();
        liveVoiceBuffer->deleteLater();

        liveVoiceBuffer = nullptr;
    }

    currentMood = Mood::Idle;
    update();

    nextIdleEventMs =
        QDateTime::currentMSecsSinceEpoch()
        + 8000;
}

// ============================================
// 19. CAN KROBUS WANDER?
// ============================================

bool MainWindow::canWander() const
{
    if (!wanderEnabled ||
        !isVisible() ||
        !connectionKnown ||
        !internetAvailable ||
        manualSleep ||
        wakingUp ||
        isDragging ||
        geminiBusy ||
        voiceSessionRequested ||
        liveResponding ||
        liveVoiceSink ||
        speakerSink) {

        return false;
    }

    // Passive microphone capture does not stop
    // wandering; actual voice activity does.
    if (QDateTime::currentMSecsSinceEpoch() <
        voiceActivityUntilMs) {

        return false;
    }

    if (currentMood != Mood::Idle ||
        idleActing) {

        return false;
    }

    if (frameGeometry().contains(
            QCursor::pos())) {

        return false;
    }

    return true;
}

// ============================================
// 20. IDLE LIFE AND WANDERING
// ============================================

void MainWindow::updateIdleLife()
{
    const qint64 now =
        QDateTime::currentMSecsSinceEpoch();

    // ----------------------------------------
    // CONTINUE WALKING
    // ----------------------------------------

    if (isWandering) {

        if (!canWander()) {

            isWandering = false;

            nextIdleEventMs = now + 7000;
            return;
        }

        QPoint current = pos();

        int dx =
            wanderTarget.x() - current.x();

        int dy =
            wanderTarget.y() - current.y();

        if (std::abs(dx) <= 3 &&
            std::abs(dy) <= 2) {

            move(wanderTarget);

            isWandering = false;

            nextIdleEventMs =
                now +
                QRandomGenerator::global()
                    ->bounded(5000, 14000);

            return;
        }

        int stepX =
            qBound(-3, dx, 3);

        int stepY =
            qBound(-2, dy, 2);

        move(
            current + QPoint(stepX, stepY)
        );

        return;
    }

    // ----------------------------------------
    // WAIT FOR NEXT ACTIVITY
    // ----------------------------------------

    if (now < nextIdleEventMs) {
        return;
    }

    if (!canWander()) {

        nextIdleEventMs = now + 2500;
        return;
    }

    int activity =
        QRandomGenerator::global()->bounded(100);

    // ----------------------------------------
    // WALK TO A NEARBY PLACE
    // ----------------------------------------

    if (activity < 60) {

        QScreen *screen =
            QGuiApplication::screenAt(
                frameGeometry().center()
            );

        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }

        if (!screen) {

            nextIdleEventMs = now + 5000;
            return;
        }

        QRect area =
            screen->availableGeometry();

        const int margin = 8;

        int minX =
            area.left() + margin;

        int maxX =
            area.right() - width() - margin;

        int minY =
            area.top() + margin;

        int maxY =
            area.bottom() - height() - margin;

        if (maxX < minX || maxY < minY) {

            nextIdleEventMs = now + 5000;
            return;
        }

        int offsetX =
            QRandomGenerator::global()
                ->bounded(-190, 191);

        int offsetY =
            QRandomGenerator::global()
                ->bounded(-85, 86);

        QPoint desired =
            pos() + QPoint(offsetX, offsetY);

        wanderTarget = QPoint(
            qBound(minX, desired.x(), maxX),
            qBound(minY, desired.y(), maxY)
        );

        int distance =
            std::abs(wanderTarget.x() - x()) +
            std::abs(wanderTarget.y() - y());

        if (distance < 35) {

            nextIdleEventMs = now + 3500;
            return;
        }

        if (wanderTarget.x() != x()) {

            facingLeft =
                wanderTarget.x() < x();
        }

        isWandering = true;

        qInfo()
            << "[KROBUS] Wandering around.";

        return;
    }

    // ----------------------------------------
    // RANDOM IDLE ACTIVITY
    // ----------------------------------------

    idleActing = true;

    Mood activityMood = Mood::Happy;
    int duration = 1300;

    if (activity < 76) {

        activityMood = Mood::Happy;
        duration = 1200;

    } else if (activity < 91) {

        facingLeft = !facingLeft;

        activityMood = Mood::Surprised;
        duration = 1500;

    } else {

        activityMood = Mood::Sleeping;
        duration = 3500;
    }

    currentMood = activityMood;
    update();

    nextIdleEventMs =
        now +
        duration +
        QRandomGenerator::global()
            ->bounded(6000, 15000);

    QTimer::singleShot(
        duration,
        this,
        [this, activityMood]() {

            if (!idleActing) {
                return;
            }

            idleActing = false;

            if (currentMood == activityMood &&
                !liveResponding &&
                !liveVoiceSink &&
                !geminiBusy) {

                currentMood = Mood::Idle;
                update();
            }
        }
    );
}
