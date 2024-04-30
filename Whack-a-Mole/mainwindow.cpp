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
        connect(timer, &QTimer::timeout, [this, i]() {
            moleTimeout(i); });  // Connect timeout signal to handler
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

    moleWhacked(btnIndex);  // Whack the mole
}

/** Function for sending commands to the kernel module via the proc file
*   @param command This is the command string to be send to the kernel module
*/
void sendCommandToKernelModule(const QString &command) {
  QFile file("/proc/whackamole");
  if(file.open(QIODevice::WriteOnly)) {
    QTextStream stream(&file);    // Create a text stream for writing
    stream << command;    // Write the command to the file
    file.close();    // Close the file after writing
  }
}

// Funtion to start the game
void MainWindow::startGame()
{
    sendCommandToKernelModule("GAME_START");
    score = 0;    // Initialize the score to 0
    scoreLabel->setText("Score: 0");    // set the GUI text score to 0
    startButton->setEnabled(false);    // Disable the start game button
    gameTimer->start(20000); // 20 seconds game
    moleTimer->start(1500); // Mole pops up every 1.5 second
}

// Function to end the game
void MainWindow::endGame()
{
    sendCommandToKernelModule("GAME_STOP");
    gameTimer->stop();    // Stop the game timer
    moleTimer->stop();    // Stop the mole timers
    startButton->setEnabled(true);    // Reenable the start button
    hideMole();    // Hide the moles
    qDebug() << "Final Score:" << score;    // Show the final score in debug
}

// Function to update game state
void MainWindow::updateGame()
{
    showMole();    // Show moles as they are generated
}

// Function to determine if a mole not whacked in the timing requirement
void MainWindow::moleTimeout(int index) {    // Check if the timed out mole is the current mole
    qDebug() << "Mole timeout called for index:" << index;
    if (index == currentMole) {
        score -= 1; // Deduct score for not hitting the mole in time
        scoreLabel->setText(QString("Score: %1").arg(score));    // Update score
        hideMole();    // Hide the mole since its timeout period has expired
    }
}

/**
 * Processes the event when a mole is whacked by the player.
 * Function is triggered when a player clicks on a mole button. It checks if the mole whacked
 * is the currently active mole. If it is, the player's score is increased, the mole's image is updated,
 * and a timer is started to hide the mole shortly after. If the wrong mole is hit, the player's score is decreased.
 *
 * @param moleIndex The index of the mole that was whacked.
 */
void MainWindow::moleWhacked(int moleIndex)
{
    qDebug() << "Mole whacked at index:" << moleIndex;    // Debug output to log the whacked mole's index
    if (moleIndex == currentMole)
    {    // Check if the correct mole was whacked
        score += 3;    // Increase score on correct hit
        qDebug() << "Score increased to:" << score;    // Debug what the score was increased to

        moleButtons[moleIndex]->setIcon(QIcon(":/images/bonk.png"));    // Mole's icon changed to the "whack" image
        moleButtons[moleIndex]->setIconSize(moleButtons[moleIndex]->size());    // Adjust icon size to fit the button

        QTimer::singleShot(250, this, [this] { // Start a timer to hide the mole
            hideMole();    // Hide the mole after a short delay
            sendCommandToKernelModule("LED_OFF"); // Turn off LED
        });
    }
    else
    {
        score -= 1;    // Deduct score if wrong mole was hit
        qDebug() << "Score decreased to:" << score;
    }
    scoreLabel->setText(QString("Score: %1").arg(score));    // Update the score
}

// Displays a new mole randomly on the game board, and sends command to the kernel to activate the corresponding LED
void MainWindow::showMole() {
    if (currentMole != -1)
        hideMole();    // Hide the currently active mole if one is visible

    int index = QRandomGenerator::global()->bounded(moleButtons.size());    // Randomly select a mole index
    currentMole = index;    // Update the current mole index

    QString moleColor = moleButtons[index]->styleSheet().split(": ").last();    // Get the color of the mole from its style sheet
    moleButtons[index]->setIcon(QIcon(QString(":/images/%1Mole.png").arg(moleColor)));    // Set the mole's icon based on its color
    moleButtons[index]->setIconSize(moleButtons[index]->size());    // Ensure the icon size matches the button size

    moleVisibilityTimers[index]->start(1500); // Start the timer for this mole
    sendCommandToKernelModule(moleColor + "_ON");   // Send command to turn on corresponding LED
}

void MainWindow::hideMole() {
    if (currentMole != -1) {    // Check if there is a currently active mole
        moleButtons[currentMole]->setIcon(QIcon());    // Remove the mole's icon
        moleVisibilityTimers[currentMole]->stop(); // Stop the timer for this mole
        sendCommandToKernelModule("LED_OFF");   // Send command to hide the mole
        currentMole = -1;    // Reset the currentMole index to -1 indicating no active mole
    }
}

// Destructor for MainWindow, cleans up allocated resources
MainWindow::~MainWindow()
{
    for (auto timer : moleVisibilityTimers) {    // Loop through all mole visibility timers
        timer->stop();    // Stop the timer
        delete timer;    // Delete the timer to free the memory
    }
    moleTimerThread->quit();    // Quit the mole timer
    moleTimerThread->wait();    // Wait for mole timer thread to finish execution
}
