#define _DEFAULT_SOURCE  /* for usleep() */

/*
 * PeerTalk Example: Chat Application for POSIX (ncurses)
 *
 * This example demonstrates:
 * - Initializing PeerTalk with callbacks
 * - Peer discovery on the local network
 * - Connecting to discovered peers
 * - Sending and receiving messages
 * - Proper cleanup on shutdown
 *
 * Layout:
 * +------------------------------------------+
 * |           PeerTalk Chat v1.0             |
 * +------------------------------------------+
 * | Peers:                  | Messages:      |
 * |  [C] Alice - connected  |  Alice: Hello  |
 * |  [D] Bob   - discovered |  You: Hi there |
 * |  [ ] Carol - idle       |  Alice: How... |
 * |                         |                |
 * +------------------------------------------+
 * | Status: Connected to 1 peer              |
 * +------------------------------------------+
 * | > _                                      |
 * +------------------------------------------+
 *
 * Commands:
 *   /list          - Show discovered peers
 *   /connect <id>  - Connect to peer by ID
 *   /disconnect    - Disconnect from current peer
 *   /quit          - Exit the application
 *   <text>         - Send message to connected peer(s)
 */

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include "peertalk.h"
#include "pt_log.h"

/* ========================================================================== */
/* Configuration                                                               */
/* ========================================================================== */

#define MAX_MESSAGES    100
#define MAX_MSG_LEN     256
#define MAX_INPUT_LEN   256

/* ========================================================================== */
/* Message History                                                             */
/* ========================================================================== */

typedef struct {
    char text[MAX_MSG_LEN];
    char sender[PT_MAX_PEER_NAME + 1];
    uint8_t is_local;
    uint8_t _pad[3];
} chat_message;

static chat_message g_messages[MAX_MESSAGES];
static int g_message_count = 0;

/* ========================================================================== */
/* Application State                                                           */
/* ========================================================================== */

static PeerTalk_Context *g_ctx = NULL;
static PT_Log *g_log = NULL;
static PeerTalk_PeerInfo g_peers[PT_MAX_PEERS];
static int g_peer_count = 0;
static int g_selected_peer = -1;
static volatile int g_running = 1;
static char g_local_name[PT_MAX_PEER_NAME + 1] = "User";
static char g_status[128] = "Starting...";

/* App-specific log categories */
#define LOG_UI      PT_LOG_CAT_APP1
#define LOG_CHAT    PT_LOG_CAT_APP2
#define LOG_NET     PT_LOG_CAT_NETWORK

/* ========================================================================== */
/* ncurses Windows                                                             */
/* ========================================================================== */

static WINDOW *g_win_peers = NULL;
static WINDOW *g_win_messages = NULL;
static WINDOW *g_win_status = NULL;
static WINDOW *g_win_input = NULL;

/* ========================================================================== */
/* Helper Functions                                                            */
/* ========================================================================== */

static void add_message(const char *sender, const char *text, int is_local)
{
    if (g_message_count >= MAX_MESSAGES) {
        memmove(&g_messages[0], &g_messages[1],
                sizeof(chat_message) * (MAX_MESSAGES - 1));
        g_message_count = MAX_MESSAGES - 1;
    }

    chat_message *msg = &g_messages[g_message_count++];
    strncpy(msg->sender, sender, PT_MAX_PEER_NAME);
    msg->sender[PT_MAX_PEER_NAME] = '\0';
    strncpy(msg->text, text, MAX_MSG_LEN - 1);
    msg->text[MAX_MSG_LEN - 1] = '\0';
    msg->is_local = (uint8_t)is_local;
}

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, ap);
    va_end(ap);
}

static void refresh_peer_list(void)
{
    uint16_t count;
    PeerTalk_GetPeers(g_ctx, g_peers, PT_MAX_PEERS, &count);
    g_peer_count = (int)count;
}

/* ========================================================================== */
/* PeerTalk Callbacks                                                          */
/* ========================================================================== */

static void on_peer_discovered(PeerTalk_Context *ctx,
                               const PeerTalk_PeerInfo *peer,
                               void *user_data)
{
    const char *name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    (void)user_data;
    PT_LOG_INFO(g_log, LOG_NET, "Discovered peer: %s (ID %u)",
                name ? name : "?", (unsigned)peer->id);
    set_status("Discovered: %s", name ? name : "?");
    refresh_peer_list();
}

static void on_peer_lost(PeerTalk_Context *ctx,
                         PeerTalk_PeerID peer_id,
                         void *user_data)
{
    (void)ctx;
    (void)user_data;
    PT_LOG_INFO(g_log, LOG_NET, "Peer lost: ID %u", (unsigned)peer_id);
    set_status("Peer lost: ID %u", (unsigned)peer_id);
    refresh_peer_list();

    if (g_selected_peer == (int)peer_id) {
        g_selected_peer = -1;
    }
}

static void on_peer_connected(PeerTalk_Context *ctx,
                              PeerTalk_PeerID peer_id,
                              void *user_data)
{
    const PeerTalk_PeerInfo *peer = PeerTalk_GetPeerByID(ctx, peer_id);
    const char *name = peer ? PeerTalk_GetPeerName(ctx, peer->name_idx) : "Unknown";
    (void)user_data;

    refresh_peer_list();

    PT_LOG_INFO(g_log, LOG_NET, "Connected to peer: %s (ID %u)",
                name, (unsigned)peer_id);
    set_status("Connected to %s", name);
    add_message("System", "Connected", 0);

    if (g_selected_peer < 0) {
        g_selected_peer = (int)peer_id;
    }
}

static void on_peer_disconnected(PeerTalk_Context *ctx,
                                 PeerTalk_PeerID peer_id,
                                 PeerTalk_Error reason,
                                 void *user_data)
{
    (void)ctx;
    (void)user_data;
    PT_LOG_WARN(g_log, LOG_NET, "Disconnected from peer %u (reason: %d)",
                (unsigned)peer_id, (int)reason);
    set_status("Disconnected (reason: %d)", (int)reason);
    add_message("System", "Disconnected", 0);
    refresh_peer_list();

    if (g_selected_peer == (int)peer_id) {
        g_selected_peer = -1;
    }
}

static void on_message_received(PeerTalk_Context *ctx,
                                PeerTalk_PeerID from_peer,
                                const void *data,
                                uint16_t length,
                                void *user_data)
{
    const PeerTalk_PeerInfo *peer = PeerTalk_GetPeerByID(ctx, from_peer);
    const char *sender = peer ? PeerTalk_GetPeerName(ctx, peer->name_idx) : "Unknown";
    char text[MAX_MSG_LEN];
    int len;
    (void)user_data;

    PT_LOG_DEBUG(g_log, LOG_CHAT, "Message from %s (%u): %u bytes",
                 sender, (unsigned)from_peer, (unsigned)length);

    len = (length < MAX_MSG_LEN - 1) ? (int)length : MAX_MSG_LEN - 1;
    memcpy(text, data, (size_t)len);
    text[len] = '\0';

    add_message(sender, text, 0);
}

/* ========================================================================== */
/* UI Drawing                                                                  */
/* ========================================================================== */

static void draw_peers(void)
{
    int y, max_y, i;
    char state;
    const char *name;

    werase(g_win_peers);
    box(g_win_peers, 0, 0);
    mvwprintw(g_win_peers, 0, 2, " Peers ");

    y = 1;
    max_y = getmaxy(g_win_peers) - 2;

    for (i = 0; i < g_peer_count && y <= max_y; i++) {
        state = g_peers[i].connected ? 'C' : 'D';

        if ((int)g_peers[i].id == g_selected_peer) {
            wattron(g_win_peers, A_REVERSE);
        }

        name = PeerTalk_GetPeerName(g_ctx, g_peers[i].name_idx);
        mvwprintw(g_win_peers, y, 1, "[%c] %u: %.12s",
                  state, (unsigned)g_peers[i].id, name ? name : "?");

        if ((int)g_peers[i].id == g_selected_peer) {
            wattroff(g_win_peers, A_REVERSE);
        }

        y++;
    }

    if (g_peer_count == 0) {
        mvwprintw(g_win_peers, 1, 1, "(no peers)");
    }

    wrefresh(g_win_peers);
}

static void draw_messages(void)
{
    int max_y, max_x, start, y, i;
    char line[256];

    werase(g_win_messages);
    box(g_win_messages, 0, 0);
    mvwprintw(g_win_messages, 0, 2, " Messages ");

    max_y = getmaxy(g_win_messages) - 2;
    max_x = getmaxx(g_win_messages) - 2;
    start = (g_message_count > max_y) ? g_message_count - max_y : 0;

    y = 1;
    for (i = start; i < g_message_count && y <= max_y; i++) {
        chat_message *msg = &g_messages[i];

        if (msg->is_local) {
            wattron(g_win_messages, A_BOLD);
        }

        snprintf(line, sizeof(line), "%s: %s", msg->sender, msg->text);
        if ((int)strlen(line) > max_x) {
            if (max_x > 3) {
                line[max_x - 3] = '.';
                line[max_x - 2] = '.';
                line[max_x - 1] = '.';
            }
            line[max_x] = '\0';
        }

        mvwprintw(g_win_messages, y, 1, "%s", line);

        if (msg->is_local) {
            wattroff(g_win_messages, A_BOLD);
        }

        y++;
    }

    wrefresh(g_win_messages);
}

static void draw_status(void)
{
    werase(g_win_status);
    box(g_win_status, 0, 0);
    mvwprintw(g_win_status, 1, 1, "%s", g_status);
    wrefresh(g_win_status);
}

static void draw_input(const char *input)
{
    werase(g_win_input);
    box(g_win_input, 0, 0);
    mvwprintw(g_win_input, 1, 1, "> %s_", input);
    wrefresh(g_win_input);
}

static void draw_all(const char *input)
{
    draw_peers();
    draw_messages();
    draw_status();
    draw_input(input);
}

/* ========================================================================== */
/* Command Handling                                                            */
/* ========================================================================== */

static void handle_command(const char *input)
{
    /* Regular message - send to selected peer */
    if (input[0] != '/') {
        if (g_selected_peer > 0) {
            PeerTalk_Error err = PeerTalk_Send(g_ctx,
                (PeerTalk_PeerID)g_selected_peer,
                input, (uint16_t)(strlen(input) + 1));
            if (err == PT_OK) {
                add_message(g_local_name, input, 1);
            } else {
                set_status("Send failed: %s", PeerTalk_ErrorString(err));
            }
        } else {
            set_status("No peer selected. Use /connect <id>");
        }
        return;
    }

    /* /quit */
    if (strncmp(input, "/quit", 5) == 0 || strncmp(input, "/q", 2) == 0) {
        g_running = 0;
        return;
    }

    /* /list */
    if (strncmp(input, "/list", 5) == 0 || strncmp(input, "/l", 2) == 0) {
        refresh_peer_list();
        set_status("Found %d peers", g_peer_count);
        return;
    }

    /* /connect <id> */
    if (strncmp(input, "/connect ", 9) == 0 || strncmp(input, "/c ", 3) == 0) {
        int id;
        const char *num = (input[1] == 'c' && input[2] == ' ') ?
                          input + 3 : input + 9;
        if (sscanf(num, "%d", &id) == 1) {
            PeerTalk_Error err;
            set_status("Connecting to peer %d...", id);
            err = PeerTalk_Connect(g_ctx, (PeerTalk_PeerID)id);
            if (err != PT_OK) {
                set_status("Connect failed: %s", PeerTalk_ErrorString(err));
            }
        } else {
            set_status("Usage: /connect <peer_id>");
        }
        return;
    }

    /* /disconnect */
    if (strncmp(input, "/disconnect", 11) == 0 || strncmp(input, "/d", 2) == 0) {
        if (g_selected_peer > 0) {
            PeerTalk_Disconnect(g_ctx, (PeerTalk_PeerID)g_selected_peer);
            g_selected_peer = -1;
        } else {
            set_status("Not connected");
        }
        return;
    }

    /* /help */
    if (strncmp(input, "/help", 5) == 0 || strncmp(input, "/h", 2) == 0) {
        add_message("Help", "/list - show peers", 0);
        add_message("Help", "/connect <id> - connect to peer", 0);
        add_message("Help", "/disconnect - disconnect", 0);
        add_message("Help", "/quit - exit", 0);
        return;
    }

    set_status("Unknown command. Type /help for help.");
}

/* ========================================================================== */
/* Signal Handler                                                              */
/* ========================================================================== */

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ========================================================================== */
/* Main                                                                        */
/* ========================================================================== */

int main(int argc, char **argv)
{
    PeerTalk_Config config;
    PeerTalk_Callbacks callbacks;
    char input[MAX_INPUT_LEN];
    int input_pos = 0;
    int term_height, term_width;
    int peer_width, msg_width, content_height;
    int ch;

    memset(&config, 0, sizeof(config));
    memset(&callbacks, 0, sizeof(callbacks));
    memset(input, 0, sizeof(input));

    /* Parse arguments */
    if (argc > 1) {
        strncpy(g_local_name, argv[1], PT_MAX_PEER_NAME);
        g_local_name[PT_MAX_PEER_NAME] = '\0';
    }

    /* Set up signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize PT_Log BEFORE ncurses (so errors are visible) */
    g_log = PT_LogCreate();
    if (g_log) {
        PT_LogSetFile(g_log, "chat_posix.log");
        PT_LogSetLevel(g_log, PT_LOG_INFO);
        PT_LogSetOutput(g_log, PT_LOG_OUT_FILE);
        PT_LogSetAutoFlush(g_log, 1);
        PT_LOG_INFO(g_log, LOG_UI, "Chat starting as '%s'", g_local_name);
    }

    /* Initialize ncurses */
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);

    /* Get terminal size */
    getmaxyx(stdscr, term_height, term_width);

    /* Create windows */
    peer_width = term_width / 3;
    msg_width = term_width - peer_width;
    content_height = term_height - 6;

    g_win_peers = newwin(content_height, peer_width, 0, 0);
    g_win_messages = newwin(content_height, msg_width, 0, peer_width);
    g_win_status = newwin(3, term_width, content_height, 0);
    g_win_input = newwin(3, term_width, content_height + 3, 0);

    /* Initialize PeerTalk */
    strncpy(config.local_name, g_local_name, PT_MAX_PEER_NAME);
    config.local_name[PT_MAX_PEER_NAME] = '\0';
    config.max_peers = PT_MAX_PEERS;

    /* Set up callbacks */
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_lost = on_peer_lost;
    callbacks.on_peer_connected = on_peer_connected;
    callbacks.on_peer_disconnected = on_peer_disconnected;
    callbacks.on_message_received = on_message_received;

    g_ctx = PeerTalk_Init(&config);
    if (!g_ctx) {
        PT_LOG_ERR(g_log, LOG_NET, "Failed to initialize PeerTalk");
        endwin();
        if (g_log) PT_LogDestroy(g_log);
        fprintf(stderr, "Failed to initialize PeerTalk\n");
        return 1;
    }
    PT_LOG_INFO(g_log, LOG_NET, "PeerTalk initialized successfully");

    PeerTalk_SetCallbacks(g_ctx, &callbacks);

    /* Start discovery */
    PeerTalk_StartDiscovery(g_ctx);
    set_status("Discovering peers as '%s'...", g_local_name);

    add_message("System", "PeerTalk Chat started. Type /help for commands.", 0);

    /* Main loop */
    while (g_running) {
        /* Poll PeerTalk for network events */
        PeerTalk_Poll(g_ctx);

        /* Refresh peer list periodically */
        refresh_peer_list();

        /* Draw UI */
        draw_all(input);

        /* Handle keyboard input (non-blocking) */
        ch = getch();
        if (ch != ERR) {
            if (ch == '\n' || ch == '\r') {
                if (input_pos > 0) {
                    input[input_pos] = '\0';
                    handle_command(input);
                    input_pos = 0;
                    input[0] = '\0';
                }
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (input_pos > 0) {
                    input[--input_pos] = '\0';
                }
            } else if (ch >= 32 && ch < 127 && input_pos < MAX_INPUT_LEN - 1) {
                input[input_pos++] = (char)ch;
                input[input_pos] = '\0';
            }
        }

        /* Small delay to avoid busy-waiting */
        usleep(20000);  /* 20ms = ~50 FPS */
    }

    /* Cleanup */
    set_status("Shutting down...");
    draw_all(input);
    PT_LOG_INFO(g_log, LOG_UI, "Chat shutting down");

    PeerTalk_StopDiscovery(g_ctx);
    PeerTalk_Shutdown(g_ctx);

    delwin(g_win_peers);
    delwin(g_win_messages);
    delwin(g_win_status);
    delwin(g_win_input);
    endwin();

    PT_LOG_INFO(g_log, LOG_UI, "Chat shutdown complete");
    if (g_log) PT_LogDestroy(g_log);

    printf("Goodbye!\n");
    return 0;
}
