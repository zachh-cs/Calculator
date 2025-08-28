// pong.cpp — console Pong (C++17) with smooth paddle movement on Windows
// Controls: P1=W/S, P2=Up/Down, R=reset, Q=quit

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>

#ifdef _WIN32
  #include <conio.h>
  #include <windows.h>
#else
  #include <termios.h>
  #include <unistd.h>
  #include <fcntl.h>
#endif

// --------------------- Game constants ---------------------

static const int WIDTH  = 80;
static const int HEIGHT = 24;

static const int PADDLE_H = 4;
static const int LEFT_X   = 2;
static const int RIGHT_X  = WIDTH - 3;

static const float BALL_SPEED   = 32.0f; // cells/sec
static const float BALL_SPEEDUP = 1.03f; // on paddle hit
static const float MAX_DY       = 1.25f; // clamp vertical speed

static const float PADDLE_SPEED = 48.0f; // cells/sec (tweak 48–80)

// --------------------- Cross-platform input & screen ---------------------

#ifdef _WIN32
bool kb_hit() { return _kbhit(); }
int  kb_getch() { return _getch(); }

void enable_vt() {
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut == INVALID_HANDLE_VALUE) return;
  DWORD mode = 0;
  if (!GetConsoleMode(hOut, &mode)) return;
  mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(hOut, mode);
}

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

#else
// POSIX non-blocking keyboard
struct TermRestore {
  termios oldt{};
  bool ok = false;
  TermRestore() {
    if (tcgetattr(STDIN_FILENO, &oldt) == 0) {
      termios newt = oldt;
      newt.c_lflag &= ~(ICANON | ECHO);
      tcsetattr(STDIN_FILENO, TCSANOW, &newt);
      int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
      fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
      ok = true;
    }
  }
  ~TermRestore() {
    if (ok) tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  }
} term_restore;

bool kb_hit() {
  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) == 1) {
    ungetc(c, stdin);
    return true;
  }
  return false;
}

int kb_getch() {
  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) == 1) return c;
  return -1;
}

void enable_vt() {} // POSIX terminals already support ANSI
void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
#endif

// --------------------- Helpers ---------------------

struct Ball {
  float x, y, vx, vy;
};

struct Paddle {
  int x;
  float y; // top position
};

void clear_and_home_once() {
  std::cout << "\x1b[2J\x1b[H";
}

void home_cursor() {
  std::cout << "\x1b[H";
}

void draw_frame(const std::vector<char>& buf) {
  home_cursor();
  for (int r = 0; r < HEIGHT; ++r) {
    std::cout.write(&buf[r * WIDTH], WIDTH);
    std::cout.put('\n');
  }
  std::cout.flush();
}

void put_char(std::vector<char>& b, int x, int y, char c) {
  if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) b[y * WIDTH + x] = c;
}

void reset_round(Ball& ball, int lastScorer /*0 left, 1 right, -1 start*/) {
  ball.x = WIDTH / 2.0f;
  ball.y = HEIGHT / 2.0f;
  int dir = (lastScorer == 0 ? 1 : (lastScorer == 1 ? -1 : (rand()%2?1:-1)));
  float angle = (rand() % 1000) / 1000.0f * 0.8f - 0.4f; // small random angle
  ball.vx = dir * BALL_SPEED * 0.8f;
  ball.vy = BALL_SPEED * angle;
}

void clamp_paddle(Paddle& p) {
  p.y = std::max(1.0f, std::min<float>(p.y, HEIGHT - 2 - PADDLE_H));
}

bool ball_hits_paddle(const Ball& ball, const Paddle& p) {
  if (std::round(ball.x) == p.x) {
    int by = (int)std::round(ball.y);
    int py = (int)std::round(p.y);
    if (by >= py && by < py + PADDLE_H) return true;
  }
  return false;
}

void draw_ui(std::vector<char>& buf, int scoreL, int scoreR, bool gameOver) {
  for (int x = 0; x < WIDTH; ++x) {
    put_char(buf, x, 0, '=');
    put_char(buf, x, HEIGHT - 1, '=');
  }
  put_char(buf, 0, 0, '+'); put_char(buf, WIDTH-1, 0, '+');
  put_char(buf, 0, HEIGHT-1, '+'); put_char(buf, WIDTH-1, HEIGHT-1, '+');

  auto write_text = [&](int x, int y, const std::string& s) {
    for (size_t i = 0; i < s.size() && x + (int)i < WIDTH; ++i) put_char(buf, x + (int)i, y, s[i]);
  };

  std::string title = "PONG  |  P1: W/S   P2: Up/Down   R=Reset   Q=Quit";
  write_text((WIDTH - (int)title.size()) / 2, 0, title);

  std::string score = std::to_string(scoreL) + " : " + std::to_string(scoreR);
  write_text((WIDTH - (int)score.size())/2, 1, score);

  if (gameOver) {
    std::string msg = "Game Over! Press R to restart or Q to quit.";
    write_text((WIDTH - (int)msg.size())/2, HEIGHT/2, msg);
  }
}

int main() {
  std::srand((unsigned)std::time(nullptr));
  enable_vt();
  clear_and_home_once();

  std::vector<char> buffer(WIDTH * HEIGHT, ' ');

  Paddle left  { LEFT_X,  (HEIGHT - PADDLE_H) / 2.0f };
  Paddle right { RIGHT_X, (HEIGHT - PADDLE_H) / 2.0f };
  Ball ball{};
  int scoreL = 0, scoreR = 0;
  bool gameOver = false;
  const int WIN_SCORE = 11;

  reset_round(ball, -1);

  auto last = std::chrono::steady_clock::now();

  while (true) {
    // --- timing ---
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> dtDur = now - last;
    float dt = dtDur.count();
    if (dt > 0.05f) dt = 0.05f; // clamp big pauses
    last = now;

    // --- discrete input (single-press) ---
    int ch = -1;
    while (kb_hit()) {
      ch = kb_getch();
      if (ch == 'q' || ch == 'Q') return 0;
      if (ch == 'r' || ch == 'R') {
        scoreL = scoreR = 0;
        gameOver = false;
        left.y  = (HEIGHT - PADDLE_H) / 2.0f;
        right.y = (HEIGHT - PADDLE_H) / 2.0f;
        reset_round(ball, -1);
      }
#ifndef _WIN32
      if (ch == 27) { // ESC [
        int c1 = kb_getch();
        int c2 = kb_getch();
        if (c1 == 91) { // '['
          if (c2 == 'A') ch = 1000; // Up
          else if (c2 == 'B') ch = 1001; // Down
        }
      }
#endif
      // fallback single-step for portability
      if (ch == 'w' || ch == 'W') left.y -= 1;
      if (ch == 's' || ch == 'S') left.y += 1;
#ifdef _WIN32
      if (ch == 224) {
        int kc = kb_getch();
        if (kc == 72) right.y -= 1; // Up
        if (kc == 80) right.y += 1; // Down
      }
#else
      if (ch == 1000) right.y -= 1;
      if (ch == 1001) right.y += 1;
#endif
    }

    clamp_paddle(left);
    clamp_paddle(right);

    // --- continuous input (held keys) — Windows only ---
#ifdef _WIN32
    if (GetAsyncKeyState('W') & 0x8000)       left.y  -= PADDLE_SPEED * dt;
    if (GetAsyncKeyState('S') & 0x8000)       left.y  += PADDLE_SPEED * dt;
    if (GetAsyncKeyState(VK_UP) & 0x8000)     right.y -= PADDLE_SPEED * dt;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000)   right.y += PADDLE_SPEED * dt;
    clamp_paddle(left);
    clamp_paddle(right);
#endif

    // --- update (if not gameOver) ---
    if (!gameOver) {
      ball.x += ball.vx * dt;
      ball.y += ball.vy * dt;

      // top/bottom collisions
      if (ball.y < 1) { ball.y = 1; ball.vy = -ball.vy; }
      if (ball.y > HEIGHT - 2) { ball.y = HEIGHT - 2; ball.vy = -ball.vy; }

      // paddle collisions
      bool hitL = false, hitR = false;
      if (ball_hits_paddle(ball, left) && ball.vx < 0)  hitL = true;
      if (ball_hits_paddle(ball, right) && ball.vx > 0) hitR = true;

      if (hitL || hitR) {
        const Paddle& p = hitL ? left : right;
        int py = (int)std::round(p.y);
        float centerOffset = (ball.y - (py + PADDLE_H / 2.0f)) / (PADDLE_H / 2.0f);
        ball.vx = (hitL ? std::abs(ball.vx) : -std::abs(ball.vx)) * BALL_SPEEDUP;
        ball.vy = std::clamp(ball.vy + centerOffset * 10.0f,
                             -MAX_DY * BALL_SPEED * 0.5f,
                              MAX_DY * BALL_SPEED * 0.5f);
        ball.x += (hitL ? 1.0f : -1.0f); // nudge to avoid double-hit
      }

      // scoring
      if (ball.x < 0) {
        scoreR++;
        if (scoreR >= WIN_SCORE) gameOver = true;
        reset_round(ball, 1); // right scored
      } else if (ball.x >= WIDTH) {
        scoreL++;
        if (scoreL >= WIN_SCORE) gameOver = true;
        reset_round(ball, 0); // left scored
      }
    }

    // --- render ---
    std::fill(buffer.begin(), buffer.end(), ' ');

    // UI & border
    draw_ui(buffer, scoreL, scoreR, gameOver);

    // middle divider
    for (int y = 1; y < HEIGHT - 1; ++y) {
      if (y % 2 == 0) put_char(buffer, WIDTH / 2, y, '|');
    }

    // paddles
    for (int i = 0; i < PADDLE_H; ++i) {
      put_char(buffer, left.x,  (int)std::round(left.y) + i, '#');
      put_char(buffer, right.x, (int)std::round(right.y) + i, '#');
    }

    // ball
    put_char(buffer, (int)std::round(ball.x), (int)std::round(ball.y), 'O');

    draw_frame(buffer);

    // frame pacing (try ~120 FPS for snappier feel)
    // sleep_ms(16); // ~60 FPS
    sleep_ms(8);     // ~120 FPS
  }
  return 0;
}
