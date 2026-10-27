
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPoint>
#include <QPixmap>
#include <QSettings>
#include <QString>
#include <QByteArray>
#include <QAudioFormat>
#include <QPointer>
#include <QtGlobal>

// ===================================
// FORWARD DECLARATIONS
// ===================================

class QPaintEvent;
class QMouseEvent;
class QContextMenuEvent;
class QCloseEvent;

class QSystemTrayIcon;
class QNetworkAccessManager;
class QMenu;
class QAction;

class QAudioSource;
class QAudioSink;
class QBuffer;
class QIODevice;

class QWebSocket;

// Offline wake-word detector.
class WakeWordDetector;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// ===================================
// KROBUS MAIN WINDOW
// ===================================

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // KROBUS always starts visible.
    bool startHidden() const;

protected:

    // ===================================
    // WINDOW EVENTS
    // ===================================

    void paintEvent(
        QPaintEvent *event
        ) override;

    void mousePressEvent(
        QMouseEvent *event
        ) override;

    void mouseMoveEvent(
        QMouseEvent *event
        ) override;

    void mouseReleaseEvent(
        QMouseEvent *event
        ) override;

    void contextMenuEvent(
        QContextMenuEvent *event
        ) override;

    void closeEvent(
        QCloseEvent *event
        ) override;


    // Handle Windows global keyboard shortcuts.
    bool nativeEvent(
        const QByteArray &eventType,
        void *message,
        qintptr *result
        ) override;


private:

    // ===================================
    // 1. KROBUS MOODS
    // ===================================

    enum class Mood {
        Idle,
        Happy,
        Thinking,
        Sad,
        Surprised,
        Sleeping
    };

    Ui::MainWindow *ui = nullptr;

    QPoint dragOffset;

    Mood currentMood = Mood::Idle;

    int animationTick = 0;

    // ===================================
    // 2. INTERNET CONNECTION
    // ===================================

    bool internetAvailable = false;
    bool connectionKnown = false;
    bool connectionCheckRunning = false;

    bool manualSleep = false;
    bool wakingUp = false;

    QNetworkAccessManager *networkManager = nullptr;

    void checkInternet();

    Mood displayedMood() const;

    // ===================================
    // 3. APPLICATION STATE
    // ===================================

    // Prevent connections during shutdown.
    bool appQuitting = false;


    // Ctrl + Alt + K global voice shortcut.
    bool globalHotkeyRegistered = false;


    // ===================================
    // 4. SYSTEM TRAY
    // ===================================

    QSystemTrayIcon *trayIcon = nullptr;

    QMenu *popupMenu = nullptr;

    // Only Show/Hide and Quit are displayed.
    QAction *showHideAction = nullptr;

    QSettings settings{
        "KROBUS",
        "KROBUS"
    };

    void setupTray();

    void refreshMenu();


    // Customize KROBUS's frog and outfit.
    void loadAppearanceSprites();

    // Open the appearance selection window.
    void showAppearanceDialog();


    void toggleVisibility();

    // Legacy Windows startup functionality.
    // Not displayed in the simplified menu.
    bool startupEnabled() const;

    void setRunAtStartup(bool enabled);

    // ===================================
    // 5. PIXEL-ART SPRITES
    // ===================================

    QPixmap idleSprite;

    QPixmap happySprite;

    QPixmap thinkingSprite;

    QPixmap sadSprite;

    QPixmap surprisedSprite;

    QPixmap sleepingSprite;

    // ===================================
    // 6. KROBUS IDLE LIFE SYSTEM
    // ===================================

    // KROBUS can explore while idle.
    bool wanderEnabled = true;

    // User interaction state.
    bool isDragging = false;

    // Movement state.
    bool isWandering = false;

    // Random animation state.
    bool idleActing = false;

    // Facing direction.
    bool facingLeft = false;

    // Where KROBUS is walking.
    QPoint wanderTarget;

    // When KROBUS should perform
    // his next idle activity.
    qint64 nextIdleEventMs = 0;

    bool canWander() const;

    void updateIdleLife();

    // ===================================
    // 7. GEMINI TEXT AI
    // ===================================

    // Kept for compatibility and debugging.
    bool geminiBusy = false;

    void sendGeminiMessage(
        const QString &message
        );

    // ===================================
    // 8. MICROPHONE AND AUDIO
    // ===================================

    QAudioSource *microphoneSource = nullptr;

    QAudioSink *speakerSink = nullptr;

    QBuffer *recordBuffer = nullptr;

    QBuffer *playbackBuffer = nullptr;

    QByteArray recordedAudio;

    QAudioFormat audioFormat;

    // True only while Gemini's microphone
    // or the microphone test is capturing.
    //
    // Local wake-word capture is managed
    // separately by WakeWordDetector.
    bool isRecording = false;

    // Development microphone test.
    void startMicrophoneTest();

    void finishMicrophoneTest();

    void playRecordedAudio();

    void stopPlayback();

    // ===================================
    // 9. OFFLINE WAKE-WORD DETECTOR
    // ===================================

    // Sherpa-ONNX detector for "Hey Krobus".
    //
    // Uses local microphone processing.
    // Does not send standby audio to Gemini.
    WakeWordDetector *wakeWordDetector = nullptr;

    // True only when "Hey Krobus" has
    // requested an active Gemini session.
    //
    // Prevents automatic Gemini connections
    // while KROBUS is in standby.
    bool voiceSessionRequested = false;


    // Tracks whether the user has spoken
    // during the current Gemini session.
    bool voiceSpeechDetected = false;

    // Identifies the current activation.
    // Prevents an old timeout from
    // cancelling a newer conversation.
    quint64 voiceActivationToken = 0;


    // Starts local wake-word listening.
    //
    // KROBUS can remain in his idle posture,
    // wander and perform random animations.
    void startWakeWordStandby();

    // ===================================
    // 10. GEMINI LIVE CONNECTION
    // ===================================

    QWebSocket *liveSocket = nullptr;

    // True when Gemini accepts the setup.
    bool liveSetupComplete = false;

    QString liveLastError;

    // True while Gemini generates speech.
    bool liveResponding = false;

    // ===================================
    // 11. WAKE-ACTIVATED CONVERSATION
    // ===================================

    // Prevent duplicate connection timers.
    bool voiceReconnectScheduled = false;

    // Count consecutive reconnect attempts.
    int voiceReconnectAttempts = 0;

    // Used to cancel outdated reconnects.
    quint64 voiceConnectionGeneration = 0;

    // Used to animate reactions to speech.
    qint64 voiceActivityUntilMs = 0;

    // Schedule Gemini connection only when
    // voiceSessionRequested is true.
    void scheduleVoiceConnection(
        int delayMs = 0
        );

    // Close Gemini and release its microphone.
    // The implementation returns to local
    // wake-word standby when appropriate.
    void stopVoiceSession();

    // Open a Gemini Live WebSocket.
    void testGeminiLiveConnection(
        bool useMicrophone = true
        );

    // Process Gemini's incoming messages.
    void handleLiveMessage(
        QWebSocket *socket,
        const QByteArray &data
        );

    // ===================================
    // 12. GEMINI LIVE MICROPHONE
    // ===================================

    // Safely tracks the active Qt audio device.
    QPointer<QIODevice> liveMicrophoneDevice;

    // Starts the cloud microphone after
    // the wake word activates Gemini.
    void startLiveMicrophone();

    // Stops capturing cloud microphone audio.
    void stopLiveMicrophone();

    // Ends the active conversation and
    // returns to wake-word standby.
    void resumeAutoConversation();

    // ===================================
    // 13. GEMINI-GENERATED SPEECH
    // ===================================

    QByteArray liveAudioData;

    QByteArray livePlaybackBytes;

    QAudioSink *liveVoiceSink = nullptr;

    QBuffer *liveVoiceBuffer = nullptr;

    // Play Gemini's generated voice.
    void playLiveVoice();

    // Stop playback and release resources.
    void stopLiveVoice();
};

#endif // MAINWINDOW_H
