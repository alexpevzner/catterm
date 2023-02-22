/*
 * Name:        catterm
 * Copyright:   Alexander Pevzner <pzz@pzz.msk.ru>
 * License:     Free for any use
 * Warranty:    None. Program distributed AS IS.
 * Description: catterm is a minimalist terminal emulation program.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/types.h>

#define DEFAULT_ESC_CHAR        "X"

/***** Static variables -- options *****/
static unsigned long            opt_tty_speed = 115200;
static char                     *opt_tty_line = NULL;
static bool                     opt_supress_ctrls = false;
static unsigned long            opt_send_delay = 0;
static bool                     opt_send_delay_relative = false;
static unsigned const char      *opt_nl_sequence = NULL;
static size_t                   opt_nl_size;
static int                      opt_esc_char;
static char                     *opt_tee_file = NULL;

/***** Static variables -- miscellaneous *****/
static struct termios           saved_console_mode;
static const char*              program_name = "catterm";

/***** Bit rate table *****/
/*
 * bit_rate represents mapping between bit rate value
 * and c_flags bits
 */
typedef struct {
    unsigned long rate;    /* Bit rate value, i.e., 9600 */
    tcflag_t      c_flags; /* c_flags bits, i.e., B9600 */
} bit_rate;

static bit_rate
bit_rate_table[] = {
#ifdef  B50
    {50, B50},
#endif
#ifdef  B75
    {75, B75},
#endif
#ifdef  B110
    {75, B110},
#endif
#ifdef  B134
    {134, B134},
#endif
#ifdef  B150
    {150, B150},
#endif
#ifdef  B200
    {200, B200},
#endif
#ifdef  B300
    {300, B300},
#endif
#ifdef  B600
    {600, B600},
#endif
#ifdef  B1200
    {1200, B1200},
#endif
#ifdef  B1800
    {1800, B1800},
#endif
#ifdef  B2400
    {2400, B2400},
#endif
#ifdef  B4800
    {4800, B4800},
#endif
#ifdef  B9600
    {9600, B9600},
#endif
#ifdef  B19200
    {19200, B19200},
#endif
#ifdef  B38400
    {38400, B38400},
#endif
#ifdef  B57600
    {57600, B57600},
#endif
#ifdef  B115200
    {115200, B115200},
#endif
#ifdef  B230400
    {230400, B230400},
#endif
#ifdef  B460800
    {460800, B460800},
#endif
#ifdef  B500000
    {500000, B500000},
#endif
#ifdef  B576000
    {576000, B576000},
#endif
#ifdef  B921600
    {921600, B921600},
#endif
#ifdef  B1000000
    {1000000, B1000000},
#endif
#ifdef  B1152000
    {1152000, B1152000},
#endif
#ifdef  B1500000
    {1500000, B1500000},
#endif
#ifdef  B2000000
    {2000000, B2000000},
#endif
#ifdef  B2500000 
    {2500000 , B2500000 },
#endif
#ifdef  B3000000
    {3000000, B3000000},
#endif
#ifdef  B3500000
    {3500000, B3500000},
#endif
#ifdef  B4000000
    {4000000, B4000000},
#endif
    {0, 0} /* Must be at the end */
};

/***** Error handling *****/
/*
 * panic with strerror(errno)
 */
#define panic_perror(msg...)                            \
    do{                                                 \
        int     err = errno;                            \
        printf( msg );                                  \
        printf( ": %s\n", strerror( err ) );            \
        exit( 1 );                                      \
    }while(0)

/*
 * panic
 */
#define panic(msg...)                                   \
    do{                                                 \
        printf( msg );                                  \
        printf( "\n" );                                 \
        exit( 1 );                                      \
    }while(0)

/***** Usage *****/
/*
 * Print usage and exit
 */
static void
usage (void)
{
    printf(
        "usage:\n"
        "    catterm [options] line\n"
        "\n"
        "options:\n"
        "    -c       -- suppress control characters on output\n"
        "    -d delay -- delay after each character sent\n"
        "                delay parameter is:\n"
        "                    NNN[us] - microseconds\n"
        "                    NNNms   - milliseconds\n"
        "                    NNN%%    - percent of character transmit time\n"
        "    -n arg   -- send new line as:\n"
        "                    lf      - '\\n' (this is default)\n"
        "                    cr      - '\\r'\n"
        "                    crlf    - '\\r' + '\\n'\n"
        "                    lfcr    - '\\n' + '\\r'\n"
        "    -s speed -- line speed (default is %ld)\n"
        "    -x char  -- use ctrl-char as exit char (default is ctrl-%s)\n"
        "    -t file  -- save (\"tee\") output to file\n"
        "    -h       -- print this help screen\n",
        opt_tty_speed,
        DEFAULT_ESC_CHAR
    );

    exit(1);
}

/*
 * Print usage error and exit
 */
static void
usage_error (const char* error, ...)
{
    va_list ap;
    char    buf[4096];

    va_start( ap, error );
    vsnprintf(buf, sizeof(buf), error, ap);
    va_end( ap );

    printf(
        "%s: %s\n"
        "try %s -h for more information\n",
        program_name, buf, program_name
    );

    exit(1);
}

/***** Memory allocation *****/
/*
 * Allocate some memory. Panic on OOM
 */
static void*
mem_alloc (size_t size)
{
    void *p = malloc(size);

    if (p == NULL) {
        panic_perror("allocation failed");
    }

    memset(p, 0, size);
    return p;
}

/*
 * Safe version of strdup. Panics on OOM
 */
static char*
mem_strdup (const char *s)
{
    size_t sz = strlen(s) + 1;
    char   *p = mem_alloc(sz);

    memcpy(p, s, sz);
    return p;
}

/***** Bit rate mapping *****/
/*
 * Map bit rate to c_flags
 *
 * For unknown/invalid bit rate returns B0
 */
static tcflag_t
bit_rate_to_c_flags (unsigned long rate)
{
    int i;

    for (i = 0; bit_rate_table[i].rate != 0; i ++)
    {
        if (bit_rate_table[i].rate == rate) {
            return bit_rate_table[i].c_flags;
        }
    }

    return B0;
}

/***** Command-line arguments parsing *****/
/*
 * Parse NL sequence (-n option)
 */
static unsigned const char*
parse_nl_sequence (char* s)
{
    static struct { const char *arg, *seq; }
    nl_map[] = {
        { "lf",         "\n" },
        { "cr",         "\r" },
        { "crlf",       "\r\n" },
        { "lfcr",       "\n\r" }
    };
    unsigned int        i;

    for (i = 0; i < sizeof( nl_map ) / sizeof( nl_map[0] ); i ++)
    {
        if (!strcasecmp(s, nl_map[i].arg)) {
            return (unsigned const char*) nl_map[i].seq;
        }
    }

    usage_error("invalid new line mode -- %s", s);
    return NULL;
}

/*
 * Parse speed (-s option)
 */
static unsigned long
parse_speed (char* s)
{
    char          *end;
    unsigned long rate;

    rate = strtoul( s, &end, 0 );
    if (*end) {
        goto USAGE;
    }

    if (bit_rate_to_c_flags(rate) == B0) {
        goto USAGE;
    }

    return rate;

USAGE:
    usage_error("invalid speed -- %s", s);
    return 0;
}

/*
 * Parse ESC character (-x option)
 */
static void
parse_esc_char (char* s)
{
    int c;

    if (!s[0] || s[1]) {
        goto USAGE;
    }

    c = *(unsigned char*) s;
    if (0x40 <= c && c <= 0x5f) {
        opt_esc_char = c - 0x40;
    }

    if (0x60 <= c && c <= 0x7f) {
        opt_esc_char = c - 0x60;
    }

    return;

USAGE:
    usage_error("invalid exit char -- %s", s);
}

/*
 * Parse delay (-d option)
 */
static void
parse_delay (const char* s)
{
    char*       end;

    opt_send_delay = strtoul( s, &end, 0 );
    if (*end == '\0' || !strcasecmp(end, "us")) {
        ;
    } else if (!strcasecmp(end, "ms")) {
        opt_send_delay *= 1000;
    } else if (!strcasecmp(end, "%")) {
        opt_send_delay_relative = true;
    } else {
        usage_error( "invalid output delay -- %s", s );
    }
}

/*
 * Parse command-line options
 */
static void
parse_argv (int argc, char **argv)
{
    int opt;

    /***** Parse options *****/
    while ((opt = getopt(argc, argv, ":cs:x:d:n:t:h")) != EOF) {
        switch (opt) {
            case 'c':
                opt_supress_ctrls = true;
                break;

            case 'n':
                opt_nl_sequence = parse_nl_sequence(optarg);
                break;

            case 's':
                opt_tty_speed = parse_speed(optarg);
                break;

            case 'x':
                parse_esc_char(optarg);
                break;

            case 'd':
                parse_delay(optarg);
                break;

            case 't':
                free(opt_tee_file);
                opt_tee_file = mem_strdup(optarg);
                break;

            case 'h':
                usage();
                break;

            case '?':
                usage_error("invalid option -- -%c", optopt);
                break;
        }
    }

    /***** Fixup output delay *****/
    if (opt_send_delay_relative) {
        opt_send_delay = (1000000 * 9) / opt_tty_speed;
    }

    /***** Fixup opt_nl_size *****/
    if (opt_nl_sequence) {
        opt_nl_size = strlen( (char*) opt_nl_sequence );
    }

    /***** Guess device name *****/
    if (optind + 1 == argc) {
        if (argv[optind][0] == '/') {
            opt_tty_line = argv[optind];
        } else {
            char        prefix[] = "/dev/";

            opt_tty_line = mem_alloc(sizeof(prefix) + strlen(argv[optind]));
            strcpy(opt_tty_line, prefix);
            strcat(opt_tty_line, argv[optind]);
        }
    }else if (optind + 1 < argc) {
        usage_error("unexpected argument -- %s", argv[optind + 1]);
    } else {
        usage_error("missed terminal line");
    }
}

/***** Opening files *****/
/*
 * Open output file. Returns -1, if save to file is not requested
 */
static int
open_tee (void)
{
    int fd = -1;

    if (opt_tee_file != NULL) {
        fd = open(opt_tee_file, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd == -1) {
            panic_perror( "can't open %s", opt_tee_file );
        }
    }

    return fd;
}

/*
 * Open and initialize TTY line
 */
static int
open_tty (void)
{
    int                 fd;
    struct termios      mode;
    int                 tmp;
    speed_t             speed = bit_rate_to_c_flags(opt_tty_speed);

    fd = open(opt_tty_line, O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (fd == -1) {
        panic_perror( "can't open %s", opt_tty_line );
    }

    memset(&mode, 0, sizeof(mode));
    mode.c_cflag = CS8 | HUPCL | CLOCAL | CREAD;
    mode.c_iflag = IGNBRK | IGNPAR;
    mode.c_oflag = 0;
    mode.c_lflag = 0;

    cfsetospeed(&mode, speed);
    cfsetispeed(&mode, speed);

    mode.c_cc[VMIN] = 1;
    mode.c_cc[VTIME] = 0;

    if (tcflush( fd, TCIOFLUSH ) == -1) {
        panic_perror( "tcflush()" );
    }

    if (tcsetattr( fd, TCSANOW, &mode ) == -1) {
        panic_perror( "tcsetattr()" );
    }

    tmp = fcntl(fd, F_GETFD);
    tmp &= ~O_NONBLOCK;
    fcntl(fd, F_SETFL, tmp);

    return fd;
}

/***** TTY mode setting *****/
/*
 * Restore console settins
 */
static void
console_restore (void)
{
    tcsetattr(0, TCSANOW, &saved_console_mode);
}

/*
 * Setup console mode
 */
static void
console_setup (void)
{
    struct termios      mode;

    if (tcgetattr(0, &mode) == -1) {
        panic_perror("tcgetattr(console)");
    }

    saved_console_mode = mode;
    atexit(console_restore);

    mode.c_lflag &= ~(ICANON | ISIG | ECHO);
//    mode.c_cflag |= CSTOPB;

    if (tcsetattr(0, TCSANOW, &mode) == -1) {
        panic_perror("tcsetattr(console)");
    }
}

/***** Miscellaneous helpers *****/
/*
 * Suppress control characters on input
 *
 * Modifies buffer in place and returns new size
 */
static size_t
suppress_ctrls (unsigned char *buffer, size_t size)
{
    size_t      i;

    for (i = 0; i < size; i ++) {
        if (buffer[i] < 0x20 ) {
            switch (buffer[i]) {
                case '\n':
                case '\r':
                case '\b':
                    break;
                default:
                    buffer[i] = '?';
            }
        }
    }

    return size;
}

/***** Main loop *****/
/*
 * microterm - main loop
 */
static void
uterm (int fd_con_in, int fd_con_out, int fd_tty, int fd_tee)
{
    unsigned char       con2tty_buffer[1024];
    size_t              con2tty_count = 0;
    size_t              con2tty_next = 0;
    unsigned char       tty2con_buffer[1024];
    size_t              tty2con_count = 0;
    size_t              tty2con_next = 0;
    int                 fd_max;
    unsigned const char *nl_seq = NULL, *nl_end;

    fd_max= fd_con_in;

    if (fd_con_out > fd_max) {
        fd_max = fd_con_out;
    }

    if (fd_tty > fd_max) {
        fd_max = fd_tty;
    }

    fd_max ++;

    while( 1 )
    {
        fd_set          fds_in, fds_out;
        int             rc;

        FD_ZERO( &fds_in );
        FD_ZERO( &fds_out );

        if (con2tty_next == con2tty_count) {
            FD_SET(fd_con_in, &fds_in);
        } else {
            FD_SET(fd_tty, &fds_out);
        }

        if (tty2con_next == tty2con_count) {
            FD_SET(fd_tty, &fds_in);
        } else {
            FD_SET(fd_con_out, &fds_out);
        }

        rc = select(fd_max, &fds_in, &fds_out, NULL, NULL);
        if (rc <= 0) {
            continue;
        }

        if (FD_ISSET(fd_tty, &fds_in)) {
            rc = read(fd_tty, tty2con_buffer, sizeof(tty2con_buffer));
            if (rc < 0) {
                panic_perror( "read(tty)" );
            } else if (!rc) {
                panic( "read(tty): end of input" );
            }

            if (fd_tee >= 0 && rc > 0) {
                write(fd_tee, tty2con_buffer, rc);
            }

            tty2con_count = rc;
            if (opt_supress_ctrls) {
                tty2con_count = suppress_ctrls(tty2con_buffer,tty2con_count);
            }
            tty2con_next = 0;
        }

        if (FD_ISSET(fd_con_in, &fds_in)) {
            rc = read(fd_con_in, con2tty_buffer, sizeof(con2tty_buffer));
            if (rc < 0) {
                panic_perror( "read(console)" );
            }
            if (memchr(con2tty_buffer, opt_esc_char, rc)) {
                exit(0);
            }

            con2tty_count = rc;
            con2tty_next = 0;
        }

        if (FD_ISSET(fd_tty, &fds_out)) {
            size_t              sz;
            unsigned const char *out = con2tty_buffer + con2tty_next;;

            if (nl_seq) {
                out = nl_seq;
                sz = (size_t) (nl_end - nl_seq);
            } else if(opt_nl_sequence && out[0] == '\n') {
                out = nl_seq = opt_nl_sequence;
                nl_end = opt_nl_sequence + opt_nl_size;
                sz = opt_nl_size;
            } else {
                unsigned char   *s;

                sz = con2tty_count - con2tty_next;
                if (opt_nl_sequence && (s = memchr(out, '\n', sz)) != NULL) {
                    sz = (size_t) (s - out);
                }
            }

            if (opt_send_delay) {
                sz = 1;
            }

            rc = write(fd_tty, out, sz);
            if (rc < 0) {
                panic_perror( "write(tty)" );
            }

            if (nl_seq) {
                nl_seq += rc;
                if ( nl_seq == nl_end ) {
                    nl_seq = NULL;
                    con2tty_next ++;
                }
            } else {
                con2tty_next += rc;
            }

            /*
             * FIXME: delay should not block tty->con transfer
             */
            if (opt_send_delay) {
                usleep(opt_send_delay);
            }
        }

        if (FD_ISSET(fd_con_out, &fds_out)) {
            rc = write(
                fd_con_out, tty2con_buffer + tty2con_next,
                tty2con_count - tty2con_next
            );
            if (rc < 0) {
                panic_perror( "write(console)" );
            }

            tty2con_next += rc;
        }
    }
}

/***** The main function *****/
/*
 * Main function
 */
int
main (int argc, char *argv[])
{
    int fd_tty, fd_tee = -1;

    parse_esc_char(DEFAULT_ESC_CHAR);
    parse_argv(argc, argv);

    fd_tee = open_tee();
    console_setup();
    fd_tty = open_tty();

    uterm(0, 1, fd_tty, fd_tee);

    return 0;
}

/* vim:ts=8:sw=4:et
 */
