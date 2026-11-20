// *** FLASH true FOR LEFT HOST BOARD AND false FOR RIGHT CLIENT BOARD

#define IS_HOST false

// *** define arrays for the anode (+) and cathode (-) wire pins and joystick analog pins

const byte ANODE_PINS[8] = {13, 12, 11, 10, 9, 8, 7, 6}; // rows
const byte CATHODE_PINS[8] = {A3, A2, A1, A0, 5, 4, 3, 2}; // cols
const byte JOY_X = A4; // x potentiometer
const byte JOY_Y = A5; // y potentiometer

// *** can change these values to fine tune the game

const int DEADZONE = 200; // how far the joystick must move to register an input
const unsigned int START_TICK_MS = 250; // time between moves at the start
const unsigned int MIN_TICK_MS = 110; // time between moves at the fastest
const unsigned int SPEEDUP_MS = 3; // how much the game speeds up after each move
const byte WIN_SCORE = 3; // number of rounds needed for a player to win the game

const byte START_TRAIL_LEN = 8; // tron bike length at round start
const byte MAX_TRAIL_LEN = 16; // longest a trail can grow
const byte GROW_EVERY_TICKS = 5; // +1 increase in length every N ticks, 0 for fixed length

const byte BRIGHT_A = 15; // player A trail brightness
const byte BRIGHT_B = 5; // player B trail brightness

const unsigned long COUNTDOWN_MS = 1500; // how long the blinking lasts before a round starts
const unsigned long CRASH_MS = 2000; // how long the blinking lasts after a crash
const unsigned long GAMEOVER_MS = 3000; // how long the blinking lasts after the game ends
const unsigned long HOLD_TO_START_MS = 1000; // hold stick toward opponent for this long to start
const unsigned long SEND_EVERY_MS = 60; // host state resend interval
const unsigned long INPUT_EVERY_MS = 30; // client input send interval
const unsigned long LINK_TIMEOUT_MS = 500; // client: no packets this long = link lost

// *** constants for serial data transfer between boards

const byte W = 16; // width of the arena across both boards
const byte H = 8; // height of the arena
enum : byte { // cell value
  EMPTY = 0,
  TRAIL_A = 1,
  TRAIL_B = 2
};
enum : byte { // directions
  UP = 0,
  RIGHT = 1,
  DOWN = 2,
  LEFT = 3,
  NONE = 4
};
enum : byte { // game phases
  COUNTDOWN = 0,
  PLAYING = 1,
  CRASHED = 2,
  GAMEOVER = 3,
  LOBBY = 4
};
const int8_t DX[4] = { 0, 1, 0, -1 }; // how to adjust x value for movement in each direction index 0-3
const int8_t DY[4] = { -1, 0, 1, 0 }; // how to adjust y value for movement in each direction index 0-3

const byte PKT_STATE = 0xFF; // first byte of every game state packet from host to client
const byte PKT_INPUT = 0xFE; // first byte of every joystick packet from client to host
const byte STATE_LEN = 41; // number of bytes in a state packet after the start byte 0xFF

const byte SLOT_VALUE[4] = {1, 4, 16, 64}; // packing 4 grid cells into each byte

// *** shared state computed on host board and shared to client board

byte grid[W][H]; // initializes two-dimensional array for the 16x8 board's cells
byte ax = 1; // player A's head x position
byte ay = 3; // player A's head y position
byte bx = 14; // player B's head x position
byte by = 4; // player B's head y position
byte phase = LOBBY; // set starting phase to LOBBY
bool deadA = false; // player A starts alive
bool deadB = false; // player B starts alive
byte scoreA = 0; // player A's score starts at 0
byte scoreB = 0; // player B's score starts at 0
unsigned long phaseStart = 0; // sets the current phase start time to 0ms

byte ledOn[8][8]; // initializes the brightness array for each board's LEDs

// *** host only

byte dirA = RIGHT; // direction player A is moving
byte dirB = LEFT; // direction player B is moving
byte pendA = RIGHT; // pending direction for player A
byte pendB = LEFT; // pending direction for player B
unsigned long lastTick = 0; // sets the time the last move happened to 0ms
unsigned long lastSend = 0; // sets the time the last packet was sent to 0ms
unsigned long holdStart = 0; // when someone started holding their stick in the lobby (0 = nobody holding)
unsigned int tickMs = START_TICK_MS; // sets the current time between moves to the starting constant

byte lastBDir = NONE; // last direction board B sent, used to start a match from the lobby
unsigned long lastBInput = 0; // when board A last got a direction from board B

struct Trail { // each trail is a ring buffer of cells (x*8 + y) with the oldest cells first
  byte cells[MAX_TRAIL_LEN]; // buffer with the maximum possible trail size
  byte start; // index of the oldest cell in the trail
  byte count; // number of cells in the trail
};
Trail trailA; // initializes the trail struct for player A
Trail trailB; // initializes the trail struct for player B
byte trailLen = START_TRAIL_LEN; // sets the current trail length to the starting constant
unsigned int tickCount = 0; // number of ticks that have occurred during this round

// *** client only

unsigned long lastPacket = 0; // the time that board B last received a valid game state packet
unsigned long lastInputSend = 0; // the time that board B last sent a valid joystick packet

// *** display

// configures appropriate board's LEDs with brightness value
void setArenaLed(byte x, byte y, byte b) {
  if (IS_HOST) {
    if (x < 8) { 
      ledOn[7 - x][y] = b; // set brightness for this cell on board A
    }
  } else {
    if (x >= 8) { 
      ledOn[7 - y][15 - x] = b; // set brightness for this cell on board B
    }
  }
}

// variable brightness display with each row slot split into 15 slices
void display(byte pattern[8][8]) {
  // lights up all LEDs that need to be on in the current pattern
  for (byte row = 0; row < 8; row++) {
    for (byte col = 0; col < 8; col++) {
      if (pattern[row][col] > 0) {
        digitalWrite(CATHODE_PINS[col], LOW);
      } else {
        digitalWrite(CATHODE_PINS[col], HIGH);
      }
    }
    digitalWrite(ANODE_PINS[row], LOW);

    // brightness control via how long each light is on
    for (byte t = 0; t < 15; t++) {
      for (byte col = 0; col < 8; col++) {
        if (pattern[row][col] > t) {
          digitalWrite(CATHODE_PINS[col], LOW);
        } else {
          digitalWrite(CATHODE_PINS[col], HIGH);
        }
      }
      delayMicroseconds(50); // delay is inconsequential to serial tranfer and input
    }
    digitalWrite(ANODE_PINS[row], HIGH);
  }
}

void render() {
  unsigned long now = millis(); // set the current time in ms

  // two blink timers
  bool slow = (now / 150) % 2 == 1; // flips every 150 ms
  bool fast = (now / 80) % 2 == 1; // flips every 80 ms

  // start from a blank picture array with all LEDs off
  memset(ledOn, 0, sizeof(ledOn));

  // link check for client board only
  if (!IS_HOST) {
    if (now - lastPacket > LINK_TIMEOUT_MS) {
      if (slow) {
        // blinks the lights along the seam if there is no connection
        for (byte y = 0; y < H; y++) {
          setArenaLed(8, y, 6);
        }
      }
      return; // draw nothing else while the link is down
    }
  }

  // lobby screen
  if (phase == LOBBY) {
    // dim line sweeping across both boards
    byte sweep = (now / 120) % W;
    for (byte y = 0; y < H; y++) {
      setArenaLed(sweep, y, 3);
    }

    // start positions pulse
    byte startBrightness;
    if (slow) {
      startBrightness = 15;
    } else {
      startBrightness = 4;
    }
    setArenaLed(1, 3, startBrightness);
    setArenaLed(14, 4, startBrightness);

    // progress bar on the seam of the host board while either player holds their stick to start
    if (IS_HOST) {
      if (holdStart > 0) {
        unsigned long held = now - holdStart;
        byte barLength = held * 8 / HOLD_TO_START_MS + 1; // number of LEDs to turn on based on duration of input
        if (barLength > 8) {
          barLength = 8; // cap the number of LEDs to the height of the seam
        }
        for (byte i = 0; i < barLength; i++) {
          setArenaLed(7, H - 1 - i, 15); // light up the number of LEDs set above
        }
      }
    }
    return;
  }

  // drawing the trails
  bool aWon = false; // initialize that team A has not won
  if (scoreA >= WIN_SCORE) { // if team A has a winning number of points, set that they won
    aWon = true;
  }

  // render the full board
  for (byte x = 0; x < W; x++) {
    for (byte y = 0; y < H; y++) {
      byte cell = grid[x][y]; // track value of the current cell

      // skip cells with neither trail
      if (cell == EMPTY) {
        continue;
      }

      // find the brightness needed for the current player's trail
      byte brightness;
      if (cell == TRAIL_A) {
        brightness = BRIGHT_A;
      } else {
        brightness = BRIGHT_B;
      }

      if (phase == CRASHED) { // flash the trail of the player that crashed
        bool playerDied = false; // initialize that the player hasn't died before checking which player died
        if (cell == TRAIL_A && deadA) {
          playerDied = true;
        }
        if (cell == TRAIL_B && deadB) {
          playerDied = true;
        }
        if (playerDied) {
          if (!fast) {
            brightness = 0; // off for half of each flash
          }
        }
      } else if (phase == GAMEOVER) { // if the game ended, only the winner's trail stays lit and pulsing
        bool winnerTrail;
        if (aWon) { // determines if the current cell is part of the winning player's trail
          winnerTrail = (cell == TRAIL_A);
        } else {
          winnerTrail = (cell == TRAIL_B);
        }

        // alternates the brightness to pulse the winning player's trail
        if (winnerTrail) {
          if (slow) {
            brightness = 15;
          } else {
            brightness = 4;
          }
        } else {
          brightness = 0; // turn off the trail of the loser
        }
      }

      if (brightness > 0) {
        setArenaLed(x, y, brightness); // only draw cells that are on
      } 
    }
  }

  // blinking heads
  if (phase == COUNTDOWN || phase == PLAYING) {
    bool on;
    if (phase == COUNTDOWN) {
      on = fast; // quick blinking during countdown
    } else {
      on = slow; // slower blinking during play
    }

    byte headBrightness;
    if (on) {
      headBrightness = 15;
    } else {
      headBrightness = 0;
    }

    setArenaLed(ax, ay, headBrightness);
    setArenaLed(bx, by, headBrightness);
  }

  // score shown during countdown
  if (phase == COUNTDOWN) {
    for (byte i = 0; i < scoreA && i < H; i++) {
      setArenaLed(0, i, 8); // player A's points in top left corner
    }
    for (byte i = 0; i < scoreB && i < H; i++) {
      setArenaLed(15, H - 1 - i, 8); // player B's points in bottom right corner
    }
  }
}

// *** input

// reads the joystick and returns which direction it's pushed, or NONE if centered
byte readJoyDir() {
  int dx = analogRead(JOY_X) - 512; // set x joystick neutral position to 0
  int dy = analogRead(JOY_Y) - 512; // set y joystick neutral position to 0
  if (max(abs(dx), abs(dy)) < DEADZONE) {
    return NONE; // stick is close enough to center to ignore
  }
  // use whichever axis is pushed further so diagonals pick one direction
  if (abs(dx) > abs(dy)) {
    if (dx > 0) {
      return RIGHT;
    } else {
      return LEFT;
    }
  } else {
    if (dy > 0) {
      return DOWN;
    } else {
      return UP;
    }
  }
}

// directions go clockwise 0 to 3 meaning the opposite is 2 steps around
bool isOpposite(byte a, byte b) {
  if (((a + 2) % 4) == b) {
    return true;
  } else {
    return false;
  }
}

// *** host game logic and sending state

// true if a bike moving into this cell would crash
bool blocked(byte x, byte y) {
  if (grid[x][y] != EMPTY) {
    return true;
  } else {
    return false;
  }
}

// empty the trail without clearing the grid
void trailReset(Trail &t) {
  t.start = 0;
  t.count = 0;
}

// add a new head cell to the end of the trail
void trailPush(Trail &t, byte x, byte y) {
  byte index = (t.start + t.count) % MAX_TRAIL_LEN; // wraps back to 0 past the end of the array
  t.cells[index] = x * 8 + y; // store x and y as one number
  t.count++;
}

// remove the oldest cells until the trail is at most len long
void trailTrim(Trail &t, byte len) {
  while (t.count > len) {
    byte c = t.cells[t.start];
    grid[c / 8][c % 8] = EMPTY; // turn the cell back into x and y and clear it
    t.start = (t.start + 1) % MAX_TRAIL_LEN; // next oldest cell is now the start
    t.count--;
  }
}

// packs the game into 41 bytes and sends it to board B
void sendState() {
  byte buf[STATE_LEN];
  buf[0] = phase;

  // both death flags in one byte: 1 = A dead, 2 = B dead, 3 = both
  byte flags = 0;
  if (deadA) {
    flags = flags + 1;
  }
  if (deadB) {
    flags = flags + 2;
  }
  buf[1] = flags;

  buf[2] = ax;
  buf[3] = ay;
  buf[4] = bx;
  buf[5] = by;
  buf[6] = scoreA;
  buf[7] = scoreB;

  // pack the grid: with 4 cells per byte, each cell worth 1, 4, 16, or 64
  memset(buf + 8, 0, 32);
  for (byte x = 0; x < W; x++) {
    for (byte y = 0; y < H; y++) {
      byte i = x * 8 + y; // cell number 0 to 127
      byte whichByte = 8 + i / 4;
      byte slot = i % 4;
      buf[whichByte] = buf[whichByte] + grid[x][y] * SLOT_VALUE[slot];
    }
  }

  // checksum so board B can throw away packets that got corrupted
  // kept under 128 so it can never look like the 0xFF start byte
  byte sum = 0;
  for (byte i = 0; i < 40; i++) {
    sum = sum + buf[i];
  }
  buf[40] = sum % 128;

  // never block the display, so skip this send if the TX buffer is busy
  if (Serial.availableForWrite() < STATE_LEN + 1) {
    return;
  }
  Serial.write(PKT_STATE);
  Serial.write(buf, STATE_LEN);
  lastSend = millis();
}

// clears the arena and puts both bikes back at their start positions
void resetRound() {
  memset(grid, 0, sizeof(grid));

  ax = 1;
  ay = 3;
  bx = 14;
  by = 4;

  dirA = RIGHT;
  pendA = RIGHT;
  dirB = LEFT;
  pendB = LEFT;

  grid[ax][ay] = TRAIL_A;
  grid[bx][by] = TRAIL_B;

  // each trail starts with just the head
  trailReset(trailA);
  trailPush(trailA, ax, ay);
  trailReset(trailB);
  trailPush(trailB, bx, by);

  trailLen = START_TRAIL_LEN;
  tickCount = 0;
  deadA = false;
  deadB = false;
  tickMs = START_TICK_MS;
  phase = COUNTDOWN;
  phaseStart = millis();
  sendState(); // tell board B right away
}

// new match, scores go back to 0
void startMatch() {
  scoreA = 0;
  scoreB = 0;
  resetRound();
}

void enterLobby() {
  phase = LOBBY;
  phaseStart = millis();
  holdStart = 0;
  sendState();
}

// moves both bikes one cell and checks for crashes
void stepGame() {
  // trails grow every few moves so rounds can't go on forever
  tickCount++;
  if (GROW_EVERY_TICKS > 0) {
    if (tickCount % GROW_EVERY_TICKS == 0) {
      if (trailLen < MAX_TRAIL_LEN) {
        trailLen++;
      }
    }
  }

  // apply the directions the players asked for since the last move
  dirA = pendA;
  dirB = pendB;

  // all four edges wrap, adding W or H first keeps it from going negative
  byte nax = (ax + DX[dirA] + W) % W;
  byte nay = (ay + DY[dirA] + H) % H;
  byte nbx = (bx + DX[dirB] + W) % W;
  byte nby = (by + DY[dirB] + H) % H;

  // free the tail cells first so chasing a tail that is moving away is legal
  // trim to one less than the limit to leave room for the new head
  trailTrim(trailA, trailLen - 1);
  trailTrim(trailB, trailLen - 1);

  // old heads are still in grid, so swapping places is a crash for both
  deadA = blocked(nax, nay);
  deadB = blocked(nbx, nby);
  if (nax == nbx && nay == nby) { // head on collision
    deadA = true;
    deadB = true;
  }

  // move whoever survived
  if (!deadA) {
    ax = nax;
    ay = nay;
    grid[ax][ay] = TRAIL_A;
    trailPush(trailA, ax, ay);
  }
  if (!deadB) {
    bx = nbx;
    by = nby;
    grid[bx][by] = TRAIL_B;
    trailPush(trailB, bx, by);
  }

  // only the survivor scores, a draw gives no points
  if (deadA || deadB) {
    if (deadA && !deadB) {
      scoreB++;
    }
    if (deadB && !deadA) {
      scoreA++;
    }
    phase = CRASHED;
    phaseStart = millis();
  } else {
    // nobody crashed, so speed up a little
    if (tickMs > MIN_TICK_MS) {
      tickMs -= SPEEDUP_MS;
    }
  }
  sendState();
}

// reads board B's joystick packets which is 0xFE followed by a direction
void readFromClient() {
  static bool expectDir = false; // remembers between calls if a packet was cut in half
  while (Serial.available()) {
    byte ch = Serial.read();

    if (ch == PKT_INPUT) {
      expectDir = true; // next byte is the direction
      continue;
    }
    if (!expectDir) {
      continue; // leftover byte that isn't part of a packet
    }
    expectDir = false;

    if (ch > NONE) {
      continue; // not a real direction
    }

    // remember it for starting from the lobby
    lastBDir = ch;
    lastBInput = millis();

    // during a match it steers player B
    if (phase != LOBBY && ch < NONE) {
      if (!isOpposite(ch, dirB)) {
        pendB = ch;
      }
    }
  }
}

// board A's game logic which runs once every loop
void hostLoop() {
  unsigned long now = millis();

  // player A's joystick ignoring turns straight back into the trail
  byte dir = readJoyDir();
  if (phase != LOBBY && dir < NONE) {
    if (!isOpposite(dir, dirA)) {
      pendA = dir;
    }
  }
  readFromClient();

  // each phase waits for its time to run out then moves on
  switch (phase) {
    case LOBBY: {
      // either player holds their stick toward the other for 1s to start
      bool aHold = false;
      if (dir == RIGHT) {
        aHold = true;
      }
      // only count B's input if it's recent
      // millis() here since lastBInput can be newer than now
      bool bHold = false;
      if (lastBDir == LEFT && millis() - lastBInput < 200) {
        bHold = true;
      }

      if (aHold || bHold) {
        if (holdStart == 0) {
          holdStart = now; // just started holding
        } else if (now - holdStart >= HOLD_TO_START_MS) {
          startMatch();
        }
      } else {
        holdStart = 0; // let go, reset the hold
      }
      break;
    }
    case COUNTDOWN: {
      if (now - phaseStart >= COUNTDOWN_MS) {
        phase = PLAYING;
        lastTick = now;
        sendState();
      }
      break;
    }
    case PLAYING: {
      // move once every tickMs
      if (now - lastTick >= tickMs) {
        lastTick = now;
        stepGame();
      }
      break;
    }
    case CRASHED: {
      if (now - phaseStart >= CRASH_MS) {
        // match over if someone hit the winning score, otherwise next round
        if (scoreA >= WIN_SCORE || scoreB >= WIN_SCORE) {
          phase = GAMEOVER;
          phaseStart = now;
          sendState();
        } else {
          resetRound();
        }
      }
      break;
    }
    case GAMEOVER: {
      if (now - phaseStart >= GAMEOVER_MS) {
        enterLobby();
      }
      break;
    }
  }

  // resend even if nothing changed so board B can tell the link is still up
  if (millis() - lastSend >= SEND_EVERY_MS) {
    sendState();
  }
}

// *** client sends input and mirrors host state

// unpacks a state packet from board A into this board's variables
void applyState(const byte* buf) {
  byte sum = 0;
  for (byte i = 0; i < 40; i++) {
    sum += buf[i];
  }
  if ((sum % 128) != buf[40]) { // corrupted so wait for the next one
    return;
  }
  if (buf[0] > LOBBY) {
    return; // not a real phase
  }

  // restart the blink timing when the phase changes
  if (buf[0] != phase) {
    phaseStart = millis();
  }
  phase = buf[0];

  // buf[1] holds both death flags with 1 = A dead, 2 = B dead, and 3 = both
  if (buf[1] % 2 == 1) {
    deadA = true;
  } else {
    deadA = false;
  }
  if ((buf[1] / 2) % 2 == 1) {
    deadB = true;
  } else {
    deadB = false;
  }

  // head position and score values
  ax = buf[2];
  ay = buf[3];
  bx = buf[4];
  by = buf[5];
  scoreA = buf[6];
  scoreB = buf[7];

  // unpack the grid by dividing by the slot value moves that cell to the bottom, % 4 keeps just it
  for (byte x = 0; x < W; x++) {
    for (byte y = 0; y < H; y++) {
      byte i = x * 8 + y;
      byte packed = buf[8 + i / 4];
      grid[x][y] = (packed / SLOT_VALUE[i % 4]) % 4;
    }
  }
  lastPacket = millis(); // link is up
}

// collects bytes from board A into a full packet
void readFromHost() {
  // static so a packet split across two loops still gets put together
  static byte buf[STATE_LEN];
  static byte index = 0;
  static bool inPacket = false;

  while (Serial.available()) {
    byte ch = Serial.read();

    // 0xFF only ever shows up at the start of a packet
    if (ch == PKT_STATE) {
      inPacket = true;
      index = 0;
      continue;
    }
    if (!inPacket) {
      continue; // waiting for the start of the next packet
    }

    buf[index] = ch;
    index++;
    if (index == STATE_LEN) {
      inPacket = false;
      applyState(buf);
    }
  }
}

// board B's logic which runs once every loop
void clientLoop() {
  readFromHost(); // read first so lastPacket is never newer than now
  unsigned long now = millis();

  // send the joystick to board A every INPUT_EVERY_MS
  if (now - lastInputSend >= INPUT_EVERY_MS) {
    lastInputSend = now;
    if (Serial.availableForWrite() >= 2) {
      Serial.write(PKT_INPUT);
      Serial.write(readJoyDir());
    }
  }
}

// *** arduino

void setup() {
  // initialize row and col pins to off states
  for (byte i = 0; i < 8; i++) {
    pinMode(ANODE_PINS[i], OUTPUT);
    pinMode(CATHODE_PINS[i], OUTPUT);
    digitalWrite(ANODE_PINS[i], HIGH);
    digitalWrite(CATHODE_PINS[i], HIGH);
  }
  Serial.begin(115200); // both boards need the same baud rate
  if (IS_HOST) {
    enterLobby(); // host starts lobby state and client waits for packets
  }
}

void loop() {
  // step through the host or client logic for this board
  if (IS_HOST) {
    hostLoop();
  } else {
    clientLoop();
  }
  render(); // build what the board should look like
  display(ledOn); // light the LEDs for the given render pass
}
