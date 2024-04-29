#include "mainwindow.h"
#include <QRandomGenerator>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QDebug>
#include <QThread>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), score(0), currentMole(-1)
{
    setupUi();
    for(int i = 0; i < moleButtons.size(); ++i){
        QTimer *timer = new QTimer(this);
        timer->setInterval(1500);   // Moles pop up for 1 second
        connect(timer, &QTimer::timeout, [this, i]() { moleTimeout(i); });
        moleVisibilityTimers.push_back(timer);
    }
}

void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    moleGrid = new QGridLayout();

    scoreLabel = new QLabel("Score: 0", this);
    startButton = new QPushButton("Start Game", this);
    endButton = new QPushButton("End Game", this);

    // Setup mole buttons
    const QStringList colors = {"red", "blue", "green", "yellow"};
    int i = 0;
    for (const QString &color : colors) {
        QPushButton *button = new QPushButton(this);
        button->setStyleSheet(QString("background-color: %1").arg(color));
        // button->setStyleSheet("QPushButton { border: none; }"); // Remove borders
        button->setFixedSize(100, 100); // button sizes
        moleButtons.append(button);
        moleGrid->addWidget(button, i / 2, i % 2);
        connect(button, &QPushButton::clicked, [this, i] { moleWhacked(i); });
        i++;

        // Threaded
        moleTimerWorker = new MoleTimer();
        moleTimerThread = new QThread(this);
        moleTimerWorker->moveToThread(moleTimerThread);
        connect(moleTimerThread, &QThread::finished, moleTimerWorker, &QObject::deleteLater);
        connect(this, &MainWindow::startMoleTimer, moleTimerWorker, &MoleTimer::runTimer);
        connect(moleTimerWorker, &MoleTimer::timeout, this, &MainWindow::hideMole);
        moleTimerThread->start();
    }

    mainLayout->addWidget(scoreLabel);
    mainLayout->addLayout(moleGrid);
    mainLayout->addWidget(startButton);
    mainLayout->addWidget(endButton);

    setCentralWidget(centralWidget);

    gameTimer = new QTimer(this);
    moleTimer = new QTimer(this);

    connect(startButton, &QPushButton::clicked, this, &MainWindow::startGame);
    connect(endButton, &QPushButton::clicked, this, &MainWindow::endGame);
    connect(gameTimer, &QTimer::timeout, this, &MainWindow::endGame);
    connect(moleTimer, &QTimer::timeout, this, &MainWindow::updateGame);
}

// Function for sending commands to the kernel module
void sendCommandToKernelModule(const QString &command) {
  QFile file("/proc/whackamole");
  if(file.open(QIODevice::WriteOnly)) {
    QTextStream stream(&file);
    stream << command;
    file.close();
  }
}

void MainWindow::startGame()
{
    score = 0;
    scoreLabel->setText("Score: 0");
    startButton->setEnabled(false);
    gameTimer->start(20000); // 20 seconds game
    moleTimer->start(1500); // Mole pops up every 1.5 second
}

void MainWindow::endGame()
{
    gameTimer->stop();
    moleTimer->stop();
    startButton->setEnabled(true);
    hideMole();
    qDebug() << "Final Score:" << score;
}

void MainWindow::updateGame()
{
    showMole();
}

void MainWindow::moleTimeout(int index) {
    if (index == currentMole) {
        score -= 1; // Deduct score for not hitting the mole in time
        scoreLabel->setText(QString("Score: %1").arg(score));
        hideMole();
    }
}

void MainWindow::moleWhacked(int moleIndex)
{
    qDebug() << "Mole whacked at index:" << moleIndex;
    if (moleIndex == currentMole)
    {
        score += 3;
        qDebug() << "Score increased to:" << score;
        moleButtons[moleIndex]->setIcon(QIcon(":/images/bonk.png"));
        moleButtons[moleIndex]->setIconSize(moleButtons[moleIndex]->size());
        QTimer::singleShot(500, this, [this] { hideMole();
        sendCommandToKernelModule("LED_OFF"); // Turn off LED
        }); // Hide after delay
    }
    else
    {
        score -= 1;
        qDebug() << "Score decreased to:" << score;
    }
    scoreLabel->setText(QString("Score: %1").arg(score));
}

void MainWindow::showMole() {
    if (currentMole != -1)
        hideMole();

    int index = QRandomGenerator::global()->bounded(moleButtons.size());
    currentMole = index;
    QString moleColor = moleButtons[index]->styleSheet().split(": ").last();
    moleButtons[index]->setIcon(QIcon(QString(":/images/%1Mole.png").arg(moleColor)));
    moleButtons[index]->setIconSize(moleButtons[index]->size());
    moleVisibilityTimers[index]->start(); // Start the timer for this mole
    sendCommandToKernelModule(moleColor + "_ON");   // Send command to turn on corresponding LED
}

void MainWindow::hideMole() {
    if (currentMole != -1) {
        moleButtons[currentMole]->setIcon(QIcon());
        moleVisibilityTimers[currentMole]->stop(); // Stop the timer for this mole
        sendCommandToKernelModule("LED_OFF");   // Send command to hide the mole
        currentMole = -1;
    }
}

MainWindow::~MainWindow()
{
    for (auto timer : moleVisibilityTimers) {
        timer->stop();
        delete timer;
    }
    moleTimerThread->quit();
    moleTimerThread->wait();
}
