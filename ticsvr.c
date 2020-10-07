#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Linked list to hold connections
struct connection {
    int clientfd;
    struct connection *next;
	char *buf;
	int bytes_in_buf;
};

struct connection *head = NULL;

int insert(int clientfd) {
    struct connection **pp = &head;
    struct connection *n;

    if ((n = malloc(sizeof(struct connection))) == NULL) {
        perror("malloc");
        return(1);
    }

    n->clientfd = clientfd;
    n->next = NULL;
	n->bytes_in_buf = 0;

	n->buf = malloc(1024);
	if (n->buf == NULL) {
		perror("malloc");
		return(1);
	}

    for (pp = &head; *pp; pp = &(*pp)->next) {
        if (*pp == NULL) {
            break;
        }
    }

    n->next = *pp;
    *pp = n;

	return(0);
}

void delete(int clientfd) {
    struct connection *curr = head;
    struct connection *prev = NULL;

    while (curr != NULL) {
        if (curr->clientfd == clientfd) {
            if (prev != NULL) {
                prev->next = curr->next;
                free(curr->buf);
				free(curr);
            } else {
                head = curr->next;
                free(curr->buf);
				free(curr);
            }

            break;
        }

        prev = curr;
        curr = curr->next;
    }
}

void printall() {
    struct connection *p;
    for (p = head; p; p = p->next)
        printf("%d\n", p->clientfd);
    printf("[end]\n");
}

// Create board as global variable
char board[9];

// Create global variables holding turns and players w/ turns
int x, o, turn;

void resetboard() {
    for (int i = 0; i < 9; i++) {
        board[i] = '1' + i;
    }
}

// Functions from 'usefulcode'
void showboard(int fd) {
    char buf[100], *bufp, *boardp;
    int col, row;

    for (bufp = buf, col = 0, boardp = board; col < 3; col++) {
        for (row = 0; row < 3; row++, bufp += 4)
            sprintf(bufp, " %c |", *boardp++);
        bufp -= 2;  // kill last " |"
        strcpy(bufp, "\r\n---+---+---\r\n");
        bufp = strchr(bufp, '\0');
    }
    if (write(fd, buf, bufp - buf) != bufp-buf)
        perror("write");
}

// Returns winner, or ' ' for draw, or 0 for not over
int game_is_over() {
    int i, c;
    extern int allthree(int start, int offset);
    extern int isfull();

    for (i = 0; i < 3; i++)
        if ((c = allthree(i, 3)) || (c = allthree(i * 3, 1)))
            return(c);
    if ((c = allthree(0, 4)) || (c = allthree(2, 2)))
        return(c);
    if (isfull())
        return(' ');
    return(0);
}

int allthree(int start, int offset) {
    if (board[start] > '9' && board[start] == board[start + offset]
            && board[start] == board[start + offset * 2])
        return(board[start]);
    return(0);
}

int isfull() {
    int i;
    for (i = 0; i < 9; i++)
        if (board[i] < 'a')
            return(0);
    return(1);
}

char *extractline(char *p, int size)
        /* returns pointer to string after, or NULL if there isn't an entire
         * line here.  If non-NULL, original p is now a valid string. */
{
    int nl;
    for (nl = 0; nl < size && p[nl] != '\r' && p[nl] != '\n'; nl++)
        ;
    if (nl == size)
        return(NULL);

    /*
     * There are three cases: either this is a lone \r, a lone \n, or a CRLF.
     */
    if (p[nl] == '\r' && nl + 1 < size && p[nl+1] == '\n') {
        /* CRLF */
        p[nl] = '\0';
        return(p + nl + 2);
    } else {
        /* lone \n or \r */
        p[nl] = '\0';
        return(p + nl + 1);
    }
}

int turnMessage(int fd, int turn) {
	if (turn == 0) {
		if (write(fd, "It is x's turn.\r\n", 17) != 17) {
			perror("write");
			return(1);
		}
	} else {
		if (write(fd, "It is o's turn.\r\n", 17) != 17) {
			perror("write");
			return(1);
		}
	}
	return(0);
}

int youAreNowMessage(int fd, int clientTurn) {
	if (clientTurn == 0) {
		if (write(fd, "You are now x.\r\n", 16) != 16) {
			perror("write");
			return(1);
		}
	} else {
		if (write(fd, "You are now o.\r\n", 16) != 16) {
			perror("write");
			return(1);
		}
	}
	return(0);
}

int gameOverMessage(int gameState) {
	char endmessage[50];

	if (gameState == ' ') {
		strcpy(endmessage, "The game has ended as a draw.\r\n");
		printf("The game has ended as a draw.\n");
	} else {
		snprintf(endmessage, sizeof(endmessage), "The winner is %c!\r\n", gameState);
		printf("The winner is %c\n", gameState);
	}

	struct connection *curr = head;
	while (curr != NULL) {
		if (gameState == ' ') {
			if (write(curr->clientfd, endmessage, 30) != 30) {
				perror("write");
				return(1);
			}
		} else {
			if (write(curr->clientfd, endmessage, 18) != 18) {
				perror("write");
				return(1);
			}
		}

		if (write(curr->clientfd, "Let's play again.\r\n", 19) != 19) {
			perror("write");
			return(1);
		}

		showboard(curr->clientfd);

		if (write(curr->clientfd, "It is x's turn.\r\n", 17) != 17) {
            perror("write");
            return(1);
        }

		curr = curr->next;
	}
	return(0);
}

int setNewPlayer(int player) {
	if (player == 0) {
		x = -1;
	} else {
		o = -1;
	}

    socklen_t size;
    struct sockaddr_in q;

	struct connection *curr = head;
	while (curr != NULL) {
		if (curr->clientfd != x && curr->clientfd != o) {
			getpeername(curr->clientfd, (struct sockaddr *)&q, &size);
			if (x == -1) {
				x = curr->clientfd;
				youAreNowMessage(curr->clientfd, 0);
				printf("%s is now x\n", inet_ntoa(q.sin_addr));
			} else {
				o = curr->clientfd;
				youAreNowMessage(curr->clientfd, 1);
				printf("%s is now o\n", inet_ntoa(q.sin_addr));
			}
			return(0);
		}
		curr = curr->next;
	}
	return(1);
}

int invalidMoveMessage(int fd) {
    if (write(fd, "You tried to make an invalid move.\r\n", 36) != 36) {
        perror("write");
        return(1);
    }
    return(0);
}

int spectatingMessage(int fd) {
    if (write(fd, "You are only spectating!\r\n", 26) != 26) {
        perror("write");
        return(1);
    }
    return(0);
}

int notYourTurnMessage(int fd) {
    if (write(fd, "It is not your turn!\r\n", 22) != 22) {
        perror("write");
        return(1);
    }
    return(0);
}

void showBoardToAll() {
    struct connection *curr = head;
    while (curr != NULL) {
        showboard(curr->clientfd);
        curr = curr->next;
    }
}

int showNewTurn(int turn) {
    struct connection *curr = head;

    while (curr != NULL) {
        if (turnMessage(curr->clientfd, turn) == 1) {
            return(1);
        }
        curr = curr->next;
    }
    return(0);
}

int performMove(char move, int clientfd) {
	socklen_t size;
	struct sockaddr_in q;

	getpeername(clientfd, (struct sockaddr *)&q, &size);

	if (board[move - 49] != move) {
		printf("%s tried to make an invalid move on %c\n", inet_ntoa(q.sin_addr), move);
		invalidMoveMessage(clientfd);
		return(1);
	}

	if (clientfd == x) {
		if (turn == 0) {
			board[move - 49] = 'x';
			turn = 1;
			printf("%s (x) made a move on %c\n", inet_ntoa(q.sin_addr), move);
			showBoardToAll();
			showNewTurn(turn);
			return(0);
		}
		printf("%s tried to make a valid move on %c, but it's not their turn\n", inet_ntoa(q.sin_addr), move);
		notYourTurnMessage(clientfd);
		return(1);
	} else if (clientfd == o) {
		if (turn == 1) {
			board[move - 49] = 'o';
			turn = 0;
			printf("%s (o) made a move on %c\n", inet_ntoa(q.sin_addr), move);
			showBoardToAll();
			showNewTurn(turn);
			return(0);
		}
		printf("%s tried to make a valid move on %c, but it's not their turn\n", inet_ntoa(q.sin_addr), move);
		notYourTurnMessage(clientfd);
		return(1);
	}
	printf("%s tried to make a valid move on %c, but they are only spectating\n", inet_ntoa(q.sin_addr), move);
	spectatingMessage(clientfd);
	return(1);
}

int transmitMessage(char *message, size_t size, int clientfd) {
	struct connection *curr = head;
	while (curr != NULL) {
		if (curr->clientfd != clientfd) {
			if (write(curr->clientfd, message, size) != size) {
				perror("write");
				return(1);
			}
		}
		curr = curr->next;
	}
	return(0);
}

int main(int argc, char **argv) {
	x = -1;
	o = -1;
	turn = 0;

    int c, port = 58885, status = 0;
    int fd, clientfd;
    socklen_t size;
    struct sockaddr_in r, q;

    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = atoi(optarg);
            break;
        default:
            status = 1;
        }
    }

    if (status || optind < argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        return(1);
    }

	// Initialize board
	resetboard();

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return(1);
    }

    memset(&r, '\0', sizeof r);
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&r, sizeof r) < 0) {
        perror("bind");
        return(1);
    }

    if (listen(fd, 5)) {
        perror("listen");
        return(1);
    }

	fd_set readfds;
	int numfd, clientTurn, gameState, len, prev;
	struct connection *curr;
	char *nextpos;

	while (1) {
		gameState = game_is_over();
		if (gameState != 0) {
			resetboard();
			gameOverMessage(gameState);
			turn = 0;
		}

		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);
		numfd = fd;
		curr = head;

		while (curr != NULL) {
			FD_SET(curr->clientfd, &readfds);
			if (curr->clientfd > numfd) {
				numfd = clientfd;
			}
			curr = curr->next;

		}

		if (select(numfd + 1, &readfds, NULL, NULL, NULL) != 1) {
			printf("error");
			perror("select");
			return(1);
		}

		// New client connecting
		if (FD_ISSET(fd, &readfds)) {
			size = sizeof q;
    		if ((clientfd = accept(fd, (struct sockaddr *)&q, &size)) < 0) {
        		perror("accept");
        		return(1);
    		}

			if (insert(clientfd) == 1) {
				return(1);
			}

			printf("new connection from %s\n", inet_ntoa(q.sin_addr));
			showboard(clientfd);

			clientTurn = -1;
			if (x == -1) {
				x = clientfd;
				clientTurn = 0;
				printf("client from %s is now x\n", inet_ntoa(q.sin_addr));
			} else if (o == -1) {
				o = clientfd;
				clientTurn = 1;
				printf("client from %s is now o\n", inet_ntoa(q.sin_addr));
			}

			if (turnMessage(clientfd, turn) != 0) {
				return(1);
			}

			if (clientTurn != -1) {
				if (youAreNowMessage(clientfd, clientTurn) != 0) {
					return(1);
				}
			}
		}

		// Recieve responses from already connected clients
		curr = head;
		while (curr != NULL) {
			if (FD_ISSET(curr->clientfd, &readfds)) {
				if ((len = read(curr->clientfd, curr->buf + curr->bytes_in_buf,
					1024 - curr->bytes_in_buf - 1)) == 0) {
					getpeername(curr->clientfd, (struct sockaddr *)&q, &size);
					printf("%s disconnected\n", inet_ntoa(q.sin_addr));
					prev = curr->clientfd;
					curr = curr->next;
					if (close(prev) == -1) {
						perror("close");
						return(1);
					}
					delete(prev);

					if (x == prev) {
						setNewPlayer(0);
					} else if (o == prev) {
						setNewPlayer(1);
					}
					continue;
				} else if (len > 0) {
					curr->bytes_in_buf = curr->bytes_in_buf + len;
					getpeername(curr->clientfd, (struct sockaddr *)&q, &size);
					while ((nextpos = extractline(curr->buf, curr->bytes_in_buf))) {
						if ((nextpos - curr->buf) == 2) {
							if (*(curr->buf + 1) == '\0') {
								if (*(curr->buf) <= 57 && *(curr->buf) >= 49) {
									performMove(*(curr->buf), curr->clientfd);
								} else {
									invalidMoveMessage(curr->clientfd);
								}
							} else {
								invalidMoveMessage(curr->clientfd);
							}
						} else if ((nextpos - curr->buf) == 1) {
							if (*(curr->buf) <= 57 && *(curr->buf) >= 49) {
								performMove(*(curr->buf), curr->clientfd);
							} else {
								invalidMoveMessage(curr->clientfd);
							}
						} else {
							transmitMessage(curr->buf, nextpos - curr->buf, curr->clientfd);
							transmitMessage("\r\n", 2, curr->clientfd);
							printf("message: %s\n", curr->buf);
						}
						curr->bytes_in_buf -= nextpos - curr->buf;
						memmove(curr->buf, nextpos, curr->bytes_in_buf);
					}
				} else {
					perror("read");
					return(1);
				}
			}
			curr = curr->next;
		}
	}
}

