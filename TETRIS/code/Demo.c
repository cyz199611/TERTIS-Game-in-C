/**
 * This is the main file of the ESPLaboratory TETRIS project.
 *
 *
 * @author: CHEN YUZONG 
 */

#include "includes.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include <time.h>
#include <stdlib.h>

#define levelNum 4
#define maxLineDisappear 4
// Set longer round time for double mode than single mode due to higher difficulty
#define singleModeSpeed 400
#define doubleModeSpeed 600

QueueHandle_t ESPL_RxQueue; // Already defined in ESPL_Functions.h
SemaphoreHandle_t ESPL_DisplayReady;
SemaphoreHandle_t inputReceived; // Binary semaphore

/*----------------------------------------Global Variable----------------------------------------*/
// Start and stop bytes for the UART protocol
static const uint8_t startByte = 0xAA, stopByte = 0x55;
static const uint16_t displaySizeX = 320, displaySizeY = 240;
int globalSpeed = 400;
int scr = 0, lvl = 0, lin = 0;
int roundTime = 100; // Default refreshing time
int isGameOver;
int connected = 0; // Not connected by defaut
int myState = -1, buddyState = -1; // Record game states of 2 connected boards, in single mode by default
int buddyA = 0, buddyB = 0, buddyC = 0, buddyD = 0, buddyE = 0; // Secure inputs of buddy's board
int buddyAState = 1, buddyBState = 1, buddyCState = 1, buddyDState = 1, buddyEState = 1; // Instantaneous inputs of budd's board
int currentX = 0, currentY = 0, currentType = 0, currentColor = 0, nextType = 0, nextColor = 0; // Pass tetris parameters to buddy's board
int buddyCurrentX = 0, buddyCurrentY = 0, buddyCurrentType = 0, buddyCurrentColor = 0, buddyNextType = 0, buddyNextColor = 0;
static const int arrHeight = 20, arrWidth = 10; // Array of the block map
int score_add[maxLineDisappear][levelNum] = {40, 80, 120, 160, 100, 200, 300, 400, 300, 600, 900, 1200, 1200, 2500, 3600, 4800}; // Score setting rule
/*----------------------------------------END Global Variable----------------------------------------*/

/*----------------------------------------enum, struct----------------------------------------*/
enum currentState{ // Game state types
	gameMenu,
	select,
	initGame,
	inGame,
	nextRound,
	gamePause,
	gameOver
};

enum currentMode{ // Game mode types
	modeSelect, // Select game parameters on the main menu
	singlePlayer,
	doublePlayerSelect,
	doublePlayerRotate,
	doublePlayerMove
};

enum button{ // User input types
	A,
	B,
	C,
	D,
	E,
	system_refresh // Condition without pressing of any button
};

enum direction{ // Tetris directions of movement
	down,
	left,
	right
};

struct tetrisBlock { // Tetris parameters
	point center; // Central coordinate of the tetris for rotation
	point position[4]; // Positions containing the coordinates of 4 squares
	int type;
	int next_type;
	int color_num;
};
/*----------------------------------------END enum, struct----------------------------------------*/

/*----------------------------------------typedef enum, struct----------------------------------------*/
typedef enum currentState currentState;
typedef enum currentMode currentMode;
typedef enum button button;
typedef enum direction direction;
typedef struct tetrisBlock tetrisBlock;
/*----------------------------------------END typedef enum, struct----------------------------------------*/

/*----------------------------------------Global enum, struct Variable----------------------------------------*/
currentState state, receivedState;
currentMode mode = modeSelect;
button publicButton = B, privateButton; // Set public and private receivers to secure the button inputs
direction direct;
color_t color[5] = {White, Red, Yellow, Blue, Orange}; // Randomize the tetris color
/*----------------------------------------END Global enum, struct Variable----------------------------------------*/

/*----------------------------------------Task Prototypes----------------------------------------*/
void refreshSystem();
void buttonInput();
void sendToBuddy();
void receiveData();
void gameStateManagement();
/*----------------------------------------END Task Prototypes----------------------------------------*/

/*----------------------------------------Function Prototypes----------------------------------------*/
// Transimit data between 2 connected boards
void sendData();
void tetrisSynchronization(tetrisBlock *currentTetris, tetrisBlock *nextTetris);

// Initialize system settings
void initBuddyBut();
void systemInit();
void initGameSetting(tetrisBlock *currentTetris, tetrisBlock *nextTetris, int map[arrHeight][arrWidth]);

// Manage game state
currentState getState(currentState state, button privateButton);

// Check tetris conditions to trigger next operation
int checkNewTetris(tetrisBlock *tetrisPtr, int map[arrHeight][arrWidth]);
int checkFullLine(int fullLineNumber[5], int map[arrHeight][arrWidth]);
void letLineDisappear(int fullLineNumber[5], int num, int map[arrHeight][arrWidth]);
int checkGameOver(tetrisBlock *blockPtr);

// Modify the fixed block map
void clearMap(int map[arrHeight][arrWidth]);
void clearTetrisPosition(tetrisBlock* blockPtr, int map[arrHeight][arrWidth]);
void printTetrisOnMap(tetrisBlock *blockPtr, int map[arrHeight][arrWidth]);

// Check tetris positions relative to the fixed block map
int getLeft(tetrisBlock* blockPtr);
int getRight(tetrisBlock* blockPtr);
int noCollision(tetrisBlock* blockPtr, int map[arrHeight][arrWidth]);

// Generate tetris blocks
void copyTetris(tetrisBlock *currentTetris, tetrisBlock *nextTetris);
void sendTetris(tetrisBlock* currentTetris, tetrisBlock* nextTetris);
void tetrisInit(tetrisBlock* blockPtr);
void tetrisShape(tetrisBlock* blockPtr);

// Tetris operations
int tetrisMove(tetrisBlock* blockPtr, direction direct, int map[arrHeight][arrWidth]);
void tetrisRotate(tetrisBlock* blockPtr, int map[arrHeight][arrWidth]);

// Draw game interface
void drawGameMenu();
void drawSelectMode(int selected);
void drawGameEnvironment(tetrisBlock* nextTetris, int map[arrHeight][arrWidth]);
void drawPause();
void drawGameOver();
/*----------------------------------------END Function Prototypes----------------------------------------*/


int main() {
	// Initialize Board functions and graphics
	ESPL_SystemInit();

	inputReceived = xSemaphoreCreateBinary();

	xTaskCreate(refreshSystem, "refreshSystem", 2000, NULL, 3, NULL); // Task to refresh the system for each game round
	xTaskCreate(buttonInput, "buttonInput", 2000, NULL, 2, NULL); // Task to get button inputs from the user
	xTaskCreate(gameStateManagement, "gameStateManagement", 2000, NULL, 2, NULL); // Task to manage game states
	xTaskCreate(receiveData, "receiveData", 1000, NULL, 2, NULL); // Task to receive inputs and data from buddy's board
	xTaskCreate(sendToBuddy, "sendToBuddy", 1000, NULL, 2, NULL); // Task to send inputs and data to buddy's board

	// Start FreeRTOS Scheduler
	vTaskStartScheduler();
}


/*----------------------------------------Task Definition----------------------------------------*/
/*
 * Task function to refresh the system for each round
 */
void refreshSystem() {
	TickType_t xLastWakeTime;
	xLastWakeTime = xTaskGetTickCount();
	while (TRUE) {
		srand(xTaskGetTickCount()); // Get random seed from random generator with kernel tick
		publicButton = system_refresh; // Nothing pressed
		xSemaphoreGive(inputReceived); // Give semaphore to task manager
		roundTime = globalSpeed/(lvl+1); // Automatically set descending speed of tetris according to level
		vTaskDelayUntil(&xLastWakeTime, roundTime);
	}
}

/*
 * Task function to get button inputs from the user
 */
void buttonInput() {
	TickType_t xLastWakeTime;
	xLastWakeTime = xTaskGetTickCount();
	const TickType_t tickFramerate = 20; // Set the frame rate of getting button inputs

    // Record previous button values for debounce
	int buddyPressedA = 1, buddyPressedB = 1, buddyPressedC = 1, buddyPressedD = 1, buddyPressedE = 1;
	int pressedA = 1, pressedB = 1, pressedC = 1, pressedD = 1, pressedE = 1;
	int roundNum = 0, connectionErrorTime = 0; // Variables for checking UART connection

	while(TRUE) {
		// Receive local button A input
		if (mode == modeSelect || mode == singlePlayer || mode == doublePlayerRotate || mode == doublePlayerSelect) {
			if (GPIO_ReadInputDataBit(ESPL_Register_Button_A, ESPL_Pin_Button_A)
					== 0 && pressedA == 1) {
				publicButton = A;
				xSemaphoreGive(inputReceived);
				pressedA = 0;
			} else if (GPIO_ReadInputDataBit(ESPL_Register_Button_A, ESPL_Pin_Button_A) == 1)
				pressedA = 1;
		}
		// Receive external button A input
		if (mode == doublePlayerMove || mode == doublePlayerSelect || mode == modeSelect
				|| (myState == (int)gamePause && mode == doublePlayerRotate)) {
			if (buddyAState == 0 && buddyPressedA == 1) {
				publicButton = A;
				buddyA = 1;
				xSemaphoreGive(inputReceived);
				buddyPressedA = 0;
			} else if (buddyAState == 1) {
				buddyPressedA = 1;
			}
		}
		// Receive local button B input
		if (mode == singlePlayer || mode == doublePlayerMove || mode == doublePlayerSelect || mode == modeSelect
				|| (mode == doublePlayerRotate && myState == (int)gamePause)) {
			if (GPIO_ReadInputDataBit(ESPL_Register_Button_B, ESPL_Pin_Button_B)
					== 0 && pressedB == 1) {
				publicButton = B;
				xSemaphoreGive(inputReceived);
				pressedB = 0;
			} else if (GPIO_ReadInputDataBit(ESPL_Register_Button_B, ESPL_Pin_Button_B) == 1)
				pressedB = 1;
		}
		// Receive external button B input
		if (mode == doublePlayerRotate || mode == doublePlayerSelect || mode == modeSelect) {
			if (buddyBState == 0 && buddyPressedB == 1) {
				publicButton = B;
				buddyB = 1;
				xSemaphoreGive(inputReceived);
				buddyPressedB = 0;
			} else if (buddyBState == 1) {
				buddyPressedB = 1;
			}
		}
		// Receive local button C input
		if (mode == singlePlayer || mode == doublePlayerMove || mode == doublePlayerSelect || mode == modeSelect) {
			if (GPIO_ReadInputDataBit(ESPL_Register_Button_C, ESPL_Pin_Button_C)
					== 0 && pressedC == 1){
				publicButton = C;
				xSemaphoreGive(inputReceived);
				pressedC = 0;
			} else if (GPIO_ReadInputDataBit(ESPL_Register_Button_C, ESPL_Pin_Button_C) == 1)
				pressedC = 1;
		}
		// Receive external button C input
		if (mode == doublePlayerRotate || mode == doublePlayerSelect || mode == modeSelect) {
			if (buddyCState == 0 && buddyPressedC == 1) {
				publicButton = C;
				buddyC = 1;
				xSemaphoreGive(inputReceived);
				buddyPressedC = 0;
			} else if (buddyCState == 1) {
				buddyPressedC = 1;
			}
		}
        // Receive local button D input
		if (mode == singlePlayer || mode == doublePlayerMove || mode == doublePlayerSelect || mode == modeSelect
				|| (mode == doublePlayerRotate && myState == (int)gamePause)) {
			if (GPIO_ReadInputDataBit(ESPL_Register_Button_D, ESPL_Pin_Button_D)
					== 0 && pressedD == 1) {
				publicButton = D;
				xSemaphoreGive(inputReceived);
				pressedD = 0;
			} else if (GPIO_ReadInputDataBit(ESPL_Register_Button_D, ESPL_Pin_Button_D) == 1)
				pressedD = 1;
		}
		// Receive external button D input
		if (mode == doublePlayerRotate || mode == doublePlayerSelect || mode == modeSelect) {
			if (buddyDState == 0 && buddyPressedD == 1) {
				publicButton = D;
				buddyD = 1;
				xSemaphoreGive(inputReceived);
				buddyPressedD = 0;
			} else if (buddyDState == 1) {
				buddyPressedD = 1;
			}
		}
        // Receive local button E input
		if (mode == singlePlayer || mode == doublePlayerRotate) {
			if (GPIO_ReadInputDataBit(ESPL_Register_Button_E, ESPL_Pin_Button_E)
					== 0 && pressedE == 1){
				publicButton = E;
				xSemaphoreGive(inputReceived);
				pressedE = 0;
			} else if (GPIO_ReadInputDataBit(ESPL_Register_Button_E, ESPL_Pin_Button_E) == 1)
				pressedE = 1;
		}
        // Receive external button E input
		if (mode == doublePlayerRotate) {
			if (buddyEState == 0 && buddyPressedE == 1) {
				publicButton = E;
				buddyE = 1;
				xSemaphoreGive(inputReceived);
				buddyPressedE = 0;
			} else if (buddyEState == 1) {
				buddyPressedE = 1;
			}
		}
		// Receive button E input for pause in double mode
		if (mode == doublePlayerMove && buddyState == (int)gamePause){
			publicButton = E;
			xSemaphoreGive(inputReceived);
		}
		// Receive button ABD input for pause in double mode
		if (mode == doublePlayerMove && myState == (int)gamePause){
			if (buddyState == (int)gameOver)
			{
				publicButton = B;
				xSemaphoreGive(inputReceived);
			}
			if (buddyState == (int)initGame)
			{
				publicButton = A;
				xSemaphoreGive(inputReceived);
			}
			if (buddyState == (int)nextRound || buddyState == (int)inGame)
			{
				publicButton = D;
				xSemaphoreGive(inputReceived);
			}

		}
		roundNum = (roundNum + 1) % 100; // range from 0 to 99
		if (roundNum == 0){
			if (connectionErrorTime <= 80) // Connection is broken when there are more than 80 times of connection error detected in 100 detections
				connected = 1;
			else
				connected = 0;
			connectionErrorTime = 0;
		}
		else{
			if ( !(myState == buddyState || (myState <= (int)nextRound && myState >= (int)initGame && buddyState <= (int)nextRound && buddyState > (int)initGame)) )
				connectionErrorTime++;
		}
		buddyState = -1;
		vTaskDelayUntil(&xLastWakeTime, tickFramerate);
	}
}

/*
 * Task function to send inputs and data to buddy's board
 */
void sendToBuddy() {
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    const TickType_t tickFramerate = 10; // Set the sending frame rate
    while (TRUE) {
        sendData();
		// Execute every 10 Ticks
		vTaskDelayUntil(&xLastWakeTime, tickFramerate);
    }
}

/*
 * Task function to receive inputs and data from buddy's board
 */
void receiveData() {
	char input;
	uint8_t pos = 0;
	char checkxor1, checkxor2;
	int buddySysrefresh;
	char buffer[16]; // Start byte, buddy A button state, checksign (copy of data), End byte
	while (TRUE) {
		// Wait for data in queue
		xQueueReceive(ESPL_RxQueue, &input, portMAX_DELAY);

		// Decode package by buffer position
		switch (pos) {
		case 0: // Start byte
			if (input != startByte)
				break;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
			// Read received data in buffer
			buffer[pos] = input;
			pos++;
			break;
		case 15: // End byte
			// Check if package is corrupted
			checkxor1 = buffer[7];
			checkxor2 = buffer[8];
			if (input == stopByte && (checkxor1 == buffer[1] ^ buffer[2]) && (checkxor2 == buffer[3] ^ buffer[4])) {
				buddyAState = buffer[1];
				buddyBState = buffer[2];
				buddyCState = buffer[3];
				buddyDState = buffer[4];
				buddyEState = buffer[5];
				buddyState = buffer[6];
				buddyCurrentX = buffer[9];
				buddyCurrentY = buffer[10];
				buddyCurrentType = buffer[11];
				buddyCurrentColor = buffer[12];
				buddyNextType = buffer[13];
				buddyNextColor = buffer[14];
			}
			pos = 0;
		}
	}
}

/*
 * Task function to manage game states
 */
void gameStateManagement() {
	currentState state = gameMenu; // Start with the main menu
	int map[arrHeight][arrWidth];

	tetrisBlock block1;
	tetrisBlock block2;
	tetrisBlock *currentTetris = &block1;
	tetrisBlock *nextTetris = &block2;

	systemInit();
	drawGameMenu();

	while(TRUE){
		if ((xSemaphoreTake(inputReceived, portMAX_DELAY == pdTRUE))){
			button privateButton = publicButton;
			state = getState(state, privateButton);
			myState = (int)state;
			initBuddyBut();

			switch(state){
			case gameMenu:{ // Display the main menu
				drawGameMenu();
				break;
			}
			case select: { // Select game parameters
				drawSelectMode(1);
				break;
			}
			case initGame:{ // Initialize the game
				initGameSetting(currentTetris, nextTetris, map);
				drawGameEnvironment(nextTetris, map);
				break;
			}
			case inGame:{ // During the game
				clearTetrisPosition(currentTetris, map);
				switch(privateButton){
				case A:{ // Rotate counterclockwise
					tetrisRotate(currentTetris, map);
					break;
				}
				case B:{ // Move right
					tetrisMove(currentTetris, right, map);
					break;
				}
				case C:{ // Move down
					tetrisMove(currentTetris, down, map);
					break;
				}
				case D:{ // Move left
					tetrisMove(currentTetris, left, map);
					break;
				}
				}
				sendTetris(currentTetris, nextTetris);
				printTetrisOnMap(currentTetris, map);
				drawGameEnvironment(nextTetris, map);
				break;
			}
			case nextRound:{
				int isNewTetris = 0;
				clearTetrisPosition(currentTetris, map);
				if (mode == doublePlayerMove){ // In double mode
					int connectionBreakCount = 0;
					while(buddyCurrentY == currentTetris->center.y && buddyCurrentX == currentTetris->center.x
							&& buddyCurrentType == currentTetris->type) {
						if (connectionBreakCount > 200)
							break;
						connectionBreakCount++;
						vTaskDelay(5);
					}
					tetrisSynchronization(currentTetris, nextTetris);
					tetrisBlock temp;
					copyTetris(&temp, currentTetris);
					isNewTetris = checkNewTetris(&temp, map);
				}
				else{ // In single mode
					isNewTetris = checkNewTetris(currentTetris, map); //Check if the tetris falls to the end and try to drop it 1 block down
					sendTetris(currentTetris, nextTetris);
				}
				printTetrisOnMap(currentTetris, map); //Print the fixed position of current tetris on map
				drawGameEnvironment(nextTetris, map);
				if (isNewTetris) {
					isGameOver = checkGameOver(currentTetris); //Check if game is over
					if (mode == doublePlayerMove) {
						int connectionBreakCount = 0;
						while(buddyCurrentY == currentTetris->center.y || !isNewTetris) {
							if (connectionBreakCount > 200)
								break;
							connectionBreakCount++;
							if (buddyCurrentX != currentTetris->center.x || buddyCurrentType != currentTetris->type || !isNewTetris){
								clearTetrisPosition(currentTetris, map);
								tetrisSynchronization(currentTetris, nextTetris);
								tetrisBlock temp;
								copyTetris(&temp, currentTetris);
								isNewTetris = checkNewTetris(&temp, map);
								printTetrisOnMap(currentTetris, map); //Print the fixed position of current tetris on map
								drawGameEnvironment(nextTetris, map);
							}
							vTaskDelay(5);
						}
						tetrisSynchronization(currentTetris, nextTetris);
					}
					else{
						copyTetris(currentTetris, nextTetris); //Change current tetris to next tetris and generate a new next tetris
						tetrisInit(nextTetris);
						sendTetris(currentTetris, nextTetris);
					}
				}
				if (isNewTetris){
					int fullLineNumber[5];
					int noOfFullLine = checkFullLine(fullLineNumber, map);
					if (noOfFullLine){ // Refresh the game condition
						scr += score_add[noOfFullLine-1][lvl];
						lin += noOfFullLine;
						lvl = lvl + lin/5; // Automatic increase of level
						if (lvl > 3)
							lvl = 3;
						drawGameEnvironment(nextTetris, map);
						letLineDisappear(fullLineNumber, noOfFullLine, map);
						drawGameEnvironment(nextTetris, map);
					}
				}
				break;
			}
			case gamePause:{
				drawPause();
				break;
			}
			case gameOver:{
				drawGameOver();
				break;
			}
			}
		}
	}
	initBuddyBut();
}
/*----------------------------------------END Task Definition----------------------------------------*/

/*----------------------------------------Function Definition----------------------------------------*/
/*
 * Function to initialize button inputs of buddy's board
 */
void initBuddyBut(){
	buddyA = 0;
	buddyB = 0;
	buddyC = 0;
	buddyD = 0;
	buddyE = 0;
}

/*
 * Function to send data to buddy's board via UART
 */
void sendData() {
	int butAState,butBState,butCState,butDState,butEState;
	butAState = GPIO_ReadInputDataBit(ESPL_Register_Button_A, ESPL_Pin_Button_A);
	butBState = GPIO_ReadInputDataBit(ESPL_Register_Button_B, ESPL_Pin_Button_B);
	butCState = GPIO_ReadInputDataBit(ESPL_Register_Button_C, ESPL_Pin_Button_C);
	butDState = GPIO_ReadInputDataBit(ESPL_Register_Button_D, ESPL_Pin_Button_D);
	butEState = GPIO_ReadInputDataBit(ESPL_Register_Button_E, ESPL_Pin_Button_E);
	const uint8_t checkxor1 = butAState ^ butBState;
	const uint8_t checkxor2 = butCState ^ butDState;
    UART_SendData(startByte); // Byte 0
    UART_SendData(butAState); // Byte 1
    UART_SendData(butBState); // Byte 2
    UART_SendData(butCState); // Byte 3
    UART_SendData(butDState); // Byte 4
    UART_SendData(butEState); // Byte 5
    UART_SendData(myState); // Byte 6
    UART_SendData(checkxor1); // Byte 7
    UART_SendData(checkxor2); // Byte 8
    UART_SendData(currentX); // Byte 9
    UART_SendData(currentY); // Byte 10
    UART_SendData(currentType); // Byte 11
    UART_SendData(currentColor); // Byte 12
    UART_SendData(nextType); // Byte 13
    UART_SendData(nextColor); // Byte 14
    UART_SendData(stopByte); // Byte 15
}

/*
 * Function to initialize the system settings of the game
 */
void systemInit() {
	currentType = -1; // Value -1 denotes the condition that data is not received yet
	nextType = -1;
	buddyCurrentType = -1;
	buddyNextType = -1;
	lvl = 0;
	mode = modeSelect;
}

/*
 * Function to initialize game setting before starting the game
 */
void initGameSetting(tetrisBlock *currentTetris, tetrisBlock *nextTetris, int map[arrHeight][arrWidth]){
	scr = 0;
	lin = 0;
	isGameOver = 0;
	tetrisInit(currentTetris);
	tetrisInit(nextTetris);
	if (mode == doublePlayerMove){
		while(buddyCurrentType == -1 && buddyNextType == -1) // Parameters are not obtained yet
			vTaskDelay(5);
		tetrisSynchronization(currentTetris, nextTetris); // Synchronize 2 connected boards
	}
	else
		sendTetris(currentTetris, nextTetris); // In single mode directly prepare tetris blocks
	clearMap(map);
}

/*
 * Function to check whether a new tetris should appear and start the new round
 */
int checkNewTetris(tetrisBlock *tetrisPtr, int map[arrHeight][arrWidth]){
	if (!tetrisMove(tetrisPtr, down, map))
		return 1; // Need next tetris
	else return 0;
}

/*
 * Function to check full lines to be eliminated
 */
int checkFullLine(int fullLineNumber[5], int map[arrHeight][arrWidth]){
	int num = 0;
	for (int row = 19; row >= 0; row--){ // Check from top to bottom
		int isLineFull = 1;
		for (int col = 0; col < 10; col++){ // Check from left to right
			if (map[row][col] == 0){
				isLineFull = 0;
				break;
			}
		}
		if (isLineFull){
			num++; // Record the number of lines to be eliminated
			fullLineNumber[num] = row; // Record the line to be eliminated
			for (int col = 0; col < 10; col++){
				map[row][col] = 0; // Eliminate the target line
			}
		}
	}
	return num;
}

/*
 * Function to move remaining tetris blocks down after eliminating full lines
 */
void letLineDisappear(int fullLineNumber[5], int num, int map[arrHeight][arrWidth]){
	while(num > 0){
		int emptyLineNumber = fullLineNumber[num]; // Record the empty line to be filled
		for (int row = emptyLineNumber; row > 0; row--)
			for (int col = 0; col < 10; col++)
				map[row][col] = map[row-1][col];
		num--;
	}
}

/*
 * Function to check whether the game is over
 */
int checkGameOver(tetrisBlock *blockPtr){
	int isGameOver = 0;
	for (int i = 0; i < 4; i++){
		if (blockPtr->position[i].y == 0){
			isGameOver = 1;
			break;
		}
	}
	return isGameOver;
}

/*
 * Function to clear the fixed tetris on the block map
 */
void clearMap(int map[arrHeight][arrWidth]){
	for (int row = 0; row < arrHeight; row++)
		for (int col = 0; col < arrWidth; col++)
			map[row][col] = 0;
}

/*
 * Function to clear the record of coordinates of current tetris in order to get ready for recording the next ones
 */
void clearTetrisPosition(tetrisBlock* blockPtr, int map[arrHeight][arrWidth]){
	for (int i = 0; i < 4; i++)
		map[blockPtr->position[i].y][blockPtr->position[i].x] = 0;
}

/*
 * Function to draw the fixed tetris block on the map when current tetris block stops moving
 */
void printTetrisOnMap(tetrisBlock *blockPtr, int map[arrHeight][arrWidth]){
	// Build fixed tetris
	for (int i = 0; i < 4; i++)
		map[blockPtr->position[i].y][blockPtr->position[i].x] = blockPtr->color_num;
}

/*
 * Function to get the most left horizontal coordinate of current tetris
 */
int getLeft(tetrisBlock* blockPtr) {
	int min = 10;
	for (int i = 0; i < 4; i++){
		if (blockPtr->position[i].x < min)
			min = blockPtr->position[i].x ;
	}
	return min; // Record the most left horizontal coordinate
}

/*
 * Function to get the most right horizontal coordinate of current tetris
 */
int getRight(tetrisBlock* blockPtr) {
	int max = -1;
	for (int i = 0; i < 4; i++){
		if (blockPtr->position[i].x > max)
			max = blockPtr->position[i].x ;
	}
	return max; // Record the most right horizontal coordinate
}

/*
 * Function to check whether there are collisions between current tetris and walls or fixed tetris
 */
int noCollision(tetrisBlock* blockPtr, int map[arrHeight][arrWidth]){
	int i;
	for (i = 0; i <= 3; i++){
		if (map[blockPtr->position[i].y][blockPtr->position[i].x] != 0)
			return 0; // Collide with other tetris blocks or walls
	}
	return 1;
}

/*
 * Function to move next tetris to current one
 */
void copyTetris(tetrisBlock *currentTetris, tetrisBlock *nextTetris) {
	currentTetris->center.x = nextTetris->center.x;
	currentTetris->center.y = nextTetris->center.y;
	currentTetris->color_num = nextTetris->color_num;
	currentTetris->next_type = nextTetris->next_type;
	currentTetris->type = nextTetris->type;
	tetrisShape(currentTetris);
}

/*
 * Function to send current and next tetris parameters to the board
 */
void sendTetris(tetrisBlock* currentTetris, tetrisBlock* nextTetris) {
	currentX = currentTetris->center.x;
	currentY = currentTetris->center.y;
	currentType = currentTetris->type;
	currentColor = currentTetris->color_num;
	nextType = nextTetris->type;
	nextColor = nextTetris->color_num;
}

/*
 * Function to generate random tetris
 */
void tetrisInit(tetrisBlock* blockPtr) {
	blockPtr->center.x = 4;
	blockPtr->center.y = 0;
	blockPtr->type = rand()%28;
	blockPtr->color_num = rand()%4 + 1; // color[0] used only for background
	tetrisShape(blockPtr);
}

/*
 * Function to set types of tetris
 */
void tetrisShape(tetrisBlock* blockPtr){
	switch (blockPtr->type) {
	case 0:
		blockPtr->position[0].x = blockPtr->center.x-1;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y+1;
		blockPtr->position[3].x = blockPtr->center.x+1;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 1;
		break;
	case 1:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x-1;
		blockPtr->position[3].y =blockPtr->center.y+1;
		blockPtr->next_type = 0;
		break;
	case 2:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x+1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x-1;
		blockPtr->position[2].y = blockPtr->center.y+1;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 3;
		break;
	case 3:
		blockPtr->position[0].x = blockPtr->center.x-1;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 2;
		break;
	case 4:
		blockPtr->position[0].x = blockPtr->center.x-1;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x+1;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x+1;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 7;
		break;
	case 5:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x-1;
		blockPtr->position[2].y = blockPtr->center.y+1;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 4;
		break;
	case 6:
		blockPtr->position[0].x = blockPtr->center.x-1;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x+1;
		blockPtr->position[3].y = blockPtr->center.y;
		blockPtr->next_type = 5;
		break;
	case 7:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x+1;
		blockPtr->position[1].y = blockPtr->center.y-1;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 6;
		break;
	case 8:
		blockPtr->position[0].x = blockPtr->center.x+1;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x+1;
		blockPtr->position[3].y = blockPtr->center.y;
		blockPtr->next_type = 9;
		break;
	case 9:
		blockPtr->position[0].x = blockPtr->center.x-1;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y-1;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 10;
		break;
	case 10:
		blockPtr->position[0].x = blockPtr->center.x-1;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x+1;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x-1;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 11;
		break;
	case 11:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y+1;
		blockPtr->position[3].x = blockPtr->center.x+1;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 8;;
		break;
	case 12:
		blockPtr->position[0].x = blockPtr->center.x-1;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y-1;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 15;
		break;
	case 13:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x+1;
		blockPtr->position[3].y = blockPtr->center.y;
		blockPtr->next_type = 12;
		break;
	case 14:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y+1;
		blockPtr->position[3].x = blockPtr->center.x+1;
		blockPtr->position[3].y = blockPtr->center.y;
		blockPtr->next_type = 13;
		break;
	case 15:
		blockPtr->position[0].x = blockPtr->center.x-1;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x+1;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 14;
		break;
	case 16:
		blockPtr->position[0].x = blockPtr->center.x-1;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x-1;
		blockPtr->position[2].y = blockPtr->center.y+1;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 16;
		break;
	case 17:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y+1;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y-1;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y-2;
		blockPtr->next_type = 18;
		break;
	case 18:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x+1;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x+2;
		blockPtr->position[3].y = blockPtr->center.y;
		blockPtr->next_type = 17;
		break;
	case 19:
		blockPtr->position[0].x = blockPtr->center.x-1;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y+1;
		blockPtr->position[3].x = blockPtr->center.x+1;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 20;
		break;
	case 20:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x-1;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 19;
		break;
	case 21:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x+1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x-1;
		blockPtr->position[2].y = blockPtr->center.y+1;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 22;
		break;
	case 22:
		blockPtr->position[0].x = blockPtr->center.x-1;
		blockPtr->position[0].y = blockPtr->center.y-1;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 21;
		break;
	case 23:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x-1;
		blockPtr->position[2].y = blockPtr->center.y+1;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 23;
		break;
	case 24:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x-1;
		blockPtr->position[2].y = blockPtr->center.y+1;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 24;
		break;
	case 25:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x-1;
		blockPtr->position[2].y = blockPtr->center.y+1;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y+1;
		blockPtr->next_type = 25;
		break;
	case 26:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x;
		blockPtr->position[1].y = blockPtr->center.y+1;
		blockPtr->position[2].x = blockPtr->center.x;
		blockPtr->position[2].y = blockPtr->center.y-1;
		blockPtr->position[3].x = blockPtr->center.x;
		blockPtr->position[3].y = blockPtr->center.y-2;
		blockPtr->next_type = 27;
		break;
	case 27:
		blockPtr->position[0].x = blockPtr->center.x;
		blockPtr->position[0].y = blockPtr->center.y;
		blockPtr->position[1].x = blockPtr->center.x-1;
		blockPtr->position[1].y = blockPtr->center.y;
		blockPtr->position[2].x = blockPtr->center.x+1;
		blockPtr->position[2].y = blockPtr->center.y;
		blockPtr->position[3].x = blockPtr->center.x+2;
		blockPtr->position[3].y = blockPtr->center.y;
		blockPtr->next_type = 26;
		break;
		// Increase the probability of rare types to reach a balance between all types
	}
}

/*
 * Function to synchronize the tetris conditions with buddy's board by passing parameters
 */
void tetrisSynchronization(tetrisBlock *currentTetris, tetrisBlock *nextTetris) {
	currentTetris->center.x = buddyCurrentX;
	currentTetris->center.y = buddyCurrentY;
	currentTetris->type = buddyCurrentType;
	currentTetris->color_num = buddyCurrentColor;
	nextTetris->type = buddyNextType;
	nextTetris->color_num = buddyNextColor;
	tetrisShape(currentTetris);
	tetrisShape(nextTetris);
}

/*
 * Function to control the movement of tetris
 */
int tetrisMove(tetrisBlock* blockPtr, direction direct, int map[arrHeight][arrWidth]){
	point pre_center; // Record in case of collision
	pre_center.x = blockPtr->center.x;
	pre_center.y = blockPtr->center.y;

	int min = getLeft(blockPtr); // Minimum horizontal coordinate
	int max = getRight(blockPtr); // Maximum horizontal coordinate

	switch(direct){
	case down:{
		blockPtr->center.y++;
		break;
	}
	case left:{
		if (min > 0) // Not collide with left wall
			blockPtr->center.x--;
		break;
	}
	case right:{
		if (max < 9) // Not collide with right wall
			blockPtr->center.x++;
		break;
	}
	}
	tetrisShape(blockPtr);
	if (!noCollision(blockPtr, map)){
		blockPtr->center.x = pre_center.x;
		blockPtr->center.y = pre_center.y;
		tetrisShape(blockPtr); // Shape the original tetris due to collision
		return 0;
	}
	else
		return 1; // Tetris can move
}

/*
 * Function to control the rotation of tetris
 */
void tetrisRotate(tetrisBlock* blockPtr, int map[arrHeight][arrWidth]){
	int pre_type = blockPtr->type;
	blockPtr->type = blockPtr->next_type;
	tetrisShape(blockPtr);
	int min = getLeft(blockPtr);
	int max = getRight(blockPtr);
	if (min < 0 || max > 9 || (!noCollision(blockPtr, map))){
		blockPtr->type = pre_type;
		tetrisShape(blockPtr);
	}
}

/*
 * Function to get the next game state based on current state
 */
currentState getState(currentState state, button privateButton) {
	switch(state){
	case gameMenu:{
		if (privateButton == A){ // Start single mode
			mode = singlePlayer;
			globalSpeed = singleModeSpeed;
			return initGame;
		}
		if (privateButton == C && connected){ // Start double mode only when 2 boards are connected
			mode = doublePlayerSelect;
			globalSpeed = doubleModeSpeed;
			return select;
		}
		else { // Set game level from 0 to 3
			if (privateButton == B && lvl < 3)
				lvl++;
			else if (privateButton == D && lvl > 0)
				lvl--;
			return gameMenu;
		}
		break;
	}
	case select:
	{
		if (!connected)
		{
			systemInit();
			return gameMenu;
		}
		if (privateButton == A) {
			if (buddyA)
				mode = doublePlayerRotate;
			else
				mode = doublePlayerMove;
			return initGame;
		}

		if (privateButton == C) {
			if (buddyC)
				mode = doublePlayerMove;
			else
				mode = doublePlayerRotate;
			return initGame;
		}
		else { // Set game level from 0 to 3
			if (privateButton == B && lvl < 3)
				lvl++;
			else if (privateButton == D && lvl > 0)
				lvl--;
			return select;
		}
		break;
	}
	case initGame:{
		if (!connected && (mode == doublePlayerMove || mode == doublePlayerRotate))
		{
			systemInit();
			return gameMenu;
		}
		return inGame;
		break;
	}
	case inGame:{
		if (!connected && (mode == doublePlayerMove || mode == doublePlayerRotate)) {
			systemInit();
			return gameMenu;
		}
		if (privateButton == system_refresh) // No operation
			return nextRound;
		if (privateButton == A || privateButton == B ||privateButton == C || privateButton == D) // Move and rotate tetris
			return inGame;
		if (privateButton == E) // Pause the game
			return gamePause;
		break;
	}
	case gamePause:{
		if (!connected && (mode == doublePlayerMove || mode == doublePlayerRotate)) {
			systemInit();
			return gameMenu;
		}
		if (privateButton == D) {
			if (mode == doublePlayerMove)
				return nextRound;
			return inGame; // Resume the game if D is pressed
		}
		else if (privateButton == B)
			return gameOver; // End the game if B is pressed
		else if (privateButton == A)
			return initGame; // Restart the game if A is pressed
		return gamePause; // The system remain in the pause scene waiting for the input if no operation is executed
		break;
	}
	case nextRound:{
		if (isGameOver)
			return gameOver;
		else {
			if (!connected && (mode == doublePlayerMove || mode == doublePlayerRotate)) {
				systemInit();
				return gameMenu;
			}
			if (mode == doublePlayerMove) {
				if (privateButton == E) // Pause the game
					return gamePause;
				return nextRound;
			}
		    else
			    return inGame;
		}
		break;
	}
	case gameOver:{
		if (privateButton != system_refresh) { // Press any button to exit to menu
			systemInit();
			return gameMenu;
		}
		else
			return gameOver;
		break;
	}
	}
}

/*
 * Function to draw the main menu in menu mode
 */
void drawGameMenu() {
	char str[100]; // buffer for messages to draw to display

	// Load font for ugfx
	font_t font1;
	font1 = gdispOpenFont("DejaVuSans24*");
	font_t font2;
	font2 = gdispOpenFont("DejaVuSans32*");

	gdispClear(White);// Clear background

	// Menu interface
	const char *title = "TETRIS";
	gdispDrawString(103, 30, title, font2, Green);

	const char *sgl = "Single Mode(A)";
	gdispDrawString(120, 80, sgl, font1, Black);
	gdispDrawBox(100, 70, 120, 30, Green);

	const char *dbl = "Double Mode(C)";
	gdispDrawString(118, 130, dbl, font1, Black);
	gdispDrawBox(100, 120, 120, 30, Green);

	sprintf(str, "Level: %2d", lvl);
	gdispDrawString(140, 180, str, font1, Black);
	gdispDrawBox(110, 170, 100, 30, Green);

	const char *author = "Produced by: Chen Yuzong & Zhai Yueliang";
	gdispDrawString(45, 220, author, font1, Blue);

	// Wait for display to stop writing
	xSemaphoreTake(ESPL_DisplayReady, portMAX_DELAY);
	// swap buffers
	ESPL_DrawLayer();
}

/*
 * Function to draw the main menu in select mode
 */
void drawSelectMode(int selected) {
	char str[100]; // buffer for messages to draw to display

	// Load font for ugfx
	font_t font1;
	font1 = gdispOpenFont("DejaVuSans24*");
	font_t font2;
	font2 = gdispOpenFont("DejaVuSans32*");

	gdispClear(White);
	xSemaphoreTake(ESPL_DisplayReady, portMAX_DELAY);

	// Menu interface
	const char *title = "TETRIS";
	gdispDrawString(103, 30, title, font2, Green);

	const char *sgl = "Control Move(A)";
	gdispDrawString(111, 80, sgl, font1, Black);
	gdispDrawBox(100, 70, 120, 30, Green);

	const char *dbl = "Control Rotate(C)";
	gdispDrawString(114, 130, dbl, font1, Black);
	gdispDrawBox(100, 120, 120, 30, Green);

	sprintf(str, "Level: %2d", lvl);
	gdispDrawString(140, 180, str, font1, Black);
	gdispDrawBox(110, 170, 100, 30, Green);

	const char *author = "Produced by: Chen Yuzong & Zhai Yueliang";
	gdispDrawString(45, 220, author, font1, Blue);

	// Wait for display to stop writing
	xSemaphoreTake(ESPL_DisplayReady, portMAX_DELAY);
	// swap buffers
	ESPL_DrawLayer();
}

/*
 * Function to draw the game environment when playing
 */
void drawGameEnvironment(tetrisBlock* nextTetris, int map[arrHeight][arrWidth]){
	// Load font for ugfx
	font_t font1;
	font1 = gdispOpenFont("DejaVuSans24*");
	font_t font2;
	font2 = gdispOpenFont("DejaVuSans32*");

	gdispClear(Green); // Background

	gdispFillArea(230, 10, 80, 35, White);
	const char *score = "SCORE";
	gdispDrawString(245, 15, score, font1, Black);

	gdispFillArea(230, 55, 80, 35, White);
	const char *level = "LEVEL";
	gdispDrawString(245, 60, level, font1, Black);

	gdispFillArea(230, 100, 80, 35, White);
	const char *line = "LINE";
	gdispDrawString(245, 105, line, font1, Black);

	gdispFillArea(230, 145, 80, 85, White);
	const char *next = "NEXT";
	gdispDrawString(245, 150, next, font1, Black);

	gdispFillArea(10, 10, 90, 220, White); // Instructions for game operations
	const char *operation = "Operations:";
	const char *operation1 = "A  Rotate";
	const char *operation2 = "B  Move right";
	const char *operation3 = "C  Move down";
	const char *operation4 = "D  Move left";
	const char *operation5 = "E  Pause";
	const char *operation6 = "F  Menu";
	gdispDrawString(15, 20, operation, font1, Black);
	gdispDrawString(15, 40, operation1, font1, Black);
	gdispDrawString(15, 60, operation2, font1, Black);
	gdispDrawString(15, 80, operation3, font1, Black);
	gdispDrawString(15, 100, operation4, font1, Black);
	gdispDrawString(15, 120, operation5, font1, Black);
	gdispDrawString(15, 140, operation6, font1, Black);

    // Print instruction for double mode
	const char *myGameMode1 = "You Move";
	const char *myGameMode2 = "You Rotate";
	if (mode == doublePlayerMove)
		gdispDrawString(25, 190, myGameMode1, font1, Red);
	else if (mode == doublePlayerRotate)
		gdispDrawString(25, 190, myGameMode2, font1, Red);

	char str1[50], str2[50], str3[50];
	sprintf(str1, "%5d",scr);
	gdispDrawString(245, 30, str1, font1, Black);
	sprintf(str2, "%5d",lvl);
	gdispDrawString(245, 75, str2, font1, Black);
	sprintf(str3, "%5d",lin);
	gdispDrawString(245, 120, str3, font1, Black);

	// Draw tetris blocks based on array map
	for (int row = 0; row < 20; row++){
		for (int col = 0; col < 10; col++){
			if (map[row][col] != 0)
				gdispFillArea(110+11*col, 10+11*row, 10, 10, color[map[row][col]]);
			else
				gdispFillArea(110+11*col, 10+11*row, 10, 10, color[0]);
		}
	}

	// Draw next tetris prediction
	for (int i = 0; i < 4; i++)
		gdispFillArea(225+10*nextTetris->position[i].x, 190+10*nextTetris->position[i].y, 11, 11, color[nextTetris->color_num]);

	// Swap buffers
	ESPL_DrawLayer();
}

/*
 * Function to draw the pause scene
 */
void drawPause(){
	char str1[100], str2[100], str3[100], str4[100];
    font_t font2 = gdispOpenFont("DejaVuSans32*");

	gdispClear(White);
	xSemaphoreTake(ESPL_DisplayReady, portMAX_DELAY);

	sprintf(str1, "PAUSE");
	sprintf(str2, "Press D to continue");
	sprintf(str3, "Press B to exit");
	sprintf(str4, "Press A to reset");
	gdispDrawString(110, 20, str1, font2, Blue);
	gdispDrawString(5, 75, str2, font2, Blue);
	gdispDrawString(40, 115, str3, font2, Blue);
	gdispDrawString(40, 155, str4, font2, Blue);

	ESPL_DrawLayer();
}

/*
 * Function to draw the game-over scene
 */
void drawGameOver(){
	char str1[100], str2[100];
	font_t font2 = gdispOpenFont("DejaVuSans32*");

	gdispClear(White);
	xSemaphoreTake(ESPL_DisplayReady, portMAX_DELAY);

	sprintf(str1, "Game Over !!!");
	sprintf(str2, "Score: %d", scr); // Display the final score
	gdispDrawString(45, 70, str1, font2, Red);
	gdispDrawString(45, 125, str2, font2, Red);

	//Set to fixed frame rate
	ESPL_DrawLayer();
}
/*----------------------------------------END Function Definition----------------------------------------*/

/*
 * Hook definitions needed for FreeRTOS to function.
 */
void vApplicationIdleHook() {
	while (TRUE) {
	};
}


void vApplicationMallocFailedHook() {
	while (TRUE) {
	};
}
