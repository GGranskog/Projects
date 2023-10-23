#include "keyboard.h"
#include "i8259.h"
#include "lib.h"


/* keyboard irq number */
#define KEYBOARD_IRQ_NUM    1

#define KEYBOARD_DATA_PORT  (0x60)
#define KEYBOARD_CMD_PORT   (0x64)
#define SCANCODES_SIZE      (58)

#define BUFFER_MAX         (127)

#define LEFT_SHIFT_PRESS    (0x2A)
#define LEFT_SHIFT_RELEASE  (0xAA)
#define RIGHT_SHIFT_PRESS   (0x36)
#define RIGHT_SHIFT_RELEASE (0xB6)
#define CTRL_PRESS          (0x1D)
#define CTRL_RELEASE        (0x9D)
#define CAPS_PRESS          (0x3A)

#define BCKSPACE            (0x08)
#define ENTER               (0x0A)
//Regions containing letter characters
#define Q_UP_LIMIT          (0x10)
#define P_LOW_LIMIT         (0x19)
#define A_UP_LIMIT          (0x1E)
#define L_LOW_LIMIT         (0x26)
#define Z_UP_LIMIT          (0x2C)
#define M_LOW_LIMIT         (0x32)

#define BUFFER_SIZE   128


// Flags for modifier keys
uint8_t l_shift_flag  = 0;
uint8_t r_shift_flag  = 0;
uint8_t ctrl_flag   = 0;    //Can be up to two (for the right and left)
uint8_t caps_flag   = 0;


volatile static int enter_flag = 0;
static int char_count = 0;
static char char_buffer[BUFFER_SIZE];


// Count to keep track of the available number of backspaces
int num_char    = 0;

// Check for modifiers and return 1 if a modifier was pressed
int check_for_modifier(uint8_t scan_code);

/*
* Store every key on the keyboard and its alternate function in a lookup table
* This was generated by pressing every key and then pressing shift + the key
* This took forever to type
*/
char scan_to_ascii[SCANCODES_SIZE][2] = {
    {0x0, 0x0}, {0x0, 0x0},     // Nothing, Nothing
    {'1', '!'}, {'2', '@'},
    {'3', '#'}, {'4', '$'},
    {'5', '%'}, {'6', '^'},
    {'7', '&'}, {'8', '*'},
    {'9', '('}, {'0', ')'},
    {'-', '_'}, {'=', '+'},
    {BCKSPACE, BCKSPACE}, {' ', ' '},     // Backspace, Tab(treat as equivalent to SPACE)
    {'q', 'Q'}, {'w', 'W'},
    {'e', 'E'}, {'r', 'R'},
    {'t', 'T'}, {'y', 'Y'},
    {'u', 'U'}, {'i', 'I'},
    {'o', 'O'}, {'p', 'P'},
    {'[', '{'}, {']', '}'},
    {ENTER, ENTER}, {0x0, 0x0},     // Enter, Left Control
    {'a', 'A'}, {'s', 'S'},
    {'d', 'D'}, {'f', 'F'},
    {'g', 'G'}, {'h', 'H'},
    {'j', 'J'}, {'k', 'K'},
    {'l', 'L'}, {';', ':'},
    {'\'', '"'}, {'`', '~'},
    {0x0, 0x0}, {'\\', '|'},    // Left Shift
    {'z', 'Z'}, {'x', 'X'},
    {'c', 'C'}, {'v', 'V'},
    {'b', 'B'}, {'n', 'N'},
    {'m', 'M'}, {',', '<'},
    {'.', '>'}, {'/', '?'},
    {0x0, 0x0}, {0x0, 0x0},     // Right Shift, Nothing
    {0x0, 0x0}, {' ', ' '},     // Nothing

};


/* void keyboard_init(void);
 * Inputs: void
 * Return Value: none
 * Function: initialize keyboard by enabling irq 1 in pic */
void keyboard_init(void) {
    enable_irq(KEYBOARD_IRQ_NUM);
}

/* extern void keyboard_handler(void);
 * Inputs: void
 * Return Value: none
 * Function: reads input from keyboard dataport when an interrupt occurs and put it to video memory
 *           and terminal_read's buffer.
 */
extern void keyboard_handler(void) {
    // Start critical section
    cli();

    uint8_t scan_code = inb(KEYBOARD_DATA_PORT); // Read from keyboard

    if (check_for_modifier(scan_code)) {
        send_eoi(KEYBOARD_IRQ_NUM);
        sti();
        return;
    }

    if (scan_code >= SCANCODES_SIZE || scan_code <= 1) {
        send_eoi(KEYBOARD_IRQ_NUM);
        sti();
        return;
    }

    if (scan_to_ascii[scan_code][0] == '\n') {
        num_char = 0;
        putc(scan_to_ascii[scan_code][0]);
        get_char(scan_to_ascii[scan_code][0]);
    } else if (ctrl_flag > 0 && scan_to_ascii[scan_code][0] == 'l') {
        clear();
        uint16_t position = screen_y * NUM_COLS + screen_x;
        outb(0x0F, 0x3D4);
        outb((uint8_t)(position & 0xFF), 0x3D5);
        outb(0x0E, 0x3D4);
        outb((uint8_t)((position >> 8) & 0xFF), 0x3D5);
    } else if (scan_to_ascii[scan_code][0] == BCKSPACE) {
        if (num_char > 0) {
            putc(scan_to_ascii[scan_code][0]);
            get_char(scan_to_ascii[scan_code][0]);
            --num_char;
        }
    } else if ((l_shift_flag || r_shift_flag) && num_char < BUFFER_MAX) {
        if (caps_flag &&
            ((scan_code >= Q_UP_LIMIT && scan_code <= P_LOW_LIMIT) ||
             (scan_code >= A_UP_LIMIT && scan_code <= L_LOW_LIMIT) ||
             (scan_code >= Z_UP_LIMIT && scan_code <= M_LOW_LIMIT))) {
            putc(scan_to_ascii[scan_code][0]);
            get_char(scan_to_ascii[scan_code][0]);
            ++num_char;
        } else {
            putc(scan_to_ascii[scan_code][1]);
            get_char(scan_to_ascii[scan_code][1]);
            ++num_char;
        }
    } else if (caps_flag && num_char < BUFFER_MAX) {
        if ((scan_code >= Q_UP_LIMIT && scan_code <= P_LOW_LIMIT) ||
            (scan_code >= A_UP_LIMIT && scan_code <= L_LOW_LIMIT) ||
            (scan_code >= Z_UP_LIMIT && scan_code <= M_LOW_LIMIT)) {
            putc(scan_to_ascii[scan_code][1]);
            get_char(scan_to_ascii[scan_code][1]);
            ++num_char;
        } else {
            putc(scan_to_ascii[scan_code][0]);
            get_char(scan_to_ascii[scan_code][0]);
            ++num_char;
        }
    } else if (num_char < BUFFER_MAX) {
        putc(scan_to_ascii[scan_code][0]);
        get_char(scan_to_ascii[scan_code][0]);
        ++num_char;
    }

    send_eoi(KEYBOARD_IRQ_NUM);
    sti();
}


/* uint8_t check_for_modifier(uint8_t scan_code)
 * Inputs: scan_code - Index of scan code sent from keyboard
 * Return Value: 1 if modifier found, 0 otherwise
 * Function: Check if scan_code is a modifier key and update global flags */
int check_for_modifier(uint8_t scan_code) {
    switch(scan_code) {
        case LEFT_SHIFT_PRESS:
            l_shift_flag = 1;
            return 1;
        case LEFT_SHIFT_RELEASE:
            l_shift_flag = 0;
            return 1;
        case RIGHT_SHIFT_PRESS:
            r_shift_flag = 1;
            return 1;
        case RIGHT_SHIFT_RELEASE:
            r_shift_flag = 0;
            return 1;
        case CTRL_PRESS:
            ctrl_flag += 1;
            return 1;
        case CTRL_RELEASE:
            ctrl_flag -= 1;
            return 1;
        case CAPS_PRESS:
            caps_flag = !caps_flag;
            return 1;
        default:
            return 0;
    }
}

/* void get_char(char new_char);
 * Puts the most recently entered keyboard character into the char_buffer
 * for terminal_read and updates if enter has been pressed.
 *
 * Inputs: new_char - The character entered by the keyboard
 * Return Value: none
 * Side effects: char_buffer, char_count, and enter_flag are changed */
void get_char(char new_char) {
    // End the buffer with the newline and enable the enter_flag.
    if (new_char == '\n') {
        enter_flag = 1;
        char_buffer[char_count >= BUFFER_SIZE ? BUFFER_SIZE - 1 : char_count] = '\n';
    } else if (new_char == BCKSPACE) {
        if (char_count > 0) {

            putc(new_char);
            --char_count;
        }
    } else if (char_count < BUFFER_SIZE - 1) {
        char_buffer[char_count] = new_char;
        ++char_count;
    } else {
        ++char_count;
    }
}


/* int32_t terminal_read(int32_t fd, void* buf, int32_t nbytes);
 * Reads in keyboard presses and stores it in the char_buffer
 * until the newline is entered. It then stores the resulting char_buffer
 * in the buf array entered by the user.
 *
 * Inputs: fd - The file descriptor value.
 *        buf - The array that terminal read will be storing the entered
 *              keyboard text to.
 *     nbytes - The maximum number of characters that can be entered in buf.
 * Return Value: The number of bytes (characters) written to the buf array.
 * Side effects: char_buffer, char_count, and enter_flag are changed */
int32_t terminal_read(int32_t fd, void* buf, int32_t nbytes) {
    int bytes_read = 0;
    int i;

    //Fills in the buffer as the keyboard interrupts. Exits after newline.
    while(enter_flag == 0){}

    cli();
    //The max size of the buffer returned ranges from 1 to 128.
    if(nbytes < BUFFER_SIZE) {
        for(i = 0; i < nbytes; ++i) {
            //Fills in the user entered buffer with the resulting characters.
            ((char*) buf)[i] = char_buffer[i];
            char_buffer[i] = ' ';                   //Clears char_buffer
            //If it is smaller than nbytes it finishes here.
            if(((char*)buf)[i] == '\n') {
                bytes_read = i + 1;
                break;
            }
            //Makes sure that the last character in buf is a newline
            if((i == (nbytes - 1)) && (((char*)buf)[i] != '\n')){
                ((char*) buf)[i] = '\n';
                bytes_read = i + 1;
                break;
            }
        }
    } else {
        for(i = 0; i < BUFFER_SIZE; ++i) {
            //Fill in the user entered buffer
            ((char*) buf)[i] = char_buffer[i];
            char_buffer[i] = ' ';               //Clear char_buffer
            if(((char*)buf)[i] == '\n') {
                bytes_read = i + 1;
                break;
            }
        }
    }
    char_count = 0;  //Go back to the start of the char_buffer.
    enter_flag = 0;
    sti();

    return bytes_read;
}


int32_t terminal_write(int32_t fd, const void* buf, int32_t nbytes){
    int i;
    char curr_char;
    for(i = 0; i < nbytes; ++i) {
        //Prints the given characters to the screen.
        curr_char = ((char*) buf)[i];
        if(curr_char != '\0')           //Skips printing out the null terminator
            putc(curr_char);
    }
    return nbytes;
}

