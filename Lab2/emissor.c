#include "common.h"
#include "signal.h"
#include "emissor.h"

#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"

const u_int8_t BCC_SET = EMISSOR_CMD_ABYTE ^ SET_CONTROL_BYTE; // Protection fields
const u_int8_t BCC_UA = RECEPTOR_ANSWER_ABYTE ^ UA_CONTROL_BYTE;

volatile int STOP_EXEC=FALSE;

int messageFlag = 1, conta = 0;

void atende() {
   printf("alarme # %d\n", conta + 1);
   messageFlag = 1;
   conta++;
}

int sendSet(int fd, int alarmInterval) {
  State state = START;
  u_int8_t ans[5] = {FLAG_BYTE, EMISSOR_CMD_ABYTE, SET_CONTROL_BYTE, BCC_SET, FLAG_BYTE}; 

  while (state != STOP) {
    if (conta == 3) {
      printf("Communication failed\n");
      return 1;
    }

    if (messageFlag) {
      write(fd, ans, sizeof(ans));
      messageFlag = 0;
      state = START;
      alarm(alarmInterval);
    }

    u_int8_t byte, addr_byte, bcc;
    int res;
    res = read(fd, &byte, 1);
    if (res == -1) {
      printf("Read error\n");
      return 1;
    }

    if (state == START) {
      if (byte == FLAG_BYTE) state = FLAG_RCV;
    }
    else if (state == FLAG_RCV) {
      if (byte == FLAG_BYTE) continue;
      else if (byte == RECEPTOR_ANSWER_ABYTE) {
        addr_byte = byte;
        state = ADDR_RCV;
      }
      else state = START;
    }
    else if (state == ADDR_RCV) {
      if (byte == FLAG_BYTE) state = FLAG_RCV;
      else if (byte == UA_CONTROL_BYTE) {
        bcc = addr_byte ^ byte;
        state = CTRL_RCV;
      }
      else state = START;
    }
    else if (state == CTRL_RCV) {
      if (byte == FLAG_BYTE) state = FLAG_RCV;
      else if (bcc == byte) state = BCC_OK;
      else state = START;
    }
    else if (state == BCC_OK) {
      if (byte == FLAG_BYTE) state = STOP;
      else state = START;
    }
  }

  printf("Read UA, success!\n");
  return 0;
}

int main(int argc, char** argv) {
    int fd,c, res_read = 0;
    struct termios oldtio,newtio;
    u_int8_t buf[255];
    int i, sum = 0, speed = 0;
    
    if ( (argc < 2) || 
  	     ((strcmp("/dev/ttyS0", argv[1])!=0) && 
  	      (strcmp("/dev/ttyS1", argv[1])!=0) )) {
      printf("Usage:\tnserial SerialPort\n\tex: nserial /dev/ttyS1\n");
      exit(1);
    }

    (void)signal(SIGALRM, atende);

  /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
  */


    fd = open(argv[1], O_RDWR | O_NOCTTY );
    if (fd <0) {perror(argv[1]); exit(-1); }

    if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
      perror("tcgetattr");
      exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME] = 1;   /* unblock after 0.1secs or 1 char received */
    newtio.c_cc[VMIN] = 0;

  /* 
    VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a 
    leitura do(s) pr�ximo(s) caracter(es)
  */

    tcflush(fd, TCIOFLUSH);

    if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

    printf("New termios structure set\n");
    printf("Sending SET...\n");

    if (sendSet(fd, 3)) exit(1);
    
    sleep(1); // Avoid changing config before sending data (transmission error)
    if (tcsetattr(fd,TCSANOW,&oldtio) == -1) {
          perror("tcsetattr");
          exit(-1);
    }
    close(fd);
    printf("Done\n");
    return 0;
}
