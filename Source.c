/*
 * square5.c
 * this program lets you move the square, with vsync
 */

/* the width and height of the screen */
#define WIDTH 240
#define HEIGHT 160

/* these identifiers define different bit positions of the display control */
#define MODE4 0x0004
#define BG2 0x0400

/* this bit indicates whether to display the front or the back buffer
 * this allows us to refer to bit 4 of the display_control register */
#define SHOW_BACK 0x10;

/* the screen is simply a pointer into memory at a specific address this
 *  * pointer points to 16-bit colors of which there are 240x160 */
volatile unsigned short* screen = (volatile unsigned short*) 0x6000000;

/* the display control pointer points to the gba graphics register */
volatile unsigned long* display_control = (volatile unsigned long*) 0x4000000;

/* the address of the color palette used in graphics mode 4 */
volatile unsigned short* palette = (volatile unsigned short*) 0x5000000;

/* pointers to the front and back buffers - the front buffer is the start
 * of the screen array and the back buffer is a pointer to the second half */
volatile unsigned short* front_buffer = (volatile unsigned short*) 0x6000000;
volatile unsigned short* back_buffer = (volatile unsigned short*)  0x600A000;

/* the button register holds the bits which indicate whether each button has
 * been pressed - this has got to be volatile as well
 */
volatile unsigned short* buttons = (volatile unsigned short*) 0x04000130;

/* the bit positions indicate each button - the first bit is for A, second for
 * B, and so on, each constant below can be ANDED into the register to get the
 * status of any one button */
#define BUTTON_A (1 << 0)
#define BUTTON_B (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START (1 << 3)
#define BUTTON_RIGHT (1 << 4)
#define BUTTON_LEFT (1 << 5)
#define BUTTON_UP (1 << 6)
#define BUTTON_DOWN (1 << 7)
#define BUTTON_R (1 << 8)
#define BUTTON_L (1 << 9)

/* the scanline counter is a memory cell which is updated to indicate how
 * much of the screen has been drawn */
volatile unsigned short* scanline_counter = (volatile unsigned short*) 0x4000006;

//add scoring variables
int player_score = 0;
int ai_score = 0;

/* wait for the screen to be fully drawn so we can do something during vblank */
void wait_vblank() {
    /* wait until all 160 lines have been updated */
    while (*scanline_counter < 160) { }
}

/* this function checks whether a particular button has been pressed */
unsigned char button_pressed(unsigned short button) {
    /* and the button register with the button constant we want */
    unsigned short pressed = *buttons & button;

    /* if this value is zero, then it's not pressed */
    if (pressed == 0) {
        return 1;
    } else {
        return 0;
    }
}

/* keep track of the next palette index */
int next_palette_index = 0;

/*
 * function which adds a color to the palette and returns the
 * index to it
 */
unsigned char add_color(unsigned char r, unsigned char g, unsigned char b) {
    unsigned short color = b << 10;
    color += g << 5;
    color += r;

    /* add the color to the palette */
    palette[next_palette_index] = color;

    /* increment the index */
    next_palette_index++;

    /* return index of color just added */
    return next_palette_index - 1;
}

/* a colored square */
struct square {
    unsigned short x, y, size;
    unsigned char color;
};
struct paddle {
    unsigned short x, y, width, height;
    unsigned char color;
};
struct ball {
    int x, y, size, dx, dy;
    unsigned char color;
};

/* put a pixel on the screen in mode 4 */
void put_pixel(volatile unsigned short* buffer, int row, int col, unsigned char color) {
    /* find the offset which is the regular offset divided by two */
    unsigned short offset = (row * WIDTH + col) >> 1;

    /* read the existing pixel which is there */
    unsigned short pixel = buffer[offset];

    /* if it's an odd column */
    if (col & 1) {
        /* put it in the left half of the short */
        buffer[offset] = (color << 8) | (pixel & 0x00ff);
    } else {
        /* it's even, put it in the left half */
        buffer[offset] = (pixel & 0xff00) | color;
    }
}

/* draw a square onto the screen */
void draw_square(volatile unsigned short* buffer, struct square* s) {
    short row, col;
    /* for each row of the square */
    for (row = s->y; row < (s->y + s->size); row++) {
        /* loop through each column of the square */
        for (col = s->x; col < (s->x + s->size); col++) {
            /* set the screen location to this color */
            put_pixel(buffer, row, col, s->color);
        }
    }
}

/* clear the screen right around the square */
/* this function takes a video buffer and returns to you the other one */
void update_screen(volatile unsigned short* buffer, unsigned short color, struct paddle* p, struct paddle* ai_paddle, unsigned char net_color, struct ball* b) {
    short row, col;
    
    // Clear the area around the player paddle
    for (row = p->y - 3; row < (p->y + p->height + 3); row++) {
        for (col = p->x - 3; col < (p->x + p->width + 3); col++) {
            put_pixel(buffer, row, col, color);
        }
    }

    // Clear the area around the AI paddle
    for (row = ai_paddle->y - 3; row < (ai_paddle->y + ai_paddle->height + 3); row++) {
        for (col = ai_paddle->x - 3; col < (ai_paddle->x + ai_paddle->width + 3); col++) {
            put_pixel(buffer, row, col, color);
        }
    }

    // Clear both the current ball position AND the screen edges
    for (row = b->y - 3; row < (b->y + b->size + 3); row++) {
        for (col = b->x - 3; col < (b->x + b->size + 3); col++) {
            put_pixel(buffer, row, col, color);
        }
    }
    
    // Additionally clear the left and right edges where the ball might get stuck
    for (row = 0; row < HEIGHT; row++) {
        for (col = 0; col < 3; col++) {
            put_pixel(buffer, row, col, color);  // Clear left edge
            put_pixel(buffer, row, WIDTH-1-col, color);  // Clear right edge
        }
    }

    // Draw the net down the middle of the screen
    for (row = 0; row < HEIGHT; row += 4) {
        put_pixel(buffer, row, WIDTH / 2, net_color);
    }
}

volatile unsigned short* flip_buffers(volatile unsigned short* buffer) {
    /* if the back buffer is up, return that */
    if(buffer == front_buffer) {
        /* clear back buffer bit and return back buffer pointer */
        *display_control &= ~SHOW_BACK;
        return back_buffer;
    } else {
        /* set back buffer bit and return front buffer */
        *display_control |= SHOW_BACK;
        return front_buffer;
    }
}

/* handle the buttons which are pressed down */
void handle_buttons(struct paddle* p) {
      /* move the paddle with the arrow keys */
    if (button_pressed(BUTTON_DOWN) && (p->y + p->height < HEIGHT)) {
        p->y += 1;
    }
    if (button_pressed(BUTTON_UP) && p->y > 0) {
        p->y -= 1;
    }
}

/* clear the screen to black */
void clear_screen(volatile unsigned short* buffer, unsigned short color) {
    unsigned short row, col;
    /* set each pixel black */
    for (row = 0; row < HEIGHT; row++) {
        for (col = 0; col < WIDTH; col++) {
            put_pixel(buffer, row, col, color);
        }
    }
}


//start of the new methods

void draw_paddle(volatile unsigned short* buffer, struct paddle* p) {
    short row, col;
    /* for each row of the paddle */
    for (row = p->y; row < (p->y + p->height); row++) {
        /* loop through each column of the paddle */
        for (col = p->x; col < (p->x + p->width); col++) {
            /* set the screen location to this color */
            put_pixel(buffer, row, col, p->color);
        }
    }
}

void update_ai_paddle(struct paddle* ai_paddle) {
    static int ai_direction = 1; // 1 for moving down, -1 for moving up

    /* Move the AI paddle */
    ai_paddle->y += ai_direction;

    /* Reverse direction if it hits the top or bottom */
    if (ai_paddle->y <= 0) {
        ai_direction = 1; // Move down
    } else if (ai_paddle->y + ai_paddle->height >= HEIGHT) {
        ai_direction = -1; // Move up
    }
}

void draw_ball(volatile unsigned short* buffer, struct ball* b) {
    put_pixel(buffer, b->y, b->x, b->color);
}

void update_ball(struct ball* b, struct paddle* player, struct paddle* ai_paddle) {
     // Update position
    b->x += b->dx;
    b->y += b->dy;

    // Check collision with top and bottom
    if (b->y <= 0 || b->y >= HEIGHT) {
        b->dy = -b->dy; // Reverse direction on collision with top/bottom
    }
    // Check for scoring and reset ball
    if (b->x <= 0) {  // AI scores
        ai_score++;
        b->x = WIDTH / 2;
        b->y = HEIGHT / 2;
    }
    if (b->x >= WIDTH) {  // Player scores
        player_score++;
        b->x = WIDTH / 2;
        b->y = HEIGHT / 2;
    }
    // Check collision with paddles
     // Player paddle collision (left paddle)
    if (b->x <= player->x + player->width &&  // Ball's right edge past paddle's left edge
        b->x >= player->x &&                  // Ball's left edge before paddle's right edge
        b->y >= player->y &&                  // Ball's top edge below paddle's top edge
        b->y <= player->y + player->height) { // Ball's bottom edge above paddle's bottom edge
        b->dx = -b->dx;                       // Reverse horizontal direction
        
    }

    // AI paddle collision (right paddle)
    if (b->x + b->size >= ai_paddle->x &&    // Ball's right edge past paddle's left edge
        b->x <= ai_paddle->x + ai_paddle->width && // Ball's left edge before paddle's right edge
        b->y >= ai_paddle->y &&              // Ball's top edge below paddle's top edge
        b->y <= ai_paddle->y + ai_paddle->height) { // Ball's bottom edge above paddle's bottom edge
        b->dx = -b->dx;                      // Reverse horizontal direction
        
    }
}
/* the main function */
int main() {
    /* we set the mode to mode 4 with bg2 on */
    *display_control = MODE4 | BG2;

    //create gray paddle
    struct paddle player = {20,60,5,25, add_color(15,15,15)};

    //create the ai paddle
    struct paddle ai_paddle = {WIDTH - 15, 10, 5, 30, add_color(15, 15, 15)};

    //create the ball
    struct ball b = {WIDTH / 2, HEIGHT / 2, 5, 1, 1, add_color(15, 15, 15)};
    
    /* add black to the palette */
    unsigned char black = add_color(0, 0, 0);

    //create the net color
    unsigned char net_color = add_color(15,15,15);

    /* the buffer we start with */
    volatile unsigned short* buffer = front_buffer;

    /* clear whole screen first */
    clear_screen(front_buffer, black);
    clear_screen(back_buffer, black);

    /* loop forever */
    while (1) {
        /* clear the previous positions of objects */
        update_screen(buffer, black, &player, &ai_paddle, net_color, &b);

        /* handle button input for the player */
        handle_buttons(&player);

        /* update the AI paddle's position */
        update_ai_paddle(&ai_paddle);

        /* update ball position */
        update_ball(&b, &player, &ai_paddle);

        /* draw paddles and ball */
        draw_paddle(buffer, &player);
        draw_paddle(buffer, &ai_paddle);
        draw_ball(buffer, &b);

        /* wait for vblank before switching buffers */
        wait_vblank();

        /* swap the buffers */
        buffer = flip_buffers(buffer);
    }
}

