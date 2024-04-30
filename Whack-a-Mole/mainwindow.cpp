#include "mainwindow.h"
#include <QRandomGenerator>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QDebug>
#include <QThread>

// Constructor for MainWindow initializes the game UI, sets up timers for moles
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), score(0), currentMole(-1)
{
    setupUi();  // set up the UI
    // Create and configure timers for each mole button
    for(int i = 0; i < moleButtons.size(); ++i){
        QTimer *timer = new QTimer(this);
        timer->setInterval(1500);   // Moles pop up for 1.5 second
        connect(timer, &QTimer::timeout, [this, i]() { moleTimeout(i); });  // Connect timeout signal to handler
        moleVisibilityTimers.push_back(timer);  // Store the timer in a list
    }

    // Set up polling timer for proc file
    QTimer *pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, &MainWindow::pollProcFile);  // Connect the timer's timeout signal to the polling function
    pollTimer->start(100);  // Start the timer to poll every 100 milliseconds
}

// Sets up user interface, initializes widgets, sets up game logic timers and connect signals
void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);     // Create the central widget
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);   // Main layout for the central widget
    moleGrid = new QGridLayout();   // Grid layout for mole buttons

    scoreLabel = new QLabel("Score: 0", this);  // Initialize the score label
    startButton = new QPushButton("Start Game", this);  // Button to start the game
    endButton = new QPushButton("End Game", this);  // Button to end the game

    // Setup mole buttons with the respective colors
    const QStringList colors = {"red", "blue", "green", "yellow"};
    int i = 0;
    for (const QString &color : colors) {
        QPushButton *button = new QPushButton(this);
        button->setStyleSheet(QString("background-color: %1").arg(color));
        // button->setStyleSheet("QPushButton { border: none; }"); // Remove borders
        button->setFixedSize(100, 100); // button sizes
        moleButtons.append(button); // Add to list of mole buttons
        moleGrid->addWidget(button, i / 2, i % 2);  // Add button to the grid layout
        connect(button, &QPushButton::clicked, [this, i] { moleWhacked(i); });  // Connect button click to action
        i++;

        // Setup and start a thread for mole timers to manage visibility
        moleTimerWorker = new MoleTimer();
        moleTimerThread = new QThread(this);
        moleTimerWorker->moveToThread(moleTimerThread);
        connect(moleTimerThread, &QThread::finished, moleTimerWorker, &QObject::deleteLater);
        connect(this, &MainWindow::startMoleTimer, moleTimerWorker, &MoleTimer::runTimer);
        connect(moleTimerWorker, &MoleTimer::timeout, this, &MainWindow::hideMole);
        moleTimerThread->start();
    }

    // Add all elements to the main layout and set it as the central widget
    mainLayout->addWidget(scoreLabel);
    mainLayout->addLayout(moleGrid);
    mainLayout->addWidget(startButton);
    mainLayout->addWidget(endButton);
    setCentralWidget(centralWidget);

     // Initialize timers for game timing and updates
    gameTimer = new QTimer(this);
    moleTimer = new QTimer(this);
    connect(startButton, &QPushButton::clicked, this, &MainWindow::startGame);
    connect(endButton, &QPushButton::clicked, this, &MainWindow::endGame);
    connect(gameTimer, &QTimer::timeout, this, &MainWindow::endGame);
    connect(moleTimer, &QTimer::timeout, this, &MainWindow::updateGame);
}

// Polls the proc file for button press data from the kernel module
void MainWindow::pollProcFile() {
    QFile file("/proc/whackamole");
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);  // Create a text stream for reading
        QString line = stream.readLine();   // Read a single line from the file
        qDebug() << "Read from /proc:" << line; // Debug output to log the line read from /proc
        if (!line.isEmpty()) {
            processButtonPress(line);  // Process the line if it's not empty
        }
        file.close();   // Close the file after reading
    }
}

// Processes the button press information received from the /proc file
void MainWindow::processButtonPress(const QString &buttonInfo) {
    QRegExp regex("Button (\\d+) pressed"); // Regular expression to extract the button index
    int btnIndex = -1;  // store the extracted button index

    // Check if the button index is found in the string
    if (regex.indexIn(buttonInfo) != -1) {
        btnIndex = regex.cap(1).toInt();    // Convert the regex string to an int
    }

    qDebug() << "Regex grabbed:" << btnIndex;   // Debug output the extracted button index

    // If a valid index is found and it matches the current mole
    if (btnIndex != -1 && btnIndex == currentMole) {
        moleWhacked(btnIndex);
    }
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
