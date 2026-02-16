#include <stdint.h>
#include <stdbool.h>

// 6x5 digit bitmaps
static const bool digits[10][5][6] = {
    // 0
    {
        {1,1,1,1,1,1},
        {1,1,0,0,1,1},
        {1,1,0,0,1,1},
        {1,1,0,0,1,1},
        {1,1,1,1,1,1}
    },
    // 1
    {
        {0,0,1,1,0,0},
        {0,0,1,1,0,0},
        {0,0,1,1,0,0},
        {0,0,1,1,0,0},
        {0,0,1,1,0,0}
    },
    // 2
    {
        {1,1,1,1,1,1},
        {0,0,0,0,1,1},
        {0,0,1,1,0,0},
        {1,1,0,0,0,0},
        {1,1,1,1,1,1}
    },
    // 3
    {
        {1,1,1,1,1,1},
        {0,0,0,0,1,1},
        {1,1,1,1,1,1},
        {0,0,0,0,1,1},
        {1,1,1,1,1,1}
    },
    // 4
    {
        {1,1,0,0,1,1},
        {1,1,0,0,1,1},
        {1,1,1,1,1,1},
        {0,0,0,0,1,1},
        {0,0,0,0,1,1}
    },
    // 5
    {
        {1,1,1,1,1,1},
        {1,1,0,0,0,0},
        {1,1,1,1,1,1},
        {0,0,0,0,1,1},
        {1,1,1,1,1,1}
    },
    // 6
    {
        {1,1,1,1,1,1},
        {1,1,0,0,0,0},
        {1,1,1,1,1,1},
        {1,1,0,0,1,1},
        {1,1,1,1,1,1}
    },
    // 7
    {
        {1,1,1,1,1,1},
        {0,0,0,0,1,1},
        {0,0,0,0,1,1},
        {0,0,0,0,1,1},
        {0,0,0,0,1,1}
    },
    // 8
    {
        {1,1,1,1,1,1},
        {1,1,0,0,1,1},
        {1,1,1,1,1,1},
        {1,1,0,0,1,1},
        {1,1,1,1,1,1}
    },
    // 9
    {
        {1,1,1,1,1,1},
        {1,1,0,0,1,1},
        {1,1,1,1,1,1},
        {0,0,0,0,1,1},
        {1,1,1,1,1,1}
    }
};

static inline void get_number_pattern(int *number, uint32_t (*buffer)[256], uint32_t *color) {
    // memset(buffer, 0, 256 * sizeof(uint32_t));
    
    if (*number < 0) *number = 0;
    if (*number > 99) *number = 99;
    
    int left_digit = *number / 10;
    int right_digit = *number % 10;
    
    // Pattern layout:
    // Rows 0-2: Empty
    // Rows 3-12: Digits (10 rows)
    // Rows 13-15: Empty
    // Cols: 0 (pad), 1-6 (left), 7-8 (pad), 9-14 (right), 15 (pad)
    
    for (int row = 0; row < 5; row++) {
        // Each row in font is mapped to 2 rows in buffer (vertical scaling)
        int buf_row_a = 6 + row * 2;
        int buf_row_b = 6 + row * 2 + 1;
        int row_offset_a = buf_row_a * 16;
        int row_offset_b = buf_row_b * 16;
        
        // Left Digit
        for (int col = 0; col < 6; col++) {
            uint32_t val = digits[left_digit][row][col] ? *color : 0;
            (*buffer)[row_offset_a + 1 + col] = val;
            (*buffer)[row_offset_b + 1 + col] = val;
        }
        
        // Right Digit
        for (int col = 0; col < 6; col++) {
            uint32_t val = digits[right_digit][row][col] ? *color : 0;
            (*buffer)[row_offset_a + 9 + col] = val;
            (*buffer)[row_offset_b + 9 + col] = val;
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
        // if (c < 'a' || c > 'z') c = 'a';
        if (c < 'a' || c > 'z') continue;
        int idx = c - 'a';

        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 4; col++) {
                (*buffer)[row * 16 + pos * 4 + col] = font[idx][row][col] ? *color : 0;
            }
        }
    }
}

static inline void get_note_pattern(const char (*note_str)[3], uint32_t (*buffer)[256], uint32_t *color) {
    // Clear only rows 6-15 (10 rows * 16 columns)
    // memset(&(*buffer)[6 * 16], 0, 10 * 16 * sizeof(uint32_t));
    
    // Layout strategy:
    // Rows 6-15 (10 rows height).
    // Col 0: Flat Dot.
    // Cols 1-8: Note (A-G), scaled 2x vertical, 2x horizontal.
    // Cols 9-14: Octave (0-9), using full 10x6 digits font.
    // Col 15: Sharp Dot.

    int start_row = 6; 

    // 1. Note (A-G) -> Cols 1-8
    char note = (*note_str)[0];
    if (note >= 'A' && note <= 'Z') note = note - 'A' + 'a';
    if (note >= 'a' && note <= 'z') {
         int idx = note - 'a';
         for (int row = 0; row < 5; row++) {
             // Scale 5-row font to 10 rows (2x vertical)
             int buf_row_a = start_row + row * 2;
             int buf_row_b = start_row + row * 2 + 1;
             
             for (int col = 0; col < 4; col++) {
                 // Scale 4-col font to 8 cols (2x horizontal)
                 // Start at Col 1
                 int buf_col_a = 1 + col * 2;
                 int buf_col_b = 1 + col * 2 + 1;
                 
                 uint32_t val = font[idx][row][col] ? *color : 0;
                 
                 // Fill 2x2 block
                 (*buffer)[buf_row_a * 16 + buf_col_a] = val;
                 (*buffer)[buf_row_a * 16 + buf_col_b] = val;
                 (*buffer)[buf_row_b * 16 + buf_col_a] = val;
                 (*buffer)[buf_row_b * 16 + buf_col_b] = val;
             }
         }
    }

    // 2. Octave (0-9) -> Cols 9-14
    char octave = (*note_str)[1];
    if (octave >= '0' && octave <= '9') {
        int idx = octave - '0';
        for (int row = 0; row < 5; row++) {
             int buf_row_a = start_row + row * 2;
             int buf_row_b = start_row + row * 2 + 1;
            for (int col = 0; col < 6; col++) {
                // Right digit starts at col 9
                uint32_t val = digits[idx][row][col] ? *color : 0;
                (*buffer)[buf_row_a * 16 + 9 + col] = val;
                (*buffer)[buf_row_b * 16 + 9 + col] = val;
            }
        }
    }

    // 3. Modifier (sharp/flat) -> Outer Columns (0 or 15)
    char mod = (*note_str)[2];
    bool is_sharp = (mod == '#' || mod == 's' || mod == 'S');
    bool is_flat = (mod == 'b' || mod == 'f' || mod == 'F');
    
    if (is_sharp) {
        // Sharp: Outer Right (Col 15), Top (rows 6-7)
        int col = 15;
        (*buffer)[(start_row + 0) * 16 + col] = *color;
        (*buffer)[(start_row + 1) * 16 + col] = 0;
        // Make it a bit taller/substantial? 2 pixels high is fine for a dot.
    } else if (is_flat) {
        // Flat: Outer Left (Col 0), Bottom (rows 14-15)
        int col = 0;
        (*buffer)[(start_row + 8) * 16 + col] = 0;
        (*buffer)[(start_row + 9) * 16 + col] = *color;
    }
}

