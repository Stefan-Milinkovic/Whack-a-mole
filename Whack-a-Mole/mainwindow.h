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

class MoleTimer : public QObject {
    Q_OBJECT
public:
    explicit MoleTimer(QObject *parent = nullptr) : QObject(parent) {}
    void runTimer(int interval) {
        QThread::sleep(interval);
        emit timeout();
    }
signals:
    void timeout();
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    virtual ~MainWindow();

signals:
    void startMoleTimer(int interval);

private slots:
    void startGame();
    void endGame();
    void updateGame();
    void moleWhacked(int moleIndex);
    void moleTimeout(int index);

private:
    QPushButton *startButton;
    QPushButton *endButton;
    QLabel *scoreLabel;
    QTimer *gameTimer;
    QTimer *moleTimer;
    int score;
    int currentMole;
    QGridLayout *moleGrid;
    QList<QPushButton *> moleButtons;  // List to hold buttons for moles
    QThread *moleTimerThread;
    MoleTimer *moleTimerWorker;
    QVector<QTimer*> moleVisibilityTimers;  // Holds timers for each mole

    void setupUi();
    void showMole();
    void hideMole();
};

#endif // MAINWINDOW_H
