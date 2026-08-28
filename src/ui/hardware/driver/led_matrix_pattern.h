#include <stdint.h>
#include <stdbool.h>

// 4x5 digit bitmaps
static const bool digits[10][5][4] = {
    // 0
    {
        {1,1,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,1,1,0}
    },
    // 1
    {
        {0,1,0,0},
        {0,1,0,0},
        {0,1,0,0},
        {0,1,0,0},
        {0,1,0,0}
    },
    // 2
    {
        {1,1,1,0},
        {0,0,1,0},
        {1,1,1,0},
        {1,0,0,0},
        {1,1,1,0}
    },
    // 3
    {
        {1,1,1,0},
        {0,0,1,0},
        {1,1,1,0},
        {0,0,1,0},
        {1,1,1,0}
    },
    // 4
    {
        {1,0,1,0},
        {1,0,1,0},
        {1,1,1,0},
        {0,0,1,0},
        {0,0,1,0}
    },
    // 5
    {
        {1,1,1,0},
        {1,0,0,0},
        {1,1,1,0},
        {0,0,1,0},
        {1,1,1,0}
    },
    // 6
    {
        {1,1,1,0},
        {1,0,0,0},
        {1,1,1,0},
        {1,0,1,0},
        {1,1,1,0}
    },
    // 7
    {
        {1,1,1,0},
        {0,0,1,0},
        {0,0,1,0},
        {0,0,1,0},
        {0,0,1,0}
    },
    // 8
    {
        {1,1,1,0},
        {1,0,1,0},
        {1,1,1,0},
        {1,0,1,0},
        {1,1,1,0}
    },
    // 9
    {
        {1,1,1,0},
        {1,0,1,0},
        {1,1,1,0},
        {0,0,1,0},
        {1,1,1,0}
    }
};

static inline void get_number_pattern(int *number, uint32_t (*buffer)[256], uint32_t *color) {
    if (*number < 0) *number = 0;
    if (*number > 999) *number = 999;
    
    int d[3];
    d[0] = *number / 100;
    d[1] = (*number / 10) % 10;
    d[2] = *number % 10;
    
    // Pattern layout (16 cols wide):
    // Col 0 (pad), 1-4 (d1), 5 (gap), 6-9 (d2), 10 (gap), 11-14 (d3), 15 (pad)
    // Rows 6-10: digits (5 font rows, 1:1 vertical)
    int col_starts[3] = {1, 6, 11};
    
    for (int row = 0; row < 5; row++) {
        int row_offset = (6 + row) * 16;
        
        for (int digit = 0; digit < 3; digit++) {
            for (int col = 0; col < 4; col++) {
                uint32_t val = digits[d[digit]][row][col] ? *color : 0;
                (*buffer)[row_offset + col_starts[digit] + col] = val;
            }
        }
    }
}



static const bool font[26][5][4] = {
    // a
    {
        {0,1,0,0},
        {1,0,1,0},
        {1,1,1,0},
        {1,0,1,0},
        {1,0,1,0}
    },
    // b
    {
        {1,1,0,0},
        {1,0,1,0},
        {1,1,0,0},
        {1,0,1,0},
        {1,1,0,0}
    },
    // c
    {
        {0,1,1,0},
        {1,0,0,0},
        {1,0,0,0},
        {1,0,0,0},
        {0,1,1,0}
    },
    // d
    {
        {1,1,0,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,1,0,0}
    },
    // e
    {
        {1,1,1,0},
        {1,0,0,0},
        {1,1,1,0},
        {1,0,0,0},
        {1,1,1,0}
    },
    // f
    {
        {1,1,1,0},
        {1,0,0,0},
        {1,1,1,0},
        {1,0,0,0},
        {1,0,0,0}
    },
    // g
    {
        {0,1,1,0},
        {1,0,0,0},
        {1,0,1,0},
        {1,0,1,0},
        {0,1,1,0}
    },
    // h
    {
        {1,0,1,0},
        {1,0,1,0},
        {1,1,1,0},
        {1,0,1,0},
        {1,0,1,0}
    },
    // i
    {
        {0,1,0,0},
        {0,1,0,0},
        {0,1,0,0},
        {0,1,0,0},
        {0,1,0,0}
    },
    // j
    {
        {0,0,1,0},
        {0,0,1,0},
        {0,0,1,0},
        {1,0,1,0},
        {0,1,1,0}
    },
    // k
    {
        {1,0,1,0},
        {1,0,1,0},
        {1,1,0,0},
        {1,0,1,0},
        {1,0,1,0}
    },
    // l
    {
        {1,0,0,0},
        {1,0,0,0},
        {1,0,0,0},
        {1,0,0,0},
        {1,1,1,0}
    },
    // m
    {
        {1,0,1,0},
        {1,1,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,0,1,0}
    },
    // n
    {
        {1,1,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,0,1,0}
    },
    // o
    {
        {0,1,0,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {0,1,0,0}
    },
    // p
    {
        {1,1,0,0},
        {1,0,1,0},
        {1,1,0,0},
        {1,0,0,0},
        {1,0,0,0}
    },
    // q
    {
        {0,1,0,0},
        {1,0,1,0},
        {1,0,1,0},
        {0,1,1,0},
        {0,0,1,0}
    },
    // r
    {
        {1,1,0,0},
        {1,0,1,0},
        {1,1,0,0},
        {1,1,0,0},
        {1,0,1,0}
    },
    // s
    {
        {0,1,1,0},
        {1,0,0,0},
        {0,1,0,0},
        {0,0,1,0},
        {1,1,0,0}
    },
    // t
    {
        {1,1,1,0},
        {0,1,0,0},
        {0,1,0,0},
        {0,1,0,0},
        {0,1,0,0}
    },
    // u
    {
        {1,0,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,1,1,0}
    },
    // v
    {
        {1,0,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {0,1,0,0}
    },
    // w
    {
        {1,0,1,0},
        {1,0,1,0},
        {1,0,1,0},
        {1,1,1,0},
        {1,0,1,0}
    },
    // x
    {
        {1,0,1,0},
        {0,1,0,0},
        {0,1,0,0},
        {0,1,0,0},
        {1,0,1,0}
    },
    // y
    {
        {1,0,1,0},
        {1,0,1,0},
        {0,1,0,0},
        {0,1,0,0},
        {0,1,0,0}
    },
    // z
    {
        {1,1,1,0},
        {0,0,1,0},
        {0,1,0,0},
        {1,0,0,0},
        {1,1,1,0}
    }
};

static inline void get_label_pattern(const char (*text)[4], uint32_t (*buffer)[256], uint32_t *color) {
    for (int pos = 0; pos < 4; pos++) {
        char c = (*text)[pos];
        if (c == '\0') c = ' '; // Fallback if string is short

        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if (c >= 'a' && c <= 'z') {
            int idx = c - 'a';
            for (int row = 0; row < 5; row++) {
                for (int col = 0; col < 4; col++) {
                    (*buffer)[row * 16 + pos * 4 + col] = font[idx][row][col] ? *color : 0;
                }
            }
        } else if (c >= '0' && c <= '9') {
            int idx = c - '0';
            for (int row = 0; row < 5; row++) {
                for (int col = 0; col < 4; col++) {
                    (*buffer)[row * 16 + pos * 4 + col] = digits[idx][row][col] ? *color : 0;
                }
            }
        }
    }
}

static inline void get_note_pattern(const char (*note_str)[4], uint32_t (*buffer)[256], uint32_t *color) {
    // Clear only rows 6-15 (10 rows * 16 columns)
    // memset(&(*buffer)[6 * 16], 0, 10 * 16 * sizeof(uint32_t));

    // Layout strategy:
    // Rows 6-10 (5 rows height, 1:1 vertical).
    // Col 0: Flat Dot.
    // Cols 1-4: Note (A-G), 1:1 scaling.
    // Col 8: Minus sign (for octave -1).
    // Cols 9-12: Octave (0-9), 1:1 scaling.
    // Col 15: Sharp Dot.

    int start_row = 6;

    // 1. Note (A-G) -> Cols 1-4
    char note = (*note_str)[0];
    if (note >= 'A' && note <= 'Z') note = note - 'A' + 'a';
    if (note >= 'a' && note <= 'z') {
         int idx = note - 'a';
         for (int row = 0; row < 5; row++) {
             int buf_row = start_row + row;
             for (int col = 0; col < 4; col++) {
                 uint32_t val = font[idx][row][col] ? *color : 0;
                 (*buffer)[buf_row * 16 + 1 + col] = val;
             }
         }
    }

    // 2. Octave (0-9) -> Cols 9-12
    char octave = (*note_str)[1];
    if (octave >= '0' && octave <= '9') {
        int idx = octave - '0';
        for (int row = 0; row < 5; row++) {
             int buf_row = start_row + row;
            for (int col = 0; col < 4; col++) {
                uint32_t val = digits[idx][row][col] ? *color : 0;
                (*buffer)[buf_row * 16 + 9 + col] = val;
            }
        }
    }

    // 3. Modifier (sharp/flat) -> Outer Columns (0 or 15)
    char mod = (*note_str)[2];
    bool is_sharp = (mod == '#' || mod == 's' || mod == 'S');
    bool is_flat = (mod == 'b' || mod == 'f' || mod == 'F');

    if (is_sharp) {
        // Sharp: Outer Right (Col 15), Top (row 6)
        int col = 15;
        (*buffer)[(start_row + 0) * 16 + col] = *color;
    } else if (is_flat) {
        // Flat: Outer Left (Col 0), Bottom (row 10)
        int col = 0;
        (*buffer)[(start_row + 4) * 16 + col] = *color;
    }

    // 4. Negative octave sign -> Col 8, middle row
    if ((*note_str)[3] == '-') {
        int row = start_row + 2;
        for (int col = 7; col <= 8; col++) {
            (*buffer)[row * 16 + col] = *color;
        }
    }
}

