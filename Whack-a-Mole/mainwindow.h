#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QGridLayout>
#include <QList>
#include <QThread>
#include <QObject>
#include <QFile>

/** 
* custom QObject for managing mole visibility timers in a separate thread
* Runs a timer with a specified interval and emits a timeout signal.
*/
class MoleTimer : public QObject {
    Q_OBJECT
public:
    explicit MoleTimer(QObject *parent = nullptr) : QObject(parent) {}
    void runTimer(int interval) {    // Run a sleep timer for specificed interval
        QThread::sleep(interval);
        emit timeout();
    }
signals:
    void timeout();    // Signal emitted when timer completes
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);    // Constructor
    virtual ~MainWindow();    // Destructor

signals:
    void startMoleTimer(int interval);    // Signal to start a mole timer with a specified interval

private slots:
    void startGame();    // Starts the game
    void endGame();    // Ends the game
    void updateGame();    // Updates the game
    void moleWhacked(int moleIndex);    // Called when a mole is whacked
    void moleTimeout(int index);    // Determines if a mole is missed
    void pollProcFile();    // Periodically poll the /proc file to check for changes
    void processButtonPress(const QString &buttonInfo);    // Process and handle button presses

private:
    QPushButton *startButton;    // Button to start the game
    QPushButton *endButton;    // Button to end the game
    QLabel *scoreLabel;    // button to track the score
    QTimer *gameTimer;    // Main game timer
    QTimer *moleTimer;    // Mole timer
    int score;    // Game score
    int currentMole;    // Index of current active mole
    QGridLayout *moleGrid;    // Layout for mole buttons
    QList<QPushButton *> moleButtons;  // List to hold buttons for moles
    QThread *moleTimerThread;    // Thread for running mole timers
    MoleTimer *moleTimerWorker;    // Worker object to handle mole timers in a seperate thread
    QVector<QTimer*> moleVisibilityTimers;  // Vector of timers each controlling the visibility of a mole

    void setupUi();    // Sets up the UI
    void showMole();    // Shows a new mole 
    void hideMole();    // Hides the mole
};

#endif // MAINWINDOW_H
