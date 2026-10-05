// PC keyboard interface constants
#pragma once

constexpr ushort KBSTATP = 0x64; // kbd controller status port(I)
constexpr uchar KBS_DIB = 0x01;  // kbd data in buffer
constexpr ushort KBDATAP = 0x60; // kbd data port(I)

constexpr uchar NO = 0;

constexpr uint SHIFT = 1 << 0;
constexpr uint CTL = 1 << 1;
constexpr uint ALT = 1 << 2;

constexpr uint CAPSLOCK = 1 << 3;
constexpr uint NUMLOCK = 1 << 4;
constexpr uint SCROLLLOCK = 1 << 5;

constexpr uint E0ESC = 1 << 6;

// Special keycodes
constexpr uchar KEY_HOME = 0xE0;
constexpr uchar KEY_END = 0xE1;
constexpr uchar KEY_UP = 0xE2;
constexpr uchar KEY_DN = 0xE3;
constexpr uchar KEY_LF = 0xE4;
constexpr uchar KEY_RT = 0xE5;
constexpr uchar KEY_PGUP = 0xE6;
constexpr uchar KEY_PGDN = 0xE7;
constexpr uchar KEY_INS = 0xE8;
constexpr uchar KEY_DEL = 0xE9;

// C('A') == Control-A
constexpr uchar
C(char x)
{
  return x - '@';
}

// the C version fills its 256-entry tables with C99 designated
// initializers, such as { [0x1D] CTL, [0x2A] SHIFT }, which C++
// does not have for arrays. instead, makemap() builds each table
// at compile time from the scan codes 0x00, 0x01, ... in order,
// followed by (scan code, value) pairs for the rest.
struct Keymap {
  uchar code[256];
  constexpr uchar operator[](uint i) const { return code[i]; }
};

struct KeyAt {
  uchar scancode;
  uchar value;
};

template <int M>
constexpr Keymap
makemap(const KeyAt (&at)[M])
{
  Keymap m{};
  for (int i = 0; i < M; i++)
    m.code[at[i].scancode] = at[i].value;
  return m;
}

// the same, plus a second list of pairs shared by several maps.
template <int N, int M, int K>
constexpr Keymap
makemap(const uchar (&first)[N], const KeyAt (&at)[M], const KeyAt (&more)[K])
{
  Keymap m = makemap(at);
  for (int i = 0; i < K; i++)
    m.code[more[i].scancode] = more[i].value;
  for (int i = 0; i < N; i++)
    m.code[i] = first[i];
  return m;
}

constexpr Keymap shiftcode = makemap({
  {0x1D, CTL},
  {0x2A, SHIFT},
  {0x36, SHIFT},
  {0x38, ALT},
  {0x9D, CTL},
  {0xB8, ALT},
});

constexpr Keymap togglecode = makemap({
  {0x3A, CAPSLOCK},
  {0x45, NUMLOCK},
  {0x46, SCROLLLOCK},
});

// keys after an E0 escape (scan code | 0x80), the same in every map.
constexpr KeyAt e0keys[] = {
  {0xC8, KEY_UP},  {0xD0, KEY_DN},   {0xC9, KEY_PGUP}, {0xD1, KEY_PGDN},
  {0xCB, KEY_LF},  {0xCD, KEY_RT},   {0x97, KEY_HOME}, {0xCF, KEY_END},
  {0xD2, KEY_INS}, {0xD3, KEY_DEL},
};

constexpr Keymap normalmap = makemap(
  {
    NO,   0x1B, '1',  '2',  '3',  '4',  '5',  '6',  // 0x00
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  // 0x10
    'o',  'p',  '[',  ']',  '\n', NO,   'a',  's',
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 0x20
    '\'', '`',  NO,   '\\', 'z',  'x',  'c',  'v',
    'b',  'n',  'm',  ',',  '.',  '/',  NO,   '*',  // 0x30
    NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
    NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',  // 0x40
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,   // 0x50
  },
  {
    {0x9C, '\n'}, // KP_Enter
    {0xB5, '/'},  // KP_Div
  },
  e0keys);

constexpr Keymap shiftmap = makemap(
  {
    NO,   033,  '!',  '@',  '#',  '$',  '%',  '^',  // 0x00
    '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  // 0x10
    'O',  'P',  '{',  '}',  '\n', NO,   'A',  'S',
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  // 0x20
    '"',  '~',  NO,   '|',  'Z',  'X',  'C',  'V',
    'B',  'N',  'M',  '<',  '>',  '?',  NO,   '*',  // 0x30
    NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
    NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',  // 0x40
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,   // 0x50
  },
  {
    {0x9C, '\n'}, // KP_Enter
    {0xB5, '/'},  // KP_Div
  },
  e0keys);

constexpr Keymap ctlmap = makemap(
  {
    NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO,
    NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO,
    C('Q'),  C('W'),  C('E'),  C('R'),  C('T'),  C('Y'),  C('U'),  C('I'),
    C('O'),  C('P'),  NO,      NO,      '\r',    NO,      C('A'),  C('S'),
    C('D'),  C('F'),  C('G'),  C('H'),  C('J'),  C('K'),  C('L'),  NO,
    NO,      NO,      NO,      C('\\'), C('Z'),  C('X'),  C('C'),  C('V'),
    C('B'),  C('N'),  C('M'),  NO,      NO,      C('/'),  NO,      NO,
  },
  {
    {0x9C, '\r'},   // KP_Enter
    {0xB5, C('/')}, // KP_Div
  },
  e0keys);

